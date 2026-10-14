/**
 * @file main.c
 * @brief 系统程序入口
 *
 * @文件职责
 * main.c 只负责整个系统的启动流程：
 *
 *     HAL初始化
 *        ↓
 *     系统时钟初始化
 *        ↓
 *     板级基础资源初始化
 *        ↓
 *     I2C / SPI 等总线初始化
 *        ↓
 *     OLED / RC522 / Servo 等模块初始化
 *        ↓
 *     创建FreeRTOS对象和任务
 *        ↓
 *     启动FreeRTOS调度器
 *
 * @设计原则
 * main.c 不负责具体业务逻辑。
 *
 * 例如以下内容不应该长期写在main.c中：
 *
 *     按键检测
 *     RFID轮询
 *     UID判断
 *     开门延时
 *     门禁状态机
 *
 * 这些功能应该由App层的Task负责。
 */

#include "main.h"

#include "bsp.h"
#include "bsp_i2c.h"
#include "bsp_spi.h"
#include "bsp_uart.h"

#include "oled_ssd1315.h"
#include "servo.h"
#include "rc522.h"

#include "app_tasks.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>

/**
 * @brief 配置系统时钟为HSI 8MHz
 *
 * @说明
 * 当前项目暂时保持：
 *
 *     HSI = 8MHz
 *     PLL = OFF
 *     SYSCLK = 8MHz
 *
 * AHB、APB1、APB2均不分频。
 *
 * @注意
 * 后续如果切换到72MHz，
 * TIM、USART、I2C、SPI等外设的时钟参数都需要重新检查。
 */
static void SystemClock_Config(void);


/**
 * @brief 系统程序入口
 *
 * @return 正常情况下不会返回
 *
 * @注意
 * vTaskStartScheduler()成功以后，
 * CPU控制权交给FreeRTOS调度器，
 * main()不会继续向下运行。
 */
int main(void)
{
    /*
     * ============================================================
     * 1. 初始化STM32 HAL
     * ============================================================
     *
     * HAL_Init()会初始化HAL基础环境。
     *
     * 当前工程中：
     *
     *     SysTick -> FreeRTOS
     *     TIM2    -> HAL Tick
     */
    HAL_Init();


    /*
     * ============================================================
     * 2. 配置系统时钟
     * ============================================================
     *
     * 当前SYSCLK保持8MHz。
     */
    SystemClock_Config();


    /*
     * ============================================================
     * 3. 初始化板级基础资源
     * ============================================================
     *
     * 当前BSP_Init()主要负责已经封装好的板级基础资源。
     *
     * USART1调试串口初始化完成以后，
     * printf才能正常输出。
     */
    BSP_Init();

    printf("\r\n============================\r\n");
    printf(" Access Control System Start\r\n");
    printf("============================\r\n");
    

    /*
    * 初始化K230D通信串口。
    * USART2：PA2 TX / PA3 RX。
    */
    if (BSP_UART2_Init() != HAL_OK)
    {
        Error_Handler();
    }
    /*
     * ============================================================
     * 5. 初始化SPI2
     * ============================================================
     *
     * 硬件：
     *
     *     PB13 -> SCK
     *     PB14 -> MISO
     *     PB15 -> MOSI
     *
     * 当前SPI2总线由：
     *
     *     W25Q128
     *     RC522
     *
     * 共同使用。
     *
     * 两个器件通过不同CS区分。
     */
    BSP_I2C1_Init();
    BSP_SPI2_Init();
    
    /*
     * ============================================================
     * 6. 初始化RC522
     * ============================================================
     *
     * 必须在BSP_SPI2_Init()之后调用。
     *
     * RC522 VersionReg已经实际验证能够稳定读到：
     *
     *     0x92
     *
     * 因此这里不再重复进行VersionReg测试。
     *
     * 后续卡片检测由RfidTask负责。
     */
    RC522_Init();


    /*
     * ============================================================
     * 7. 初始化门锁舵机
     * ============================================================
     *
     * 硬件：
     *
     *     PB8 -> TIM4_CH3
     *
     * Servo_Init()内部会：
     *
     *     初始化PWM
     *          ↓
     *     直接从关门位置开始输出
     *
     * 避免PWM先到中间位置再跳到关门位置，
     * 减少上电瞬间抽动。
     */
    Servo_Init();

  

    /*
     * ============================================================
     * 8. 初始化OLED
     * ============================================================
     *
     * OLED：
     *
     *     SSD1315
     *     128x64
     *     I2C address = 0x3C
     */
    if (OLED_Init() != HAL_OK)
    {
        /*
         * OLED属于当前项目重要的人机界面。
         *
         * 初始化失败时暂时认为是严重启动故障。
         *
         * 后续如果希望系统在OLED损坏时仍继续运行，
         * 可以改成记录错误而不是进入Error_Handler。
         */
        printf("ERROR: OLED init failed\r\n");

        Error_Handler();
    }


    /*
     * 清空Frame Buffer和OLED显示内容。
     */
    if (OLED_Clear() != HAL_OK)
    {
        printf("ERROR: OLED clear failed\r\n");

        Error_Handler();
    }


    /*
     * 显示系统启动界面。
     *
     * OLED_ShowString()只修改Frame Buffer，
     * OLED_UpdateScreen()才真正把内容发送到OLED。
     */
    OLED_ShowString(
        0,
        0,
        "Access Control"
    );

    OLED_ShowString(
        0,
        16,
        "System Ready"
    );

    if (OLED_UpdateScreen() != HAL_OK)
    {
        printf("ERROR: OLED update failed\r\n");

        Error_Handler();
    }


    /*
     * ============================================================
     * 9. 初始化应用层对象
     * ============================================================
     *
     * App_Init()当前负责创建Queue等
     * FreeRTOS应用对象。
     *
     * 如果创建失败，通常意味着：
     *
     *     Heap不足
     *     参数错误
     *     RTOS对象创建失败
     */
    if (App_Init() != pdPASS)
    {
        printf("ERROR: App_Init failed\r\n");

        Error_Handler();
    }


    /*
     * ============================================================
     * 10. 创建FreeRTOS任务
     * ============================================================
     *
     * 当前包括：
     *
     *     KeyTask
     *     LedTask
     *     RfidTask
     *
     * 后续还会逐步调整为正式的：
     *
     *     AuthTask
     *     AccessTask
     *     ...
     */
    if (App_CreateTasks() != pdPASS)
    {
        printf("ERROR: App_CreateTasks failed\r\n");

        Error_Handler();
    }


    printf("System Ready\r\n");

   

    /*
     * ============================================================
     * 11. 启动FreeRTOS Scheduler
     * ============================================================
     *
     * 从这里开始：
     *
     *     main()
     *
     * 不再负责系统业务。
     *
     * FreeRTOS会根据任务优先级调度：
     *
     *     KeyTask
     *     LedTask
     *     RfidTask
     *     ...
     */
    vTaskStartScheduler();


    /*
     * 正常情况下永远不会运行到这里。
     *
     * 如果vTaskStartScheduler()返回，
     * 通常说明FreeRTOS无法启动，
     * 比如Idle Task创建时Heap不足。
     */
    Error_Handler();


    /*
     * 理论不可达。
     * 写return只是保持C函数形式完整。
     */
    return 0;
}


