/*
 * App_TxService.c
 *
 *  Created on: 2026. 6. 23.
 *      Author: USER
 */
#include "App_StatusTxService.h"
#include "App_Can.h"
#include "App_PdwService.h"
#include "App_RxService.h"
#include "task.h"

#define APP_TX_STATUS_PERIOD_MS        (10u) // 20ms 주기 송신. 50Hz. CAN FD 0x400 메시지 송신용 - 아직 확정 아님!!!(초음파 주기 값에 따라 수정)

//CAN 0x400 메시지 송신용 서비스

static void AppStatusTxService_BuildDistanceLevel(DistanceLevelCmd_t *tx,
                                                  const AppPdwState *pdw,
                                                  const AppRpiInputState *rpiInput);

void AppStatusTxService_Task(void *arg)
{
    DistanceLevelCmd_t tx; // 0x400 메시지 구조체
    AppPdwState pdw;
    AppRpiInputState rpiInput;
    TickType_t lastWakeTime;

    (void)arg;

    lastWakeTime = xTaskGetTickCount();

    for(;;)
    {
        if((AppPdwService_GetState(&pdw) == pdPASS) &&
           (AppRxService_GetRpiInput(&rpiInput) == pdPASS))
        {
            AppStatusTxService_BuildDistanceLevel(&tx, &pdw, &rpiInput);
            (void)AppCan_SendDistanceLevel(&tx);
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(APP_TX_STATUS_PERIOD_MS));
    }
}

static void AppStatusTxService_BuildDistanceLevel(DistanceLevelCmd_t *tx,
                                                  const AppPdwState *pdw,
                                                  const AppRpiInputState *rpiInput)
{
    if((tx == NULL) || (pdw == NULL) || (rpiInput == NULL))
    {
        return;
    }

    tx->frontLevel = (uint8)pdw->level[APP_PDW_DIR_FRONT];
    tx->frontRightLevel = (uint8)pdw->level[APP_PDW_DIR_FRONT_RIGHT];
    tx->rightFrontLevel = (uint8)pdw->level[APP_PDW_DIR_RIGHT_FRONT];
    tx->rightBehindLevel = (uint8)pdw->level[APP_PDW_DIR_RIGHT_BEHIND];
    tx->behindRightLevel = (uint8)pdw->level[APP_PDW_DIR_BEHIND_RIGHT];
    tx->behindLevel = (uint8)pdw->level[APP_PDW_DIR_BEHIND];
    tx->behindLeftLevel = (uint8)pdw->level[APP_PDW_DIR_BEHIND_LEFT];
    tx->leftBehindLevel = (uint8)pdw->level[APP_PDW_DIR_LEFT_BEHIND];
    tx->leftFrontLevel = (uint8)pdw->level[APP_PDW_DIR_LEFT_FRONT];
    tx->frontLeftLevel = (uint8)pdw->level[APP_PDW_DIR_FRONT_LEFT];

    tx->emergencyStop = (pdw->enabled == TRUE) ? 1u : 0u;
    tx->vehicleSpeed = rpiInput->driveCmd;
    tx->gearStatus = (uint8)rpiInput->gear;
    tx->collisionAlarm = (pdw->dangerDetected == TRUE) ? 1u : 0u;
}