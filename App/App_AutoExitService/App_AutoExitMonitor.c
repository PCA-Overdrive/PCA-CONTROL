#include "App_AutoExitService_Internal.h"

#include "App_Can.h"
#include "App_RxService.h"
#include "task.h"

typedef struct
{
    AppAutoExitStatus status;
    TickType_t resultStartTick;
    TickType_t lastStatusTxTick;

    sint16 startYawDeg;
    sint16 endYawDeg;
    sint16 lineAngleDeg;
    sint16 targetTurnDeg;
    sint16 targetYawDeg;
    sint16 yawErrorDeg;
    boolean yawValid;
} AppAutoExitMonitorContext;

static AppAutoExitMonitorContext g_monitor;

static boolean AppAutoExitMonitor_HasElapsed(TickType_t startTick,
                                             uint32 durationMs)
{
    TickType_t nowTick;

    nowTick = xTaskGetTickCount();

    return ((nowTick - startTick) >= pdMS_TO_TICKS(durationMs)) ? TRUE : FALSE;
}

static boolean AppAutoExitMonitor_IsResultStatus(AppAutoExitStatus status)
{
    if((status == APP_AUTO_EXIT_STATUS_COMPLETE) ||
       (status == APP_AUTO_EXIT_STATUS_STOPPED))
    {
        return TRUE;
    }

    return FALSE;
}

static sint16 AppAutoExitMonitor_NormalizeYawDeg(sint16 yawDeg)
{
    while(yawDeg > 180)
    {
        yawDeg -= 360;
    }

    while(yawDeg < -180)
    {
        yawDeg += 360;
    }

    return yawDeg;
}

static sint16 AppAutoExitMonitor_CalcYawErrorToTargetDeg(sint16 targetYawDeg,
                                                         sint16 currentYawDeg)
{
    return AppAutoExitMonitor_NormalizeYawDeg((sint16)(targetYawDeg - currentYawDeg));
}

#if (APP_AUTO_EXIT_YAW_VALIDATION_ENABLE != 0u)
static sint16 AppAutoExitMonitor_AbsSint16(sint16 value)
{
    return (value < 0) ? (sint16)(-value) : value;
}
#endif

static sint16 AppAutoExitMonitor_CalcTargetTurnDeg(AppAutoExitDirection direction,
                                                   sint16 lineAngleDeg)
{
    sint16 targetTurnDeg;

    if(direction == APP_AUTO_EXIT_DIR_STRAIGHT)
    {
        return 0;
    }

    if(direction == APP_AUTO_EXIT_DIR_RIGHT)
    {
        targetTurnDeg = (sint16)(APP_AUTO_EXIT_BASE_TURN_DEG + lineAngleDeg);
    }
    else
    {
        targetTurnDeg = (sint16)(APP_AUTO_EXIT_BASE_TURN_DEG - lineAngleDeg);
    }

    if(targetTurnDeg < APP_AUTO_EXIT_TARGET_TURN_MIN_DEG)
    {
        targetTurnDeg = APP_AUTO_EXIT_TARGET_TURN_MIN_DEG;
    }
    else if(targetTurnDeg > APP_AUTO_EXIT_TARGET_TURN_MAX_DEG)
    {
        targetTurnDeg = APP_AUTO_EXIT_TARGET_TURN_MAX_DEG;
    }

    return targetTurnDeg;
}

static sint16 AppAutoExitMonitor_CalcTargetYawDeg(sint16 startYawDeg,
                                                  AppAutoExitDirection direction,
                                                  sint16 targetTurnDeg)
{
    sint16 turnSign;

    if(direction == APP_AUTO_EXIT_DIR_RIGHT)
    {
        turnSign = APP_AUTO_EXIT_IMU_RIGHT_SIGN;
    }
    else if(direction == APP_AUTO_EXIT_DIR_LEFT)
    {
        turnSign = (sint16)(-APP_AUTO_EXIT_IMU_RIGHT_SIGN);
    }
    else
    {
        turnSign = 0;
    }

    return AppAutoExitMonitor_NormalizeYawDeg(
        (sint16)(startYawDeg + (sint16)(turnSign * targetTurnDeg)));
}

static void AppAutoExitMonitor_ResetYaw(void)
{
    g_monitor.startYawDeg = 0;
    g_monitor.endYawDeg = 0;
    g_monitor.lineAngleDeg = 0;
    g_monitor.targetTurnDeg = 0;
    g_monitor.targetYawDeg = 0;
    g_monitor.yawErrorDeg = 0;
    g_monitor.yawValid = FALSE;
}

