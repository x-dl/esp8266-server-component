/*
 * @Descripttion: 
 * @version: 
 * @Author: Jasper
 * @Date: 2021-09-19 15:26:28
 * @LastEditors: Jasper
 * @LastEditTime: 2021-10-06 10:44:38
 * @FilePath: \FreeRTOS_Padding\app\threadx_dbs.c
 */
#include "FreeRTOS.h"
#include "stm32f4xx.h"
#include "semphr.h"
#include "stdio.h"
#include "task.h"
#include "timers.h"
#include "string.h"
#include "stdlib.h"
/* ��Ӹ÷������������Ҫ��Ӳ������������������Ӳ��ƽ̨�й� */
static void BSP_UART_Init(void);
static void BSP_TIM6_Init(void);
static void BSP_DMA_Init(DMA_Stream_TypeDef *DMA_Streamx, u32 chx, u32 par, u32 mar, u16 ndtr);
static void BSP_SDMA(DMA_Stream_TypeDef *DMA_Streamx, u32 chx, u32 par, u32 mar, u16 ndtr);
/* �������������Ӳ��ƽ̨�޹صĺ��� */
static void myprintf3(char *str);
static void myprintf3_INT(char *str); //������Ҫע�⣬�ַ�����/0��β
static void myprintf3_DMA(char *str);
static void myprintf3_DMA_cnt(char *str, int cnt); //������Ҫע�⣬�ַ�����/0��β
void USART3_IRQ_Enable(void);
void USART3_IRQ_Disable(void);
static void vThreadx_task(void *pvParameters);
static void esp8266_task(void *pvParameters);
static void recycle_task(void *pvParameters);
static unsigned char reqid_get(char *recv); //��ȡ����id�� ����id�����ڿͻ���
/* ����������е��ź��� */
static SemaphoreHandle_t xSemaphore = NULL;
static xSemaphoreHandle private_SEVER_thread_mutex = 0;
static xSemaphoreHandle DMA_Transmit_semaphore = pdFALSE;
/* �������õ����ַ��� */
static const char esp8266_cmd[][40];
static const char esp82666_hello[];
static const char esp82666_response[];
static const char Thread_Name[][10];
/* �߳̿��ƿ� */
typedef struct idtask
{
    TaskHandle_t IDTask_Handler;
    unsigned char thread_status;
    unsigned char client_status;
    unsigned char thread_ID;
    char task[10];
} IDtask;
/* ��غ궨�� */
#define TICKS_TO_WAIT 10
#define USART_REC_LEN 200     //�����������ֽ��� 200
#define SERVICE_NAME_LENGTH 5 //�������ṩ��������Ƶ���Ŀ��5
#define SEVER_TASK_PRIO 6
//�������ȼ�
#define THREADX_TASK_PRIO (SEVER_TASK_PRIO - 1)
//�����ջ��С
#define THREADX_STK_SIZE 200
//�������ȼ�
#define esp8266_TASK_PRIO (SEVER_TASK_PRIO - 1)
//�����ջ��С
#define esp8266_STK_SIZE 128
//�������ȼ�
#define RECYCLE_TASK_PRIO (SEVER_TASK_PRIO - 2)
//�����ջ��С
#define RECYCLE_STK_SIZE 128
//������
TaskHandle_t esp8266_Task_Handler;
TaskHandle_t recycle_task_Handler;
/* ��������������ȫ�ֱ��� */
static u8 USART_RX_BUF[USART_REC_LEN] = {0}; //���ջ���,���USART_REC_LEN���ֽ�.
static u16 USART_RX_CNT = 0;                 //����״̬���
static u16 USART_RX_PRECNT = 0;              //����״̬���
static u8 flag = pdFALSE;                    //��������״̬��־��Ĭ�Ϸ�������û��׼���õ�
static u8 isr_flag = pdTRUE;
static unsigned char *pc_SEVER_myprintf1 = NULL;
void Sever_task(void *pvParameters)
{
    unsigned char *ucpsend_item = USART_RX_BUF;
    char send[20] = {"hello world!!!\r\n"};
    unsigned char request_id = NULL;
    IDtask IDArray[5] = {0};       /* ��¼����̵߳�״̬ */
    IDtask *Current_IDtask = NULL; /* ָ��ǰ���� */
    char(*Current_Thread_Name)[10] = NULL;
    /* ��ʼ��Ӳ�� */
    BSP_UART_Init();
    BSP_TIM6_Init();
    BSP_DMA_Init(DMA1_Stream3, DMA_Channel_4, (u32)&USART3->DR, (u32)send, (u16)strlen(send)); //���ü򻯰��DMA
    USART_DMACmd(USART3, USART_DMAReq_Tx, ENABLE);                                             //ʹ�ܴ���1��DMA����
    /*�����ź��� */
    xSemaphore = xSemaphoreCreateBinary();
    DMA_Transmit_semaphore = xSemaphoreCreateBinary();
    private_SEVER_thread_mutex = xSemaphoreCreateMutex();
    taskENTER_CRITICAL();                               //�����ٽ���
    xTaskCreate((TaskFunction_t)recycle_task,           //������
                (const char *)"recycle",                //��������
                (uint16_t)RECYCLE_STK_SIZE,             //�����ջ��С
                (void *)IDArray,                        //���ݸ��������Ĳ���
                (UBaseType_t)RECYCLE_TASK_PRIO,         //�������ȼ�
                (TaskHandle_t *)&recycle_task_Handler); //������
    taskEXIT_CRITICAL();                                //�˳��ٽ���
    if (private_SEVER_thread_mutex == NULL)
    {
        printf("private_SEVER_thread_mutex fail\r\n");
    }
    if (DMA_Transmit_semaphore == NULL)
    {
        printf("DMA_Transmit_semaphore fail\r\n");
    }
    if (xSemaphore == NULL)
    {
        /* There was insufficient FreeRTOS heap available for the semaphore to
        be created successfully. */
        printf("sever xsemaphore fail\r\n");
    }
    else /* �ź���ȫ�������ɹ���ʼ��������sever */
    {
        printf("try to acquire sever\r\n");
        myprintf3((char *)(((char(*)[40])(*esp8266_cmd)) + 6)); //�鿴��ǰesp8266�Ƿ��Ѿ�������wifi
        //printf((const char *)(((char(*)[40])(*esp8266_cmd)) + 6)); //�鿴��ǰesp8266�Ƿ��Ѿ�������wifi
        if (xSemaphoreTake(xSemaphore, portMAX_DELAY / portTICK_PERIOD_MS) == pdTRUE)
        {
            isr_flag = pdTRUE;
            //printf((const char *)USART_RX_BUF);                         //ȷ�����ڿ��Ի�ȡ����
            if (strstr((const char *)USART_RX_BUF, "No AP") != pdFALSE) //���������������ζ���ʱ��û���������ȵ�
            {
                taskENTER_CRITICAL();                               //�����ٽ���
                xTaskCreate((TaskFunction_t)esp8266_task,           //������
                            (const char *)"esp8266",                //��������
                            (uint16_t)esp8266_STK_SIZE,             //�����ջ��С
                            (void *)NULL,                           //���ݸ��������Ĳ���
                            (UBaseType_t)esp8266_TASK_PRIO,         //�������ȼ�
                            (TaskHandle_t *)&esp8266_Task_Handler); //������
                taskEXIT_CRITICAL();                                //�˳��ٽ���
            }
            else if (strstr((const char *)USART_RX_BUF, "+CWJAP:") != pdFALSE) /* ����Ѿ�������wifi�� */
            {
                myprintf3((char *)(((char(*)[40])(*esp8266_cmd)) + 3)); //��ӡ��ȡwifi������
                if (xSemaphoreTake(xSemaphore, portMAX_DELAY / portTICK_PERIOD_MS) == pdTRUE)
                {
                    ucpsend_item = (USART_RX_BUF + 24); //��ȡip��ַ���׵�ַ
                    *(ucpsend_item + 15) = '\0';        //��ȡ�ַ���
                    sprintf((char *)USART_RX_BUF, "ipaddress:%s\r\n", (const char *)ucpsend_item);
                    *(USART_RX_BUF + 28) = '\0'; //��ȡ�ַ���
                    printf((const char *)USART_RX_BUF);
                    printf("esp8266 successful init norst\r\n");
                    isr_flag = pdTRUE; //�ڿ���������֮ǰ����Ҫȷ�����������ܹ������շ�
                    flag = pdTRUE;     //��Կ����,���ʱ����Կ���������,����Ҫ��������
                }
                else
                {
                    printf("sever open failed");
                }
            }
            else
            {
                printf("----------------------\r\n");
                printf((const char *)USART_RX_BUF); /* ��ӡ������Ϣ */
                printf("----------------------\r\n");
                while (1)
                    ;
            }
            ucpsend_item = USART_RX_BUF; //�ڽ�����һ֡���ݵ�ʱ�����ָ�������׵�ַ
        }
        else
        {
            printf("wait time error!!!\r\n");
        }
        /* The semaphore can now be used. Its handle is stored in the
        xSemahore variable.  Calling xSemaphoreTake() on the semaphore here
        will fail until the semaphore has first been given. */

        while (1)
        {
            /* A context switch will occur before the function returns if the priority being set is higher than the currently executing task.
                if we do this ,its actually do switch*/
            //vTaskPrioritySet(NULL, tskIDLE_PRIORITY + 6); //just make sever task a bit higher than idle task thraed task must lower than sever
            if (flag == pdTRUE) //���Ϊ��,����������
            {
                printf("sever have started!!!\r\n");
                while (1) //���������������������Ϊ
                {
                    if (xSemaphoreTake(xSemaphore, portMAX_DELAY / portTICK_PERIOD_MS) == pdTRUE) //�������Դ���3������
                    {
                        /* It is time to start sever. */
                        ucpsend_item = USART_RX_BUF;
                        //printf(":::%s", ucpsend_item);
                        request_id = reqid_get((char *)ucpsend_item); /* Attention:��������������ܵ��ĵ�һ֡����һ�������Կͻ��˵�������� */
                        //printf("%d\r\n", request_id);
                        switch (request_id)
                        {
                        case 0: /* ������û���� */
                            /* printf("can not find the command \r\n"); */
                            break;
                        case 1:
                        {
                            isr_flag = pdTRUE; //Ϊ�����ô��ڽ��յ����� ���������ȷ������Ϊ��
                            ucpsend_item = USART_RX_BUF;
                            Current_IDtask = IDArray;
                            Current_IDtask += (*ucpsend_item - '0'); /* ��¼��ʱ��Ҫ����Ŀͻ��� */
                            Current_IDtask->client_status = pdTRUE;  /* �ͻ��˽���ע�� */
                            sprintf((char *)ucpsend_item, "AT+CIPSEND=%c,%d\r\n", *ucpsend_item, strlen((const char *)esp82666_hello));
                            xSemaphoreTake(private_SEVER_thread_mutex, 200 / portTICK_PERIOD_MS); /* ��ȡ������ */
                            myprintf3_DMA((char *)ucpsend_item);
                            if (xSemaphoreTake(DMA_Transmit_semaphore, 100 / portTICK_PERIOD_MS) == pdTRUE)
                            {
                                if (xSemaphoreTake(xSemaphore, 100 / portTICK_PERIOD_MS) == pdTRUE) //�������Դ���3������
                                {
                                    if (strstr((const char *)ucpsend_item, (const char *)"OK") != NULL) //���Է���������
                                    {
                                        USART3_IRQ_Disable();                  //��������֮ǰ�ȰѴ���3�����ж�ʧ��
                                        myprintf3_DMA((char *)esp82666_hello); //��ͻ��˷��ͷ��������ṩ�ķ���

                                        if (xSemaphoreTake(DMA_Transmit_semaphore, 100 / portTICK_PERIOD_MS) == pdTRUE) //���������10ms����
                                        {
                                            vTaskDelay(200 / portTICK_PERIOD_MS);
                                            USART3_IRQ_Enable(); //����ʱ100ms,Ȼ��������֮ǰ�ȰѴ���3�����ж�ʹ��
                                        }
                                        else
                                        {
                                            printf("sever DMA_Transmit_semaphore error!!!\r\n");
                                        }
                                    }
                                    else
                                    {
                                        printf("accept OK error\r\n");
                                    }
                                    isr_flag = pdTRUE; //���ڿ����ٴλ�ȡ����
                                }
                            }
                            else
                            {
                                printf("accept client first error\r\n");
                            }
                            xSemaphoreGive(private_SEVER_thread_mutex); /* �黹������ */
                        }
                        break;
                        case 2:
                            /* its time to shutdown the thread */
                            taskENTER_CRITICAL(); //�����ٽ���
                            ucpsend_item = USART_RX_BUF;
                            Current_IDtask = IDArray;
                            Current_IDtask += (*ucpsend_item - '0');    /* ��¼��ʱ��Ҫ����Ŀͻ��� */
                            if (Current_IDtask->IDTask_Handler != NULL) /* �����������Ϊ0����ô��ɾ����������ƿ� */
                            {
                                vTaskDelete(Current_IDtask->IDTask_Handler);
                                Current_IDtask->IDTask_Handler = NULL;
                            }
                            else
                            {
                            }
                            Current_IDtask->client_status = pdFALSE; /* �ͻ��˽���ע�� */
                            Current_IDtask->thread_status = pdFALSE;
                            Current_IDtask->thread_ID = NULL;
                            memset(Current_IDtask->task, 0, sizeof(Current_IDtask->task));
                            isr_flag = pdTRUE; //Ϊ�����ô��ڽ��յ����� ���������ȷ������Ϊ��
                            taskEXIT_CRITICAL();
                            break;
                        case 3:
                            /* its time to shutdown the thread */
                            taskENTER_CRITICAL(); //�����ٽ���
                            ucpsend_item = USART_RX_BUF;
                            Current_IDtask = IDArray;
                            Current_IDtask += (*ucpsend_item - '0');    /* ��¼��ʱ��Ҫ����Ŀͻ��� */
                            if (Current_IDtask->IDTask_Handler != NULL) /* �����������Ϊ0����ô��ɾ����������ƿ� */
                            {
                                vTaskDelete(Current_IDtask->IDTask_Handler);
                                Current_IDtask->IDTask_Handler = NULL;
                            }
                            else
                            {
                            }
                            Current_IDtask->client_status = pdFALSE; /* �ͻ��˽���ע�� */
                            Current_IDtask->thread_status = pdFALSE;
                            Current_IDtask->thread_ID = NULL;
                            memset(Current_IDtask->task, 0, sizeof(Current_IDtask->task));
                            isr_flag = pdTRUE; //Ϊ�����ô��ڽ��յ����� ���������ȷ������Ϊ��
                            taskEXIT_CRITICAL();
                            break;
                            /* һ���Ƿ������ṩ�ķ���Ĭ����6�����񣬿��Լ������� */
                        case 4:
                        case 5:
                        case 6:
                        case 7:
                        case 8:
                        case 9:
                        {
                            printf((const char *)ucpsend_item);
                            ucpsend_item = USART_RX_BUF;
                            ucpsend_item += 7; /* ��ȡ�ͻ��˵�ID�� */
                            Current_IDtask = IDArray;
                            Current_IDtask += (*ucpsend_item - '0');     /* ��¼��ʱ��Ҫ����Ŀͻ��� */
                            if (Current_IDtask->client_status == pdTRUE) /* �ͻ���ע��ɹ���Ϊ�ͻ��˷����̣߳����ҳ�ʼ���߳�*/
                            {
                                if (Current_IDtask->thread_status == pdTRUE) /* ��ζ�ŷ������Ѿ�Ϊ�ͻ��˷������߳� */
                                {
                                    taskENTER_CRITICAL(); //�����ٽ���
                                    ucpsend_item = (unsigned char *)strstr((char *)ucpsend_item, ".");
                                    ucpsend_item += 1; /* ��ʱָ��ͻ��˵����� */
                                    memcpy(Current_IDtask->task, ucpsend_item, SERVICE_NAME_LENGTH);
                                    taskEXIT_CRITICAL(); //�˳��ٽ���
                                }
                                else
                                {

                                    Current_Thread_Name = (char(*)[10])Thread_Name;
                                    Current_Thread_Name += (*ucpsend_item - '0');
                                    xSemaphoreTake(private_SEVER_thread_mutex, 200 / portTICK_PERIOD_MS);       /* ��ȡ������ ��ֹDMA�ж�*/
                                    taskENTER_CRITICAL();                                                       //�����ٽ���
                                    if (xTaskCreate((TaskFunction_t)vThreadx_task,                              //������
                                                    (const char *)Current_Thread_Name,                          //��������
                                                    (uint16_t)THREADX_STK_SIZE,                                 //�����ջ��С
                                                    (void *)Current_IDtask,                                     //���ݸ��������Ĳ���
                                                    (UBaseType_t)tskIDLE_PRIORITY + 5,                          //�������ȼ�
                                                    (TaskHandle_t *)&Current_IDtask->IDTask_Handler) == pdPASS) //������
                                    {

                                        Current_IDtask->thread_status = pdTRUE;
                                        Current_IDtask->thread_ID = (*ucpsend_item - '0');
                                        ucpsend_item = (unsigned char *)strstr((char *)ucpsend_item, ".");
                                        ucpsend_item += 1; /* ��ʱָ��ͻ��˵����� */
                                        memcpy(Current_IDtask->task, ucpsend_item, SERVICE_NAME_LENGTH);
                                        taskEXIT_CRITICAL(); //�˳��ٽ���
                                        /* Ȼ����߿ͻ��ˣ�������ݾ�Ҫ���� */
                                        ucpsend_item = USART_RX_BUF;
                                        ucpsend_item += 7; /* ��ȡ�ͻ��˵�ID�� */
                                        sprintf((char *)USART_RX_BUF, "AT+CIPSEND=%c,%d\r\n", *ucpsend_item, strlen((const char *)esp82666_response));
                                        isr_flag = pdTRUE; //���ڿ����ٴλ�ȡ����
                                        ucpsend_item = USART_RX_BUF;
                                        myprintf3_DMA((char *)ucpsend_item);
                                        if (xSemaphoreTake(DMA_Transmit_semaphore, 100 / portTICK_PERIOD_MS) == pdTRUE)
                                        {
                                            if (xSemaphoreTake(xSemaphore, 100 / portTICK_PERIOD_MS) == pdTRUE) //�������Դ���3������
                                            {
                                                if (strstr((const char *)ucpsend_item, (const char *)"OK") != NULL) //���Է���������
                                                {
                                                    USART3_IRQ_Disable();                     //��������֮ǰ�ȰѴ���3�����ж�ʧ��
                                                    myprintf3_DMA((char *)esp82666_response); //��ͻ��˷��ͷ��������ṩ�ķ���

                                                    if (xSemaphoreTake(DMA_Transmit_semaphore, 100 / portTICK_PERIOD_MS) == pdTRUE) //���������10ms����
                                                    {
                                                        vTaskDelay(200 / portTICK_PERIOD_MS);
                                                        USART3_IRQ_Enable(); //����ʱ100ms,Ȼ��������֮ǰ�ȰѴ���3�����ж�ʹ��
                                                    }
                                                    else
                                                    {
                                                        printf("sever DMA_Transmit_semaphore error!!!\r\n");
                                                    }
                                                }
                                                else
                                                {
                                                    printf("accept OK error\r\n");
                                                }
                                                isr_flag = pdTRUE; //���ڿ����ٴλ�ȡ����
                                            }
                                            else
                                            {
                                                printf("UART locked\r\n");
                                            }
                                        }
                                        else
                                        {
                                            printf("DMA locked\r\n");
                                        }
                                    } /* ���ˣ��̳߳�ʼ����� */
                                    else
                                    {
                                        printf("thread create failure!!!\r\n");
                                    }

                                    xSemaphoreGive(private_SEVER_thread_mutex); /* �黹������ */
                                }
                            }
                            else
                            {
                                printf("client have not register\r\n");
                            }
                        }
                        break;
                        case 10:
                            //printf("can not find the command \r\n"); /* ���һ������������������� */
                            break;
                        default:
                            //printf("It is serious to assert\r\n");/* ��������ǲ����ܳ��ֵ� */
                            break;
                        }
                    }
                    else
                    {
                        //vTaskDelay(100 / portTICK_PERIOD_MS);
                    }
                    isr_flag = pdTRUE; //���ڿ����ٴλ�ȡ����
                }
            }
            else
            {
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }
        }
    }
    vTaskDelete(NULL);
}
static void BSP_UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);

    /* Enable the USARTy Interrupt */
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY - 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_USART3); //GPIOB10����ΪUSART3
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_USART3); //GPIOB11����ΪUSART3
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* Configure USARTy Tx as alternate function push-pull */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

    /* Configure USARTy */
    USART_Init(USART3, &USART_InitStructure);

    /* Enable USARTy Receive and Transmit interrupts */
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);

    /* Enable the USARTy */
    USART_Cmd(USART3, ENABLE);
}
static void BSP_TIM6_Init(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStruct;
    uint32_t PrescalerValue = 84 - 1;
    uint32_t Period = 1000 - 1;
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);         //ʹ��ʱ��
    TIM_TimeBaseInitStruct.TIM_CounterMode = TIM_CounterMode_Up; //����ģʽ
    TIM_TimeBaseInitStruct.TIM_Period = Period;                  //�Զ���װ��ֵ����Χ0~65535,
    TIM_TimeBaseInitStruct.TIM_Prescaler = PrescalerValue;       //��Ƶϵ������Ϊ1ʱ����ʱ��Ƶ�ʹ������ߵ���������ʱ�����ó���������42000000/42000
    TIM_TimeBaseInit(TIM6, &TIM_TimeBaseInitStruct);             //��ʼ��
    TIM_Cmd(TIM6, ENABLE);                                       //ʹ�ܶ�ʱ��6

    NVIC_InitStructure.NVIC_IRQChannel = TIM6_DAC_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 8;
    NVIC_Init(&NVIC_InitStructure);
    TIM_ITConfig(TIM6, TIM_IT_Update, ENABLE); //����ʱ��6�����ж�
    NVIC_EnableIRQ(TIM6_DAC_IRQn);             //ʹ��TIM6�ж�
}
static void BSP_DMA_Init(DMA_Stream_TypeDef *DMA_Streamx, u32 chx, u32 par, u32 mar, u16 ndtr)
{

    DMA_InitTypeDef DMA_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    if ((u32)DMA_Streamx > (u32)DMA2) //�õ���ǰstream������DMA2����DMA1
    {
        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE); //DMA2ʱ��ʹ��
    }
    else
    {
        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE); //DMA1ʱ��ʹ��
    }
    DMA_DeInit(DMA_Streamx);

    while (DMA_GetCmdStatus(DMA_Streamx) != DISABLE)
    {
    } //�ȴ�DMA������

    /* ���� DMA Stream */
    DMA_InitStructure.DMA_Channel = chx;                                    //ͨ��ѡ��
    DMA_InitStructure.DMA_PeripheralBaseAddr = par;                         //DMA�����ַ
    DMA_InitStructure.DMA_Memory0BaseAddr = mar;                            //DMA �洢��0��ַ
    DMA_InitStructure.DMA_DIR = DMA_DIR_MemoryToPeripheral;                 //�洢��������ģʽ
    DMA_InitStructure.DMA_BufferSize = ndtr;                                //���ݴ�����
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;        //���������ģʽ
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;                 //�洢������ģʽ
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; //�������ݳ���:8λ
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;         //�洢�����ݳ���:8λ
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;                           // ʹ����ͨģʽ
    DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;                   //�е����ȼ�
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;         //�洢��ͻ�����δ���
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single; //����ͻ�����δ���
    DMA_Init(DMA_Streamx, &DMA_InitStructure);                          //��ʼ��DMA Stream
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;                     // ʹ��
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;           // ��ռ���ȼ�
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;                  // �����ȼ�
    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Stream3_IRQn;
    NVIC_Init(&NVIC_InitStructure); // Ƕ�������жϿ�������ʼ��

    DMA_ITConfig(DMA1_Stream3, DMA_IT_TC, ENABLE);
}
static void BSP_SDMA(DMA_Stream_TypeDef *DMA_Streamx, u32 chx, u32 par, u32 mar, u16 ndtr)
{

    DMA_InitTypeDef DMA_InitStructure;
    /* ���� DMA Stream */
    DMA_InitStructure.DMA_Channel = chx;                                    //ͨ��ѡ��
    DMA_InitStructure.DMA_PeripheralBaseAddr = par;                         //DMA�����ַ
    DMA_InitStructure.DMA_Memory0BaseAddr = mar;                            //DMA �洢��0��ַ
    DMA_InitStructure.DMA_DIR = DMA_DIR_MemoryToPeripheral;                 //�洢��������ģʽ
    DMA_InitStructure.DMA_BufferSize = ndtr;                                //���ݴ�����
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;        //���������ģʽ
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;                 //�洢������ģʽ
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; //�������ݳ���:8λ
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;         //�洢�����ݳ���:8λ
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;                           // ʹ����ͨģʽ
    DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;                   //�е����ȼ�
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;         //�洢��ͻ�����δ���
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single; //����ͻ�����δ���
    DMA_Init(DMA_Streamx, &DMA_InitStructure);                          //��ʼ��DMA Stream
}
void USART3_IRQHandler(void)
{
    SEGGER_SYSVIEW_RecordEnterISR();
    if (USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)
    {
        USART_ClearITPendingBit(USART3, USART_IT_RXNE);
        /* Read one byte from the receive data register */
        USART_RX_BUF[USART_RX_CNT++] = USART_ReceiveData(USART3);
        if (USART_RX_CNT == USART_REC_LEN - 1) //��һλ��'\0'
        {
            USART_RX_CNT = 0;
            USART_RX_PRECNT = 0;
        }
    }
    if (USART_GetITStatus(USART3, USART_IT_TXE) != RESET) //�����ж�
    {
        if (*pc_SEVER_myprintf1 == pdFALSE)
        {
            USART_ITConfig(USART3, USART_IT_TXE, DISABLE); //���ݷ�����ɣ����ڷ����ж�ʧ��
        }
        else
        {
            USART3->DR = (u8)*pc_SEVER_myprintf1++; //���ݻ�û�з������
        }
    }
    SEGGER_SYSVIEW_RecordExitISR();
}
void TIM6_DAC_IRQHandler(void) //�����Ĵ���void TIM6_DAC_IRQn (void),TIM6_IRQHandler
{
    SEGGER_SYSVIEW_RecordEnterISR();
    if (TIM_GetITStatus(TIM6, TIM_IT_Update) == SET) //����ж�
    {
        if (isr_flag == pdTRUE)
        {

            if (USART_RX_CNT != 0)
            {
                if (USART_RX_CNT != USART_RX_PRECNT)
                {
                    USART_RX_PRECNT = USART_RX_CNT;
                }
                else
                {
                    USART_RX_BUF[USART_RX_CNT] = '\0';
                    USART_RX_PRECNT = 0;
                    USART_RX_CNT = 0;
                    isr_flag = pdFALSE;
                    xSemaphoreGiveFromISR(xSemaphore, NULL);
                }
            }
        }

        TIM_ClearITPendingBit(TIM6, TIM_IT_Update); //����жϱ�־λ
    }
    SEGGER_SYSVIEW_RecordExitISR();
}
void DMA1_Stream3_IRQHandler(void) // ����3 DMA�����жϴ�����
{
    SEGGER_SYSVIEW_RecordEnterISR();
    if (DMA_GetITStatus(DMA1_Stream3, DMA_IT_TCIF3) != RESET)
    {
        DMA_ClearFlag(DMA1_Stream3, DMA_FLAG_TCIF3);
        DMA_Cmd(DMA1_Stream3, DISABLE);                      // �ر�DMA
        DMA_SetCurrDataCounter(DMA1_Stream3, 0);             //����������Ϊ0
        xSemaphoreGiveFromISR(DMA_Transmit_semaphore, NULL); //ע��,��ʱDMA�����ȼ������������ȼ�������ʹ��freertos ISR API
    }
    SEGGER_SYSVIEW_RecordExitISR();
}
static void myprintf3(char *str)
{
    USART_ClearFlag(USART3, USART_FLAG_TC); //��ֹ����stm32��һ���ַ���ʧ������
    while (*str)
    {
        USART3->DR = (u8)*str;
        while ((USART3->SR & 0X40) == 0)
            ; //ѭ������,ֱ���������
        str++;
    }
}
static void myprintf3_DMA(char *str) //������Ҫע�⣬�ַ�����/0��β
{
    USART_ClearFlag(USART3, USART_FLAG_TC); //��ֹ����stm32��һ���ַ���ʧ������
    DMA_Cmd(DMA1_Stream3, DISABLE);         //�ر�DMA����
    while (DMA_GetCmdStatus(DMA1_Stream3) != DISABLE)
    {
    }                                                                               //ȷ��DMA���Ա�����
    BSP_SDMA(DMA1_Stream3, DMA_Channel_4, (u32)&USART3->DR, (u32)str, strlen(str)); //���ü򻯰��DMA
    DMA_Cmd(DMA1_Stream3, ENABLE);                                                  //����DMA����
}
static void myprintf3_DMA_cnt(char *str, int cnt) //������Ҫע�⣬�ַ�����/0��β
{
    USART_ClearFlag(USART3, USART_FLAG_TC); //��ֹ����stm32��һ���ַ���ʧ������
    DMA_Cmd(DMA1_Stream3, DISABLE);         //�ر�DMA����
    while (DMA_GetCmdStatus(DMA1_Stream3) != DISABLE)
    {
    }                                                                       //ȷ��DMA���Ա�����
    BSP_SDMA(DMA1_Stream3, DMA_Channel_4, (u32)&USART3->DR, (u32)str, cnt); //���ü򻯰��DMA
    DMA_Cmd(DMA1_Stream3, ENABLE);                                          //����DMA����
}
static void myprintf3_INT(char *str) //������Ҫע�⣬�ַ�����/0��β
{
    USART_ClearFlag(USART3, USART_FLAG_TC);       //��ֹ����stm32��һ���ַ���ʧ������
    pc_SEVER_myprintf1 = (u8 *)str;               //����printf��ָ����ַ���
    USART_ITConfig(USART3, USART_IT_TXE, ENABLE); //���������ж�
}
void USART3_IRQ_Enable(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}
void USART3_IRQ_Disable(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = DISABLE;
    NVIC_Init(&NVIC_InitStructure);
}
static unsigned char reqid_get(char *recv)
{
    char(*ptr)[40] = (((char(*)[40])(*esp8266_cmd)) + 7); //ָ���ʶ�������
    char count_id = 1;                                    /* 0������û���ҵ����� */
    while (*ptr)
    {
        if (strstr(recv, (char *)ptr) != NULL)
        {
            return count_id;
        }
        else
        {
            count_id++;
            ptr++;
        }
    }
    /* ������е���һ�����Ǿ���ζ�ŷ������޷�ʶ�����Կͻ��˵��������֪�ͻ��˷������޷�ʶ�� */
    return NULL;
}
/**
 * @Function Name: esp8266_task
 * @Description: ���ڳ�ʼ��������
 * @Param: pvParameters
 * @Return: ��
 * @param {void} *pvParameters
 */
