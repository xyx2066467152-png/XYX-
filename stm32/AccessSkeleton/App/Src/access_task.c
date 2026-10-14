#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "app_events.h"
#include "servo.h"
#include "oled_ssd1315.h"
#include <stdio.h>
/**
 * @brief 门禁业务任务
 * @职责
 *   接收认证结果，并决定门应该怎么动作。
 * AUTH_GRANTED：
 *     OLED提示成功
 *     -> 开门
 *     -> 等待2秒
 *     -> 关门
 * AUTH_DENIED：
 *     OLED提示失败
 *     -> 不开门
 * @注意
 *   当前为了快速完成项目，
 *   开门保持时间使用vTaskDelay()。
 *   后续正式状态机阶段，
 *   再改成非阻塞的 WAIT_CLOSE 状态。
 */
void AccessTask(void *argument)
{
    QueueHandle_t authQueue;
    AuthEvent_t event;

    /*
     * 获取App_CreateTasks传进来的认证Queue。
     */
    authQueue = (QueueHandle_t)argument;

    /*
     * 这行只打印一次。
     *
     * 如果上电后看不到它，
     * 说明AccessTask根本没有被创建/运行。
     */
    printf("AccessTask started\r\n");

    for (;;)
    {
        if (xQueueReceive(
                authQueue,
                &event,
                portMAX_DELAY) == pdPASS)
        {
            /*
             * 能看到这句话说明：
             *
             * RfidTask
             *   ↓
             * authQueue
             *   ↓
             * AccessTask
             *
             * 整条消息链已经打通。
             */
            printf("Auth event received\r\n");
            if (event.source == AUTH_SOURCE_FACE)
            {
                printf("AccessTask FACE user=%u result=%u\r\n",
                       event.user_id, (unsigned int)event.result);
            }

            if (event.result == AUTH_GRANTED)
            {
                printf("Access Granted\r\n");

                OLED_Clear();
                OLED_ShowString(0, 0, "Access Granted");
                OLED_ShowString(0, 16, "Door Open");
                /*
                * 开门
                */
                Servo_Open();

                OLED_Clear();
                OLED_ShowString(0, 0, "Access Granted");
                OLED_ShowString(0, 16, "Door Open");
                OLED_UpdateScreen();

                /*
                * 保持开门2秒。
                * 当前阶段先使用阻塞延时，
                * 后面主门禁状态机会再改成非阻塞方式。
                */
                vTaskDelay(pdMS_TO_TICKS(2000U));


                /*
                * 开始关门。
                */
                Servo_Close();

                printf("Door Closed\r\n");

                /*
                * 舵机收到关门PWM后，机械动作并不是瞬间完成的。
                * 先给它一点时间运动到关门位置，
                * 再显示 Door Closed，会更符合实际状态。
                */
                vTaskDelay(pdMS_TO_TICKS(500U));


                /*
                * 提示门已经关闭。
                */
                OLED_Clear();
                OLED_ShowString(0, 0, "Access Granted");
                OLED_ShowString(0, 16, "Door Closed");
                OLED_UpdateScreen();


                /*
                * Door Closed 保留1.5秒，
                * 让用户能够看清提示。
                */
                vTaskDelay(pdMS_TO_TICKS(1500U));


                /*
                * 最后恢复系统待机界面。
                */
                OLED_Clear();
                OLED_ShowString(0, 0, "Access Control");
                OLED_ShowString(0, 16, "System Ready");
                OLED_UpdateScreen();
            }
            else
            {
                printf("Access Denied\r\n");

                OLED_Clear();
                OLED_ShowString(0, 0, "Access Denied");
                OLED_UpdateScreen();

                vTaskDelay(pdMS_TO_TICKS(1000U));

                OLED_Clear();
                OLED_ShowString(0, 0, "Access Control");
                OLED_ShowString(0, 16, "System Ready");
                OLED_UpdateScreen();
            }
        }
    }
}
