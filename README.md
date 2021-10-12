# Server Component 🚀
## Preface
This server component is used for system monitoring and data analysis. <br>
And it seems that just add a thread to your existing embedded platform,The impact on the behavior of the original embedded platform is minimal.<br>
And greatly improve your understanding of embedded systems, giving you a different vision.<br>
#### Attention:Rar file contains examples of development boards based on spot atom Explorer, which can be run directly.But its context is not the latest version
## Architecture
![](https://gitee.com/xudangling_admin/pic-go/raw/master/20211006155511.png)
### Introduce
#### Hardware is consist of the TIM、UART、DMA.
#### Operate System is FreeRTOS.Other real-time operating systems may be added later.
#### On the basis of the original embedded service software, add the sever component.(Almost no impact on your existing embedded system)
## How to apply it
### first situation(your development board is explorer from zhengdianyuanzi)
The way to use it is incredibly simple<br>
1. Download the threadx_dbs.c source file.
2. Add this file to your original embedded project.
3. Create the thread(Threads need to be created before they can be used).<br>
### second situation(your development board is not explorer from zhengdianyuanzi)
The way to use it maybe a little difficult
1. Download the threadx_dbs.c source file.
2. Add this file to your original embedded project.
3. Add corresponding hardware driver,modify it on the original frame.This Step may cost you a lot of time.
4. Create the thread(Threads need to be created before they can be used).
## Initialization Process
Assume that you have created the thread.
to be continue...
## Bug fixed Timeline
### 2021.10.6
Add the latest demo to the repo.<br>
Changed the way consumers copy data up to the bit machine,and fix the metaphysics bug.
### 2021.10.12
Fix the predata send error,before just wait for esp8266 for 10ms.<br>
But now sever must have received SEND OK to do the next