static void AppAutoExitMonitor_CaptureEndYaw(void)
{
    AppUltrasonicState ultrasonic;

    if((g_monitor.yawValid == TRUE) &&
       (AppRxService_GetUltrasonicState(&ultrasonic) == pdPASS))
    {
        g_monitor.endYawDeg = ultrasonic.imuYaw;
        g_monitor.yawErrorDeg =
            AppAutoExitMonitor_CalcYawErrorToTargetDeg(g_monitor.targetYawDeg,
                                                       g_monitor.endYawDeg);
    }
    else
    {
        g_monitor.yawValid = FALSE;
    }
}

static boolean AppAutoExitMonitor_IsYawCompletionValid(void)
{
#if (APP_AUTO_EXIT_YAW_VALIDATION_ENABLE == 0u)
    return TRUE;
#else
    sint16 absYawErrorDeg;

    if(g_monitor.yawValid == FALSE)
    {
        return FALSE;
    }

    absYawErrorDeg = AppAutoExitMonitor_AbsSint16(g_monitor.yawErrorDeg);

    return (absYawErrorDeg <= APP_AUTO_EXIT_YAW_TARGET_TOL_DEG) ? TRUE : FALSE;
#endif
}

static void AppAutoExitMonitor_ClearExpiredResult(void)
{
    if(AppAutoExitMonitor_IsResultStatus(g_monitor.status) == FALSE)
    {
        return;
    }

    if(AppAutoExitMonitor_HasElapsed(g_monitor.resultStartTick,
                                     APP_AUTO_EXIT_RESULT_HOLD_MS) == TRUE)
    {
        g_monitor.status = APP_AUTO_EXIT_STATUS_IDLE;
    }
}

static void AppAutoExitMonitor_SendStatus(AppAutoExitStatus status)
{
    ExitCompleteCmd_t tx;

    tx.autoparkingStatus = (uint8)status;

    (void)AppCan_SendExitComplete(&tx);
}

static void AppAutoExitMonitor_ServiceStatusTx(void)
{
    TickType_t nowTick;

    nowTick = xTaskGetTickCount();

    if((nowTick - g_monitor.lastStatusTxTick) >= pdMS_TO_TICKS(APP_AUTO_EXIT_STATUS_TX_PERIOD_MS))
    {
        AppAutoExitMonitor_SendStatus(g_monitor.status);
        g_monitor.lastStatusTxTick = nowTick;
    }
}

void AppAutoExitMonitor_Init(void)
{
    g_monitor.status = APP_AUTO_EXIT_STATUS_IDLE;
    g_monitor.resultStartTick = 0u;
    g_monitor.lastStatusTxTick = xTaskGetTickCount();

    AppAutoExitMonitor_ResetYaw();
}

void AppAutoExitMonitor_Start(AppAutoExitDirection direction)
{
    AppUltrasonicState ultrasonic;
    AppRpiInputState rpiInput;

    g_monitor.status = APP_AUTO_EXIT_STATUS_IN_PROGRESS;
    AppAutoExitMonitor_ResetYaw();

    if(AppRxService_GetRpiInput(&rpiInput) == pdPASS)
    {
        g_monitor.lineAngleDeg = rpiInput.lineAngleDeg;
    }

    if(AppRxService_GetUltrasonicState(&ultrasonic) == pdPASS)
    {
        g_monitor.startYawDeg = ultrasonic.imuYaw;
        g_monitor.endYawDeg = ultrasonic.imuYaw;
        g_monitor.yawValid = TRUE;
    }

    g_monitor.targetTurnDeg =
        AppAutoExitMonitor_CalcTargetTurnDeg(direction,
                                             g_monitor.lineAngleDeg);

    if(g_monitor.yawValid == TRUE)
    {
        g_monitor.targetYawDeg =
            AppAutoExitMonitor_CalcTargetYawDeg(g_monitor.startYawDeg,
                                                direction,
                                                g_monitor.targetTurnDeg);

        g_monitor.yawErrorDeg =
            AppAutoExitMonitor_CalcYawErrorToTargetDeg(g_monitor.targetYawDeg,
                                                       g_monitor.endYawDeg);
    }
}

void AppAutoExitMonitor_SetIdle(void)
{
    g_monitor.status = APP_AUTO_EXIT_STATUS_IDLE;
}

void AppAutoExitMonitor_SetResult(AppAutoExitStatus status)
{
    g_monitor.status = status;
    g_monitor.resultStartTick = xTaskGetTickCount();
}

boolean AppAutoExitMonitor_FinishAndValidate(void)
{
    AppAutoExitMonitor_CaptureEndYaw();

    return AppAutoExitMonitor_IsYawCompletionValid();
}

void AppAutoExitMonitor_Service(void)
{
    if(g_monitor.status == APP_AUTO_EXIT_STATUS_IN_PROGRESS)
    {
        AppAutoExitMonitor_CaptureEndYaw();
    }

    AppAutoExitMonitor_ClearExpiredResult();
    AppAutoExitMonitor_ServiceStatusTx();
}