static void esp8266_task(void *pvParameters)
{
    unsigned char *ucpsend_item = USART_RX_BUF;
    char(*ptr)[40] = (char(*)[40]) * (esp8266_cmd + 0);
    char count = 6;
    printf(":::enter esp8266_task\r\n");
    while (count--)
    {
        //printf("\r\n_%s\r\n", (char *)ptr);
        myprintf3((char *)ptr++);
        if (xSemaphoreTake(xSemaphore, portMAX_DELAY / portTICK_PERIOD_MS) == pdTRUE)
        {
            if (count == 4) //���at+rst�Ĳ���
            {
                USART3_IRQ_Disable();
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                USART3_IRQ_Enable();
                goto end;
            }
            if (count == 3) //��������ȵ�Ĳ���
            {
                isr_flag = pdTRUE; //��ʱ�Ѿ����յ�����esp8266�ĵ�һ֡���ݣ���ȷ�����ڿ����ٴλ�ȡ�����������ź����Ļ�ȡ��������
                                   //����
                while (1)
                {
                    //printf(":::%s:::\r\n", ucpsend_item);/*һֱ�ȴ�OK�ķ��أ�WIFIģ��᷵�غܶ��ַ�*/
                    if (xSemaphoreTake(xSemaphore, portMAX_DELAY / portTICK_PERIOD_MS) == pdTRUE)
                    {
                        //printf(":::%s:::\r\n", ucpsend_item);
                        //printf(":::\r\n");
                        if (strstr((const char *)ucpsend_item, "OK") != pdFALSE) //���õ����������Ƿ���OK����ͬ��wifiģ���в�ͬ�����
                        {
                            /* ���û�м���������У����ֻ��һ�֣�wifi�û�������������� */
                            //printf(":::%s\r\n", ucpsend_item);
                            goto end;
                        }
                        else if (strstr((const char *)ucpsend_item, "FAIL\r\n") != pdFALSE)
                        {
                            printf("Wifi�ȵ��˺Ż�������д����\r\n");
                        }
                        isr_flag = pdTRUE; //ȷ�����ڿ����ٴλ�ȡ����
                    }
                }
            }
            if (count == 2) //��Ի�ȡwifi�ȵ�Ĳ���
            {
                ucpsend_item = (USART_RX_BUF + 24); //��ȡip��ַ���׵�ַ
                *(ucpsend_item + 15) = '\0';        //��ȡ�ַ���
                sprintf((char *)USART_RX_BUF, "ipaddress:%s\r\n", (const char *)ucpsend_item);
                *(USART_RX_BUF + 28) = '\0'; //��ȡ�ַ���
                printf((const char *)USART_RX_BUF);
                ucpsend_item = USART_RX_BUF; //�ڽ�����һ֡���ݵ�ʱ�����ָ�������׵�ַ
                goto end;
            }
            /* It is time to execute. */

            /* We have finished our task.  Return to the top of the loop where
            we will block on the semaphore until it is time to execute
            again.  Note when using the semaphore for synchronisation with an
            ISR in this manner there is no need to 'give' the semaphore
            back. */
            //printf(":%s\r\n", ucpsend_item);
        end:
            isr_flag = pdTRUE; //���ڿ����ٴλ�ȡ����
        }
    }
    printf("esp8266 successful init rst\r\n");
    flag = pdTRUE; //��Կ����,���ʱ����Կ�����������������ʱ������esp8266�����ɾ��
    vTaskDelete(NULL);
    printf(":::exit esp8266_task\r\n");
}
/**
 * @Function Name: vThreadx_task
 * @Description: ��Ҫʵ�ֺ�C++���������͵ĺ�������
 * @Param: �߳���ز���
 * @Return: ��
 * @param {void} *pvParameters
 */
