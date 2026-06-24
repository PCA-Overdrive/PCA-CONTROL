#include "App_AutoExitService.h"

#include "App_RxService.h"
#include "task.h"

#define APP_AUTO_EXIT_SERVICE_PERIOD_MS 10u

static boolean g_autoExitActive = FALSE;
static boolean g_autoExitCmdValid = FALSE;
static VehicleControlCmd_t g_autoExitCmd;

void AppAutoExitService_Init(void)
{
    g_autoExitActive = FALSE;
    g_autoExitCmdValid = FALSE;

    g_autoExitCmd.driveCmd = 127u;
    g_autoExitCmd.steeringCmd = 127u;
}

boolean AppAutoExitService_IsActive(void)
{
    return g_autoExitActive;
}

BaseType_t AppAutoExitService_GetControlCommand(VehicleControlCmd_t *cmd)
{
    if(cmd == NULL)
    {
        return pdFAIL;
    }

    if((g_autoExitActive == FALSE) || (g_autoExitCmdValid == FALSE))
    {
        return pdFAIL;
    }

    *cmd = g_autoExitCmd;
    return pdPASS;
}

void AppAutoExitService_Task(void *arg)
{
    TickType_t lastWakeTime;
    AppAutoParkingState autoParking;
    static boolean prevStart = FALSE;

    (void)arg;

    lastWakeTime = xTaskGetTickCount();

    for(;;)
    {
        if(AppRxService_GetAutoParkingState(&autoParking) == pdPASS)
        {
            if((autoParking.autoParkingStart == TRUE) &&
               (prevStart == FALSE))
            {
                /*
                 * 아직 진짜 출차 로직 넣기 전.
                 * 1차 테스트: autoParkingStart 들어오면 정지 명령 active만 켜기.
                 */
                g_autoExitActive = TRUE;
                g_autoExitCmdValid = TRUE;
                g_autoExitCmd.driveCmd = 127u;
                g_autoExitCmd.steeringCmd = 127u;
            }

            prevStart = autoParking.autoParkingStart;
        }

        vTaskDelayUntil(&lastWakeTime,
                        pdMS_TO_TICKS(APP_AUTO_EXIT_SERVICE_PERIOD_MS));
    }
}
