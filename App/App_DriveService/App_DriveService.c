#include "App_DriveService.h"
#include "App_AutoExitService/App_AutoExitService.h"
#include "App_Can.h"
#include "App_PdwService.h"
#include "App_RxService.h"
#include "task.h"

#define APP_DRIVE_SERVICE_PERIOD_MS    (12u)

/* 라즈베리파이 CAN 수신 타임아웃 임계값 (ms) */
#define APP_DRIVE_RPI_TIMEOUT_MS       (200u)

/* 외부(App_Can.c)에서 0x201 수신 시 갱신하는 tick 변수 */
volatile TickType_t g_rpiLastRxTick = 0u;

void AppDriveService_Task(void *arg)
{
    AppPdwState         pdw;
    AppRpiInputState    rpiInput;
    VehicleControlCmd_t tx;
    VehicleControlCmd_t autoExitCmd;
    (void)arg;

    /* 시작 시점으로 초기화 (부팅 직후 타임아웃 오판 방지) */
    g_rpiLastRxTick = xTaskGetTickCount();

    for(;;)
    {
        /* -----------------------------------------------
           라즈베리파이 타임아웃 판정
           ----------------------------------------------- */
        boolean rpiTimedOut =
            ((xTaskGetTickCount() - g_rpiLastRxTick) >= pdMS_TO_TICKS(APP_DRIVE_RPI_TIMEOUT_MS))
            ? TRUE : FALSE;

        if(rpiTimedOut == TRUE)
        {
            /* 라즈베리파이 꺼짐 → 중립 강제 송신 */
            tx.driveCmd    = 127u;
            tx.steeringCmd = 127u;
            (void)AppCan_SendVehicleControl(&tx);
        }
        else if((AppPdwService_GetState(&pdw) == pdPASS) &&
                (AppRxService_GetRpiInput(&rpiInput) == pdPASS))
        {
            if(AppAutoExitService_GetControlCommand(&autoExitCmd) == pdPASS)
            {
                tx = autoExitCmd;
            }
            else if((pdw.enabled == TRUE) && (pdw.dangerDetected == TRUE))
            {
                tx.driveCmd    = 127u;
                tx.steeringCmd = 127u; /* 기존 코드에 steeringCmd 누락 - 함께 수정 */
            }
            else
            {
                tx.driveCmd    = rpiInput.driveCmd;
                tx.steeringCmd = rpiInput.steeringCmd;
            }
            (void)AppCan_SendVehicleControl(&tx);
        }

        vTaskDelay(pdMS_TO_TICKS(APP_DRIVE_SERVICE_PERIOD_MS));
    }
}