typedef struct resource_pool_queue
{
    char key[5];
    char value[10];
} reso_queue;
static void vThreadx_task(void *pvParameters)
{
    extern xQueueHandle public_queue;
    IDtask *thread = (IDtask *)pvParameters;
    char rx_count = 0;
    reso_queue cons_queue[10] = {0};
    char pre_send[25] = {0};
    char send[200] = {0};
    char *ptr = NULL; /* ָ������͵����� */
    send[0] = 0xA5;
    send[1] = 0xA5;
    taskENTER_CRITICAL(); //�����ٽ���
    printf("thread#%d start\r\n", thread->thread_ID);
    printf("%s\r\n", thread->task);
    taskEXIT_CRITICAL();
    while (1)
    {
        if (xQueuePeek(public_queue, &cons_queue[rx_count++], 200 / portTICK_PERIOD_MS) == pdTRUE)
        {
            vTaskDelay(1 / portTICK_PERIOD_MS); /* ����ط���ʱ��Ŀ�����������е������߶��ܹ���ȡ���������� */
            if (rx_count == 10)                 /* ��ʱ�����ݷ��͸���λ�� */
            {
                ptr = send + 2;                                   /* ÿ�β���ptr֮ǰ ��Ҫ��ptrָ��send+2 */
                memcpy(send + 2, cons_queue, sizeof(cons_queue)); /* ���ͻ�������ǰ�����ֽ���0xA5 0XA5��ͷ */
                ptr += sizeof(cons_queue);                        /* ָ�����һ�����ݵĺ�һ���ַ� */
                *ptr++ = '\r';
                *ptr = '\n';                                                                                      /* ��λ����\r\n��β */
                sprintf((char *)pre_send, "AT+CIPSEND=%d,%d\r\n", thread->thread_ID, sizeof(cons_queue) + 2 + 2); /* ���͵�������= ����+֡ͷ+֡β*/
                if (xSemaphoreTake(private_SEVER_thread_mutex, 500 / portTICK_PERIOD_MS) == pdTRUE)               /* ��ȡ������ */
                {
                    myprintf3_DMA((char *)pre_send); /* ����Ԥ�����������esp8266�ͻ������� */
                    if (xSemaphoreTake(DMA_Transmit_semaphore, 100 / portTICK_PERIOD_MS) == pdTRUE)
                    {
                        vTaskDelay(10 / portTICK_PERIOD_MS); /* ��esp8266ģ��׼����ʱ�䣬һ��Ҫ�� ��Ȼ���׳������ݶ�ʧ������ */
                        USART3_IRQ_Disable();                /* ���������ر� */
                        myprintf3_DMA_cnt((char *)send, sizeof(cons_queue) + 2 + 2);
                        if (xSemaphoreTake(DMA_Transmit_semaphore, 100 / portTICK_PERIOD_MS) == pdTRUE) //���������10ms����
                        {
                            USART3_IRQ_Enable(); /* �����ζ�ŷ��������Լ������� */
                        }
                        else
                        {
                            printf("sever DMA_Transmit_semaphore error!!!\r\n");
                        }
                    }
                    else
                    {
                        printf("threadx error");
                        vTaskDelay(50 / portTICK_PERIOD_MS);
                    }
                    xSemaphoreGive(private_SEVER_thread_mutex); /* �ȹ黹�ź�����Ȼ������ʱ */
                }
                else
                {
                    printf(":%d->there is sb qiangzhan\r\n", thread->thread_ID);
                    vTaskDelay(50);
                }
                rx_count = 0; //����
            }
        }
    }
}
/**
 * @Function Name: recycle_task
 * @Description: ��������
 * @Param: 
 * @Return: 
 * @param {void} *pvParameters
 */