/**
 * @brief 配置系统时钟为内部HSI 8MHz
 *
 * @调用时机
 *
 *     HAL_Init();
 *     SystemClock_Config();
 *
 * 必须在大部分外设初始化之前执行。
 *
 * @当前配置
 *
 *     HSI       = ON
 *     PLL       = OFF
 *     SYSCLK    = HSI 8MHz
 *     AHB       = 8MHz
 *     APB1      = 8MHz
 *     APB2      = 8MHz
 *
 * @易错点
 * 以后如果改成72MHz：
 *
 * 1. Flash Latency需要重新设置；
 * 2. TIM4 PWM参数需要重新计算；
 * 3. SPI2分频需要重新检查；
 * 4. USART波特率依赖外设时钟；
 * 5. I2C时序也应该重新验证。
 */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};


    /*
     * ============================================================
     * 配置内部HSI振荡器
     * ============================================================
     */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;

    osc.HSIState = RCC_HSI_ON;

    osc.HSICalibrationValue =
        RCC_HSICALIBRATION_DEFAULT;

    /*
     * 当前不使用PLL。
     */
    osc.PLL.PLLState = RCC_PLL_OFF;


    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        Error_Handler();
    }


    /*
     * ============================================================
     * 配置系统总线时钟
     * ============================================================
     */
    clk.ClockType =
        RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_PCLK1 |
        RCC_CLOCKTYPE_PCLK2;


    /*
     * 系统时钟来源：
     *
     *     HSI = 8MHz
     */
    clk.SYSCLKSource =
        RCC_SYSCLKSOURCE_HSI;


    /*
     * 所有总线当前均不分频。
     */
    clk.AHBCLKDivider =
        RCC_SYSCLK_DIV1;

    clk.APB1CLKDivider =
        RCC_HCLK_DIV1;

    clk.APB2CLKDivider =
        RCC_HCLK_DIV1;


    /*
     * 8MHz下使用FLASH_LATENCY_0。
     */
    if (HAL_RCC_ClockConfig(
            &clk,
            FLASH_LATENCY_0) != HAL_OK)
    {
        Error_Handler();
    }
}


/**
 * @brief 系统严重错误处理
 *
 * @用途
 * 当系统遇到无法继续安全运行的错误时，
 * 关闭中断并停留在死循环。
 *
 * @注意
 * 进入本函数以后：
 *
 *     FreeRTOS不会继续调度
 *     HAL Tick不会正常推进
 *
 * 所以不要再在这里调用：
 *
 *     vTaskDelay()
 *     HAL_Delay()
 *
 * 等依赖中断的接口。
 */
void Error_Handler(void)
{
    __disable_irq();

    for (;;)
    {
        /*
         * 当前暂时停在这里。
         *
         * 后续项目完善时，
         * 可以增加故障LED编码等机制。
         */
    }
}
