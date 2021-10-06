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
/* 添加该服务器组件所需要的硬件及其驱动，与具体的硬件平台有关 */
static void BSP_UART_Init(void);
static void BSP_TIM6_Init(void);
static void BSP_DMA_Init(DMA_Stream_TypeDef *DMA_Streamx, u32 chx, u32 par, u32 mar, u16 ndtr);
static void BSP_SDMA(DMA_Stream_TypeDef *DMA_Streamx, u32 chx, u32 par, u32 mar, u16 ndtr);
/* 服务器组件中与硬件平台无关的函数 */
static void myprintf3(char *str);
static void myprintf3_INT(char *str); //但是需要注意，字符串以/0结尾
static void myprintf3_DMA(char *str);
static void myprintf3_DMA_cnt(char *str, int cnt); //但是需要注意，字符串以/0结尾
void USART3_IRQ_Enable(void);
void USART3_IRQ_Disable(void);
static void vThreadx_task(void *pvParameters);
static void esp8266_task(void *pvParameters);
static void recycle_task(void *pvParameters);
static unsigned char reqid_get(char *recv); //获取请求id号 请求id来自于客户端
/* 服务器组件中的信号量 */
static SemaphoreHandle_t xSemaphore = NULL;
static xSemaphoreHandle private_SEVER_thread_mutex = 0;
static xSemaphoreHandle DMA_Transmit_semaphore = pdFALSE;
/* 服务器用到的字符串 */
static const char esp8266_cmd[][40];
static const char esp82666_hello[];
static const char esp82666_response[];
static const char Thread_Name[][10];
/* 线程控制块 */
typedef struct idtask
{
    TaskHandle_t IDTask_Handler;
    unsigned char thread_status;
    unsigned char client_status;
    unsigned char thread_ID;
    char task[10];
} IDtask;
/* 相关宏定义 */
#define TICKS_TO_WAIT 10
#define USART_REC_LEN 200     //定义最大接收字节数 200
#define SERVICE_NAME_LENGTH 5 //服务器提供服务的名称的数目是5
#define SEVER_TASK_PRIO 6
//任务优先级
#define THREADX_TASK_PRIO (SEVER_TASK_PRIO - 1)
//任务堆栈大小
#define THREADX_STK_SIZE 200
//任务优先级
#define esp8266_TASK_PRIO (SEVER_TASK_PRIO - 1)
//任务堆栈大小
#define esp8266_STK_SIZE 128
//任务优先级
#define RECYCLE_TASK_PRIO (SEVER_TASK_PRIO - 2)
//任务堆栈大小
#define RECYCLE_STK_SIZE 128
//任务句柄
TaskHandle_t esp8266_Task_Handler;
TaskHandle_t recycle_task_Handler;
/* 服务器组件中相关全局变量 */
static u8 USART_RX_BUF[USART_REC_LEN] = {0}; //接收缓冲,最大USART_REC_LEN个字节.
static u16 USART_RX_CNT = 0;                 //接收状态标记
static u16 USART_RX_PRECNT = 0;              //接收状态标记
static u8 flag = pdFALSE;                    //服务器的状态标志，默认服务器是没有准备好的
static u8 isr_flag = pdTRUE;
static unsigned char *pc_SEVER_myprintf1 = NULL;
void Sever_task(void *pvParameters)
{
    unsigned char *ucpsend_item = USART_RX_BUF;
    char send[20] = {"hello world!!!\r\n"};
    unsigned char request_id = NULL;
    IDtask IDArray[5] = {0};       /* 记录五个线程的状态 */
    IDtask *Current_IDtask = NULL; /* 指向当前任务 */
    char(*Current_Thread_Name)[10] = NULL;
    /* 初始化硬件 */
    BSP_UART_Init();
    BSP_TIM6_Init();
    BSP_DMA_Init(DMA1_Stream3, DMA_Channel_4, (u32)&USART3->DR, (u32)send, (u16)strlen(send)); //配置简化版的DMA
    USART_DMACmd(USART3, USART_DMAReq_Tx, ENABLE);                                             //使能串口1的DMA发送
    /*创建信号量 */
    xSemaphore = xSemaphoreCreateBinary();
    DMA_Transmit_semaphore = xSemaphoreCreateBinary();
    private_SEVER_thread_mutex = xSemaphoreCreateMutex();
    taskENTER_CRITICAL();                               //进入临界区
    xTaskCreate((TaskFunction_t)recycle_task,           //任务函数
                (const char *)"recycle",                //任务名称
                (uint16_t)RECYCLE_STK_SIZE,             //任务堆栈大小
                (void *)IDArray,                        //传递给任务函数的参数
                (UBaseType_t)RECYCLE_TASK_PRIO,         //任务优先级
                (TaskHandle_t *)&recycle_task_Handler); //任务句柄
    taskEXIT_CRITICAL();                                //退出临界区
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
    else /* 信号量全部创建成功则开始尝试连接sever */
    {
        printf("try to acquire sever\r\n");
        myprintf3((char *)(((char(*)[40])(*esp8266_cmd)) + 6)); //查看当前esp8266是否已经连接上wifi
        //printf((const char *)(((char(*)[40])(*esp8266_cmd)) + 6)); //查看当前esp8266是否已经连接上wifi
        if (xSemaphoreTake(xSemaphore, portMAX_DELAY / portTICK_PERIOD_MS) == pdTRUE)
        {
            isr_flag = pdTRUE;
            //printf((const char *)USART_RX_BUF);                         //确保串口可以获取数据
            if (strstr((const char *)USART_RX_BUF, "No AP") != pdFALSE) //如果检索到，就意味这个时候还没有连接上热点
            {
                taskENTER_CRITICAL();                               //进入临界区
                xTaskCreate((TaskFunction_t)esp8266_task,           //任务函数
                            (const char *)"esp8266",                //任务名称
                            (uint16_t)esp8266_STK_SIZE,             //任务堆栈大小
                            (void *)NULL,                           //传递给任务函数的参数
                            (UBaseType_t)esp8266_TASK_PRIO,         //任务优先级
                            (TaskHandle_t *)&esp8266_Task_Handler); //任务句柄
                taskEXIT_CRITICAL();                                //退出临界区
            }
            else if (strstr((const char *)USART_RX_BUF, "+CWJAP:") != pdFALSE) /* 如果已经连接上wifi， */
            {
                myprintf3((char *)(((char(*)[40])(*esp8266_cmd)) + 3)); //打印获取wifi的命令
                if (xSemaphoreTake(xSemaphore, portMAX_DELAY / portTICK_PERIOD_MS) == pdTRUE)
                {
                    ucpsend_item = (USART_RX_BUF + 24); //获取ip地址的首地址
                    *(ucpsend_item + 15) = '\0';        //截取字符串
                    sprintf((char *)USART_RX_BUF, "ipaddress:%s\r\n", (const char *)ucpsend_item);
                    *(USART_RX_BUF + 28) = '\0'; //截取字符串
                    printf((const char *)USART_RX_BUF);
                    printf("esp8266 successful init norst\r\n");
                    isr_flag = pdTRUE; //在开启服务器之前，先要确保串口数据能够正常收发
                    flag = pdTRUE;     //锁钥定理,这个时候可以开启服务器,不需要开启任务
                }
                else
                {
                    printf("sever open failed");
                }
            }
            else
            {
                printf("----------------------\r\n");
                printf((const char *)USART_RX_BUF); /* 打印错误信息 */
                printf("----------------------\r\n");
                while (1)
                    ;
            }
            ucpsend_item = USART_RX_BUF; //在接受下一帧数据的时候可以指向数组首地址
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
            if (flag == pdTRUE) //如果为真,则开启服务器
            {
                printf("sever have started!!!\r\n");
                while (1) //开启服务器后服务器的行为
                {
                    if (xSemaphoreTake(xSemaphore, portMAX_DELAY / portTICK_PERIOD_MS) == pdTRUE) //接受来自串口3的数据
                    {
                        /* It is time to start sever. */
                        ucpsend_item = USART_RX_BUF;
                        //printf(":::%s", ucpsend_item);
                        request_id = reqid_get((char *)ucpsend_item); /* Attention:服务器开启后接受到的第一帧数据一定是来自客户端的请求服务 */
                        //printf("%d\r\n", request_id);
                        switch (request_id)
                        {
                        case 0: /* 这个语句没有用 */
                            /* printf("can not find the command \r\n"); */
                            break;
                        case 1:
                        {
                            isr_flag = pdTRUE; //为了能让串口接收到数据 ，这里必须确保变量为真
                            ucpsend_item = USART_RX_BUF;
                            Current_IDtask = IDArray;
                            Current_IDtask += (*ucpsend_item - '0'); /* 记录此时需要服务的客户端 */
                            Current_IDtask->client_status = pdTRUE;  /* 客户端进行注册 */
                            sprintf((char *)ucpsend_item, "AT+CIPSEND=%c,%d\r\n", *ucpsend_item, strlen((const char *)esp82666_hello));
                            xSemaphoreTake(private_SEVER_thread_mutex, 200 / portTICK_PERIOD_MS); /* 获取互斥量 */
                            myprintf3_DMA((char *)ucpsend_item);
                            if (xSemaphoreTake(DMA_Transmit_semaphore, 100 / portTICK_PERIOD_MS) == pdTRUE)
                            {
                                if (xSemaphoreTake(xSemaphore, 100 / portTICK_PERIOD_MS) == pdTRUE) //接受来自串口3的数据
                                {
                                    if (strstr((const char *)ucpsend_item, (const char *)"OK") != NULL) //可以发送数据了
                                    {
                                        USART3_IRQ_Disable();                  //发送数据之前先把串口3接受中断失能
                                        myprintf3_DMA((char *)esp82666_hello); //向客户端发送服务器可提供的服务

                                        if (xSemaphoreTake(DMA_Transmit_semaphore, 100 / portTICK_PERIOD_MS) == pdTRUE) //正常情况下10ms足以
                                        {
                                            vTaskDelay(200 / portTICK_PERIOD_MS);
                                            USART3_IRQ_Enable(); //先延时100ms,然后发送数据之前先把串口3接受中断使能
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
                                    isr_flag = pdTRUE; //串口可以再次获取数据
                                }
                            }
                            else
                            {
                                printf("accept client first error\r\n");
                            }
                            xSemaphoreGive(private_SEVER_thread_mutex); /* 归还互斥量 */
                        }
                        break;
                        case 2:
                            /* its time to shutdown the thread */
                            taskENTER_CRITICAL(); //进入临界区
                            ucpsend_item = USART_RX_BUF;
                            Current_IDtask = IDArray;
                            Current_IDtask += (*ucpsend_item - '0');    /* 记录此时需要服务的客户端 */
                            if (Current_IDtask->IDTask_Handler != NULL) /* 如果任务句柄不为0，那么就删除该任务控制块 */
                            {
                                vTaskDelete(Current_IDtask->IDTask_Handler);
                                Current_IDtask->IDTask_Handler = NULL;
                            }
                            else
                            {
                            }
                            Current_IDtask->client_status = pdFALSE; /* 客户端进行注册 */
                            Current_IDtask->thread_status = pdFALSE;
                            Current_IDtask->thread_ID = NULL;
                            memset(Current_IDtask->task, 0, sizeof(Current_IDtask->task));
                            isr_flag = pdTRUE; //为了能让串口接收到数据 ，这里必须确保变量为真
                            taskEXIT_CRITICAL();
                            break;
                        case 3:
                            /* its time to shutdown the thread */
                            taskENTER_CRITICAL(); //进入临界区
                            ucpsend_item = USART_RX_BUF;
                            Current_IDtask = IDArray;
                            Current_IDtask += (*ucpsend_item - '0');    /* 记录此时需要服务的客户端 */
                            if (Current_IDtask->IDTask_Handler != NULL) /* 如果任务句柄不为0，那么就删除该任务控制块 */
                            {
                                vTaskDelete(Current_IDtask->IDTask_Handler);
                                Current_IDtask->IDTask_Handler = NULL;
                            }
                            else
                            {
                            }
                            Current_IDtask->client_status = pdFALSE; /* 客户端进行注册 */
                            Current_IDtask->thread_status = pdFALSE;
                            Current_IDtask->thread_ID = NULL;
                            memset(Current_IDtask->task, 0, sizeof(Current_IDtask->task));
                            isr_flag = pdTRUE; //为了能让串口接收到数据 ，这里必须确保变量为真
                            taskEXIT_CRITICAL();
                            break;
                            /* 一下是服务器提供的服务，默认是6个服务，可以继续增加 */
                        case 4:
                        case 5:
                        case 6:
                        case 7:
                        case 8:
                        case 9:
                        {
                            printf((const char *)ucpsend_item);
                            ucpsend_item = USART_RX_BUF;
                            ucpsend_item += 7; /* 获取客户端的ID号 */
                            Current_IDtask = IDArray;
                            Current_IDtask += (*ucpsend_item - '0');     /* 记录此时需要服务的客户端 */
                            if (Current_IDtask->client_status == pdTRUE) /* 客户端注册成功，为客户端分配线程，并且初始化线程*/
                            {
                                if (Current_IDtask->thread_status == pdTRUE) /* 意味着服务器已经为客户端分配了线程 */
                                {
                                    taskENTER_CRITICAL(); //进入临界区
                                    ucpsend_item = (unsigned char *)strstr((char *)ucpsend_item, ".");
                                    ucpsend_item += 1; /* 此时指向客户端的请求 */
                                    memcpy(Current_IDtask->task, ucpsend_item, SERVICE_NAME_LENGTH);
                                    taskEXIT_CRITICAL(); //退出临界区
                                }
                                else
                                {

                                    Current_Thread_Name = (char(*)[10])Thread_Name;
                                    Current_Thread_Name += (*ucpsend_item - '0');
                                    xSemaphoreTake(private_SEVER_thread_mutex, 200 / portTICK_PERIOD_MS);       /* 获取互斥量 防止DMA中断*/
                                    taskENTER_CRITICAL();                                                       //进入临界区
                                    if (xTaskCreate((TaskFunction_t)vThreadx_task,                              //任务函数
                                                    (const char *)Current_Thread_Name,                          //任务名称
                                                    (uint16_t)THREADX_STK_SIZE,                                 //任务堆栈大小
                                                    (void *)Current_IDtask,                                     //传递给任务函数的参数
                                                    (UBaseType_t)tskIDLE_PRIORITY + 5,                          //任务优先级
                                                    (TaskHandle_t *)&Current_IDtask->IDTask_Handler) == pdPASS) //任务句柄
                                    {

                                        Current_IDtask->thread_status = pdTRUE;
                                        Current_IDtask->thread_ID = (*ucpsend_item - '0');
                                        ucpsend_item = (unsigned char *)strstr((char *)ucpsend_item, ".");
                                        ucpsend_item += 1; /* 此时指向客户端的请求 */
                                        memcpy(Current_IDtask->task, ucpsend_item, SERVICE_NAME_LENGTH);
                                        taskEXIT_CRITICAL(); //退出临界区
                                        /* 然后告诉客户端，你的数据就要来了 */
                                        ucpsend_item = USART_RX_BUF;
                                        ucpsend_item += 7; /* 获取客户端的ID号 */
                                        sprintf((char *)USART_RX_BUF, "AT+CIPSEND=%c,%d\r\n", *ucpsend_item, strlen((const char *)esp82666_response));
                                        isr_flag = pdTRUE; //串口可以再次获取数据
                                        ucpsend_item = USART_RX_BUF;
                                        myprintf3_DMA((char *)ucpsend_item);
                                        if (xSemaphoreTake(DMA_Transmit_semaphore, 100 / portTICK_PERIOD_MS) == pdTRUE)
                                        {
                                            if (xSemaphoreTake(xSemaphore, 100 / portTICK_PERIOD_MS) == pdTRUE) //接受来自串口3的数据
                                            {
                                                if (strstr((const char *)ucpsend_item, (const char *)"OK") != NULL) //可以发送数据了
                                                {
                                                    USART3_IRQ_Disable();                     //发送数据之前先把串口3接受中断失能
                                                    myprintf3_DMA((char *)esp82666_response); //向客户端发送服务器可提供的服务

                                                    if (xSemaphoreTake(DMA_Transmit_semaphore, 100 / portTICK_PERIOD_MS) == pdTRUE) //正常情况下10ms足以
                                                    {
                                                        vTaskDelay(200 / portTICK_PERIOD_MS);
                                                        USART3_IRQ_Enable(); //先延时100ms,然后发送数据之前先把串口3接受中断使能
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
                                                isr_flag = pdTRUE; //串口可以再次获取数据
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
                                    } /* 至此，线程初始化完成 */
                                    else
                                    {
                                        printf("thread create failure!!!\r\n");
                                    }

                                    xSemaphoreGive(private_SEVER_thread_mutex); /* 归还互斥量 */
                                }
                            }
                            else
                            {
                                printf("client have not register\r\n");
                            }
                        }
                        break;
                        case 10:
                            //printf("can not find the command \r\n"); /* 最后一种情况触发是正常现象 */
                            break;
                        default:
                            //printf("It is serious to assert\r\n");/* 这种情况是不可能出现的 */
                            break;
                        }
                    }
                    else
                    {
                        //vTaskDelay(100 / portTICK_PERIOD_MS);
                    }
                    isr_flag = pdTRUE; //串口可以再次获取数据
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

    GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_USART3); //GPIOB10复用为USART3
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_USART3); //GPIOB11复用为USART3
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
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);         //使能时钟
    TIM_TimeBaseInitStruct.TIM_CounterMode = TIM_CounterMode_Up; //计数模式
    TIM_TimeBaseInitStruct.TIM_Period = Period;                  //自动重装载值，范围0~65535,
    TIM_TimeBaseInitStruct.TIM_Prescaler = PrescalerValue;       //分频系数，不为1时，定时器频率挂载总线的两倍（看时钟树得出），所以42000000/42000
    TIM_TimeBaseInit(TIM6, &TIM_TimeBaseInitStruct);             //初始化
    TIM_Cmd(TIM6, ENABLE);                                       //使能定时器6

    NVIC_InitStructure.NVIC_IRQChannel = TIM6_DAC_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 8;
    NVIC_Init(&NVIC_InitStructure);
    TIM_ITConfig(TIM6, TIM_IT_Update, ENABLE); //允许定时器6更新中断
    NVIC_EnableIRQ(TIM6_DAC_IRQn);             //使能TIM6中断
}
static void BSP_DMA_Init(DMA_Stream_TypeDef *DMA_Streamx, u32 chx, u32 par, u32 mar, u16 ndtr)
{

    DMA_InitTypeDef DMA_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    if ((u32)DMA_Streamx > (u32)DMA2) //得到当前stream是属于DMA2还是DMA1
    {
        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE); //DMA2时钟使能
    }
    else
    {
        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE); //DMA1时钟使能
    }
    DMA_DeInit(DMA_Streamx);

    while (DMA_GetCmdStatus(DMA_Streamx) != DISABLE)
    {
    } //等待DMA可配置

    /* 配置 DMA Stream */
    DMA_InitStructure.DMA_Channel = chx;                                    //通道选择
    DMA_InitStructure.DMA_PeripheralBaseAddr = par;                         //DMA外设地址
    DMA_InitStructure.DMA_Memory0BaseAddr = mar;                            //DMA 存储器0地址
    DMA_InitStructure.DMA_DIR = DMA_DIR_MemoryToPeripheral;                 //存储器到外设模式
    DMA_InitStructure.DMA_BufferSize = ndtr;                                //数据传输量
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;        //外设非增量模式
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;                 //存储器增量模式
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; //外设数据长度:8位
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;         //存储器数据长度:8位
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;                           // 使用普通模式
    DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;                   //中等优先级
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;         //存储器突发单次传输
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single; //外设突发单次传输
    DMA_Init(DMA_Streamx, &DMA_InitStructure);                          //初始化DMA Stream
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;                     // 使能
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;           // 抢占优先级
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;                  // 子优先级
    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Stream3_IRQn;
    NVIC_Init(&NVIC_InitStructure); // 嵌套向量中断控制器初始化

    DMA_ITConfig(DMA1_Stream3, DMA_IT_TC, ENABLE);
}
static void BSP_SDMA(DMA_Stream_TypeDef *DMA_Streamx, u32 chx, u32 par, u32 mar, u16 ndtr)
{

    DMA_InitTypeDef DMA_InitStructure;
    /* 配置 DMA Stream */
    DMA_InitStructure.DMA_Channel = chx;                                    //通道选择
    DMA_InitStructure.DMA_PeripheralBaseAddr = par;                         //DMA外设地址
    DMA_InitStructure.DMA_Memory0BaseAddr = mar;                            //DMA 存储器0地址
    DMA_InitStructure.DMA_DIR = DMA_DIR_MemoryToPeripheral;                 //存储器到外设模式
    DMA_InitStructure.DMA_BufferSize = ndtr;                                //数据传输量
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;        //外设非增量模式
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;                 //存储器增量模式
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; //外设数据长度:8位
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;         //存储器数据长度:8位
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;                           // 使用普通模式
    DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;                   //中等优先级
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;         //存储器突发单次传输
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single; //外设突发单次传输
    DMA_Init(DMA_Streamx, &DMA_InitStructure);                          //初始化DMA Stream
}
void USART3_IRQHandler(void)
{
    SEGGER_SYSVIEW_RecordEnterISR();
    if (USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)
    {
        USART_ClearITPendingBit(USART3, USART_IT_RXNE);
        /* Read one byte from the receive data register */
        USART_RX_BUF[USART_RX_CNT++] = USART_ReceiveData(USART3);
        if (USART_RX_CNT == USART_REC_LEN - 1) //留一位给'\0'
        {
            USART_RX_CNT = 0;
            USART_RX_PRECNT = 0;
        }
    }
    if (USART_GetITStatus(USART3, USART_IT_TXE) != RESET) //发送中断
    {
        if (*pc_SEVER_myprintf1 == pdFALSE)
        {
            USART_ITConfig(USART3, USART_IT_TXE, DISABLE); //数据发送完成，串口发送中断失能
        }
        else
        {
            USART3->DR = (u8)*pc_SEVER_myprintf1++; //数据还没有发送完成
        }
    }
    SEGGER_SYSVIEW_RecordExitISR();
}
void TIM6_DAC_IRQHandler(void) //曾经的错误：void TIM6_DAC_IRQn (void),TIM6_IRQHandler
{
    SEGGER_SYSVIEW_RecordEnterISR();
    if (TIM_GetITStatus(TIM6, TIM_IT_Update) == SET) //溢出中断
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

        TIM_ClearITPendingBit(TIM6, TIM_IT_Update); //清除中断标志位
    }
    SEGGER_SYSVIEW_RecordExitISR();
}
void DMA1_Stream3_IRQHandler(void) // 串口3 DMA发送中断处理函数
{
    SEGGER_SYSVIEW_RecordEnterISR();
    if (DMA_GetITStatus(DMA1_Stream3, DMA_IT_TCIF3) != RESET)
    {
        DMA_ClearFlag(DMA1_Stream3, DMA_FLAG_TCIF3);
        DMA_Cmd(DMA1_Stream3, DISABLE);                      // 关闭DMA
        DMA_SetCurrDataCounter(DMA1_Stream3, 0);             //传输数据量为0
        xSemaphoreGiveFromISR(DMA_Transmit_semaphore, NULL); //注意,此时DMA的优先级大于任务优先级，不能使用freertos ISR API
    }
    SEGGER_SYSVIEW_RecordExitISR();
}
static void myprintf3(char *str)
{
    USART_ClearFlag(USART3, USART_FLAG_TC); //防止出现stm32第一个字符丢失的现象
    while (*str)
    {
        USART3->DR = (u8)*str;
        while ((USART3->SR & 0X40) == 0)
            ; //循环发送,直到发送完毕
        str++;
    }
}
static void myprintf3_DMA(char *str) //但是需要注意，字符串以/0结尾
{
    USART_ClearFlag(USART3, USART_FLAG_TC); //防止出现stm32第一个字符丢失的现象
    DMA_Cmd(DMA1_Stream3, DISABLE);         //关闭DMA传输
    while (DMA_GetCmdStatus(DMA1_Stream3) != DISABLE)
    {
    }                                                                               //确保DMA可以被设置
    BSP_SDMA(DMA1_Stream3, DMA_Channel_4, (u32)&USART3->DR, (u32)str, strlen(str)); //配置简化版的DMA
    DMA_Cmd(DMA1_Stream3, ENABLE);                                                  //开启DMA传输
}
static void myprintf3_DMA_cnt(char *str, int cnt) //但是需要注意，字符串以/0结尾
{
    USART_ClearFlag(USART3, USART_FLAG_TC); //防止出现stm32第一个字符丢失的现象
    DMA_Cmd(DMA1_Stream3, DISABLE);         //关闭DMA传输
    while (DMA_GetCmdStatus(DMA1_Stream3) != DISABLE)
    {
    }                                                                       //确保DMA可以被设置
    BSP_SDMA(DMA1_Stream3, DMA_Channel_4, (u32)&USART3->DR, (u32)str, cnt); //配置简化版的DMA
    DMA_Cmd(DMA1_Stream3, ENABLE);                                          //开启DMA传输
}
static void myprintf3_INT(char *str) //但是需要注意，字符串以/0结尾
{
    USART_ClearFlag(USART3, USART_FLAG_TC);       //防止出现stm32第一个字符丢失的现象
    pc_SEVER_myprintf1 = (u8 *)str;               //更新printf所指向的字符串
    USART_ITConfig(USART3, USART_IT_TXE, ENABLE); //开启发送中断
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
    char(*ptr)[40] = (((char(*)[40])(*esp8266_cmd)) + 7); //指向待识别的命令
    char count_id = 1;                                    /* 0代表着没有找到命令 */
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
    /* 如果运行到这一步，那就意味着服务器无法识别来自客户端的命令，将告知客户端服务器无法识别 */
    return NULL;
}
/**
 * @Function Name: esp8266_task
 * @Description: 用于初始化服务器
 * @Param: pvParameters
 * @Return: 无
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
            if (count == 4) //针对at+rst的操作
            {
                USART3_IRQ_Disable();
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                USART3_IRQ_Enable();
                goto end;
            }
            if (count == 3) //针对连接热点的操作
            {
                isr_flag = pdTRUE; //此时已经接收到来自esp8266的第一帧数据，先确保串口可以再次获取数据以至于信号量的获取可以正常
                                   //进行
                while (1)
                {
                    //printf(":::%s:::\r\n", ucpsend_item);/*一直等待OK的返回，WIFI模块会返回很多字符*/
                    if (xSemaphoreTake(xSemaphore, portMAX_DELAY / portTICK_PERIOD_MS) == pdTRUE)
                    {
                        //printf(":::%s:::\r\n", ucpsend_item);
                        //printf(":::\r\n");
                        if (strstr((const char *)ucpsend_item, "OK") != pdFALSE) //检查得到的数据中是否有OK，不同的wifi模块有不同的情况
                        {
                            /* 如果没有检查进入语句中，情况只有一种，wifi用户名或者密码错误 */
                            //printf(":::%s\r\n", ucpsend_item);
                            goto end;
                        }
                        else if (strstr((const char *)ucpsend_item, "FAIL\r\n") != pdFALSE)
                        {
                            printf("Wifi热点账号或密码填写错误\r\n");
                        }
                        isr_flag = pdTRUE; //确保串口可以再次获取数据
                    }
                }
            }
            if (count == 2) //针对获取wifi热点的操作
            {
                ucpsend_item = (USART_RX_BUF + 24); //获取ip地址的首地址
                *(ucpsend_item + 15) = '\0';        //截取字符串
                sprintf((char *)USART_RX_BUF, "ipaddress:%s\r\n", (const char *)ucpsend_item);
                *(USART_RX_BUF + 28) = '\0'; //截取字符串
                printf((const char *)USART_RX_BUF);
                ucpsend_item = USART_RX_BUF; //在接受下一帧数据的时候可以指向数组首地址
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
            isr_flag = pdTRUE; //串口可以再次获取数据
        }
    }
    printf("esp8266 successful init rst\r\n");
    flag = pdTRUE; //锁钥定理,这个时候可以开启服务器，并把临时创建的esp8266任务给删除
    vTaskDelete(NULL);
    printf(":::exit esp8266_task\r\n");
}
/**
 * @Function Name: vThreadx_task
 * @Description: 需要实现和C++特性相类型的函数重载
 * @Param: 线程相关参数
 * @Return: 无
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
    char *ptr = NULL; /* 指向待发送的数据 */
    send[0] = 0xA5;
    send[1] = 0xA5;
    taskENTER_CRITICAL(); //进入临界区
    printf("thread#%d start\r\n", thread->thread_ID);
    printf("%s\r\n", thread->task);
    taskEXIT_CRITICAL();
    while (1)
    {
        if (xQueuePeek(public_queue, &cons_queue[rx_count++], 200 / portTICK_PERIOD_MS) == pdTRUE)
        {
            vTaskDelay(1 / portTICK_PERIOD_MS); /* 这个地方延时的目的在于让所有的消费者都能够获取到生产资料 */
            if (rx_count == 10)                 /* 此时将数据发送给上位机 */
            {
                ptr = send + 2;                                   /* 每次操作ptr之前 先要让ptr指向send+2 */
                memcpy(send + 2, cons_queue, sizeof(cons_queue)); /* 发送缓冲区的前两个字节以0xA5 0XA5开头 */
                ptr += sizeof(cons_queue);                        /* 指向最后一个数据的后一个字符 */
                *ptr++ = '\r';
                *ptr = '\n';                                                                                      /* 上位机以\r\n结尾 */
                sprintf((char *)pre_send, "AT+CIPSEND=%d,%d\r\n", thread->thread_ID, sizeof(cons_queue) + 2 + 2); /* 发送的数据量= 数据+帧头+帧尾*/
                if (xSemaphoreTake(private_SEVER_thread_mutex, 500 / portTICK_PERIOD_MS) == pdTRUE)               /* 获取互斥量 */
                {
                    myprintf3_DMA((char *)pre_send); /* 发送预数据区，针对esp8266客户端数据 */
                    if (xSemaphoreTake(DMA_Transmit_semaphore, 100 / portTICK_PERIOD_MS) == pdTRUE)
                    {
                        vTaskDelay(10 / portTICK_PERIOD_MS); /* 给esp8266模块准备的时间，一定要有 不然容易出现数据丢失的现象 */
                        USART3_IRQ_Disable();                /* 将服务器关闭 */
                        myprintf3_DMA_cnt((char *)send, sizeof(cons_queue) + 2 + 2);
                        if (xSemaphoreTake(DMA_Transmit_semaphore, 100 / portTICK_PERIOD_MS) == pdTRUE) //正常情况下10ms足以
                        {
                            USART3_IRQ_Enable(); /* 这就意味着服务器可以继续工作 */
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
                    xSemaphoreGive(private_SEVER_thread_mutex); /* 先归还信号量，然后再延时 */
                }
                else
                {
                    printf(":%d->there is sb qiangzhan\r\n", thread->thread_ID);
                    vTaskDelay(50);
                }
                rx_count = 0; //清零
            }
        }
    }
}
/**
 * @Function Name: recycle_task
 * @Description: 回收任务
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
    这里的esp8266的命令顺序是按照服务器配置顺序的，根据不同的服务器初始化进行配置即可
    串口发送出去的第一个字符是作废的！！！！（问题已解决）
 */
static const char esp8266_cmd[][40] = {
    /* 服务器初始化流程 */
    "AT+CWMODE=1\r\n",
    "AT+RST\r\n",
    "AT+CWJAP=\"TP-LINK_206\",\"123456789\"\r\n",
    "AT+CIFSR\r\n",
    "AT+CIPMUX=1\r\n",
    "AT+CIPSERVER=1,1001\r\n",
    /* 查询热点 */
    "AT+CWJAP?\r\n",
    /* 客户端请求和退出cmd */
    ",CONNECT\r\n", /* 客户端第一个命令 */
    ",CONNECT FAIL\r\n",
    ",CLOSED\r\n",
    /* 
        服务器能够提供的服务,服务名称在这里填写
     */
    "1.start",
    "2.bbb",
    "3.ccc",
    "4.ddd",
    "5.eee",
    "6.fff",
    "\0\0\0\0\0\0\0\0\0\0", /* 结束循环检测命令 */
};