static void recycle_task(void *pvParameters)
{
    char recv[20] = {0};
    while (1)
    {

        if (xQueueReceive(public_queue, recv, portMAX_DELAY) == pdTRUE)
        {
            //printf("done\r\n");
        }
        else
        {
        }
    }
}

static const char esp82666_hello[] =
    {"      |--------------------------------------------------------------|\r\n\
      |Hello  Client                                                 |\r\n\
      |Attention:Just send the severce number that you what to me    |\r\n\
      |Sever have supported the severce which is display below:      |\r\n\
      |1.start     2.bbb     3.ccc     4.ddd     5.eee     6.fff     |\r\n\
      |When you have send the order,What you need to do is to wait   |\r\n\
      |The data type is key-value class. and the frame has head cheek|\r\n\
      |Every data frame has '0XA5 0X5A' head cheak                   |\r\n\
      |The Server is still to be improved,  Hope your join           |\r\n\
      |--------------------------------------------------------------|\r\n\
      |   This Light Server Framework is build by Jasper from JXUST  |\r\n\
      |--------------------------------------------------------------|\r\n"};
static const char esp82666_response[] =
    {"      |--------------------------------------------------------------|\r\n\
      |                     Your Data is Comming                     |\r\n\
      |                     Just wait for a moment                   |\r\n\
      |--------------------------------------------------------------|\r\n"};
static const char Thread_Name[][10] = {
    "Thread #0",
    "Thread #1",
    "Thread #2",
    "Thread #3",
    "Thread #4",
};
/* 
    �����esp8266������˳���ǰ��շ���������˳��ģ����ݲ�ͬ�ķ�������ʼ���������ü���
    ���ڷ��ͳ�ȥ�ĵ�һ���ַ������ϵģ��������������ѽ����
 */
static const char esp8266_cmd[][40] = {
    /* ��������ʼ������ */
    "AT+CWMODE=1\r\n",
    "AT+RST\r\n",
    "AT+CWJAP=\"TP-LINK_206\",\"123456789\"\r\n",
    "AT+CIFSR\r\n",
    "AT+CIPMUX=1\r\n",
    "AT+CIPSERVER=1,1001\r\n",
    /* ��ѯ�ȵ� */
    "AT+CWJAP?\r\n",
    /* �ͻ���������˳�cmd */
    ",CONNECT\r\n", /* �ͻ��˵�һ������ */
    ",CONNECT FAIL\r\n",
    ",CLOSED\r\n",
    /* 
        �������ܹ��ṩ�ķ���,����������������д
     */
    "1.start",
    "2.bbb",
    "3.ccc",
    "4.ddd",
    "5.eee",
    "6.fff",
    "\0\0\0\0\0\0\0\0\0\0", /* ����ѭ��������� */
};