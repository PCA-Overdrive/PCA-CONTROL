#include "App_AutoExitService.h"
#include "App_AutoExitService_Internal.h"

#include "App_RxService.h"
#include "task.h"

typedef enum
{
    APP_AUTO_EXIT_STATE_IDLE = 0,
    APP_AUTO_EXIT_STATE_START_STOP,
    APP_AUTO_EXIT_STATE_SELECT_STRATEGY,
    APP_AUTO_EXIT_STATE_AVOID_ESCAPE,
    APP_AUTO_EXIT_STATE_AVOID_STOP_1,
    APP_AUTO_EXIT_STATE_AVOID_REALIGN,
    APP_AUTO_EXIT_STATE_AVOID_STOP_2,
    APP_AUTO_EXIT_STATE_RUN_PROFILE,
    APP_AUTO_EXIT_STATE_BLOCKED,
    APP_AUTO_EXIT_STATE_STOPPED
} AppAutoExitState;

typedef struct
{
    const AppAutoExitMotionStep *steps;
    uint32 count;
    uint32 index;
    TickType_t startTick;
    uint32 firstStepReductionMs;
} AppAutoExitProfileRuntime;

typedef struct
{
    uint32 escapeMs;
    uint32 realignMs;
    TickType_t escapeStartTick;
    TickType_t realignStartTick;
    uint32 escapeElapsedMs;
} AppAutoExitAvoidRuntime;

typedef struct
{
    boolean active;
    boolean cmdValid;
    VehicleControlCmd_t cmd;
    AppAutoExitCmd lastCommand;

    AppAutoExitState state;
    AppAutoExitDirection direction;
    TickType_t stateStartTick;

    AppAutoExitProfileRuntime profile;
    AppAutoExitAvoidRuntime avoid;
} AppAutoExitServiceContext;

static AppAutoExitServiceContext g_autoExit;

static void AppAutoExitService_ResetProfile(void)
{
    g_autoExit.profile.steps = 0;
    g_autoExit.profile.count = 0u;
    g_autoExit.profile.index = 0u;
    g_autoExit.profile.startTick = 0u;
    g_autoExit.profile.firstStepReductionMs = 0u;
}

static void AppAutoExitService_ResetAvoidPlan(void)
{
    g_autoExit.avoid.escapeMs = 0u;
    g_autoExit.avoid.realignMs = 0u;
    g_autoExit.avoid.escapeStartTick = 0u;
    g_autoExit.avoid.realignStartTick = 0u;
    g_autoExit.avoid.escapeElapsedMs = 0u;
}

static boolean AppAutoExitService_HasElapsed(TickType_t startTick,
                                             uint32 durationMs)
{
    TickType_t nowTick;

    nowTick = xTaskGetTickCount();

    return ((nowTick - startTick) >= pdMS_TO_TICKS(durationMs)) ? TRUE : FALSE;
}

static uint32 AppAutoExitService_GetElapsedMs(TickType_t startTick)
{
    TickType_t elapsedTick;

    elapsedTick = xTaskGetTickCount() - startTick;

    return (uint32)((elapsedTick * 1000u) / configTICK_RATE_HZ);
}

static void AppAutoExitService_SetCommand(uint8 driveCmd, uint8 steeringCmd)
{
    g_autoExit.cmd.driveCmd = driveCmd;
    g_autoExit.cmd.steeringCmd = steeringCmd;
    g_autoExit.cmdValid = TRUE;
}

static void AppAutoExitService_EnterIdle(void)
{
    g_autoExit.active = FALSE;
    g_autoExit.cmdValid = FALSE;
    g_autoExit.state = APP_AUTO_EXIT_STATE_IDLE;
    g_autoExit.direction = APP_AUTO_EXIT_DIR_STRAIGHT;

    AppAutoExitService_ResetProfile();
    AppAutoExitService_ResetAvoidPlan();
}

static void AppAutoExitService_EnterTimedStopState(AppAutoExitState state)
{
    g_autoExit.active = TRUE;
    g_autoExit.state = state;
    g_autoExit.stateStartTick = xTaskGetTickCount();

    AppAutoExitService_SetCommand(APP_AUTO_EXIT_DRIVE_STOP,
                                  APP_AUTO_EXIT_STEER_CENTER);
}

static void AppAutoExitService_EnterBlocked(void)
{
    AppAutoExitMonitor_SetResult(APP_AUTO_EXIT_STATUS_BLOCKED);
    AppAutoExitService_EnterTimedStopState(APP_AUTO_EXIT_STATE_BLOCKED);
}

static void AppAutoExitService_EnterStopped(void)
{
    AppAutoExitMonitor_SetResult(APP_AUTO_EXIT_STATUS_STOPPED);
    AppAutoExitService_EnterTimedStopState(APP_AUTO_EXIT_STATE_STOPPED);
}

static void AppAutoExitService_StopProfile(void)
{
    AppAutoExitService_EnterIdle();

    g_autoExit.cmd.driveCmd = APP_AUTO_EXIT_DRIVE_STOP;
    g_autoExit.cmd.steeringCmd = APP_AUTO_EXIT_STEER_CENTER;
}

static void AppAutoExitService_StartProfile(const AppAutoExitMotionStep *profile,
                                            uint32 profileCount,
                                            uint32 firstStepReductionMs)
{
    if((profile == 0) || (profileCount == 0u))
    {
        AppAutoExitService_EnterBlocked();
        return;
    }

    g_autoExit.profile.steps = profile;
    g_autoExit.profile.count = profileCount;
    g_autoExit.profile.index = 0u;
    g_autoExit.profile.firstStepReductionMs = firstStepReductionMs;
    g_autoExit.profile.startTick = xTaskGetTickCount();

    g_autoExit.active = TRUE;
    g_autoExit.state = APP_AUTO_EXIT_STATE_RUN_PROFILE;

    AppAutoExitService_SetCommand(g_autoExit.profile.steps[0].driveCmd,
                                  g_autoExit.profile.steps[0].steeringCmd);
}

static void AppAutoExitService_StartProfileForDirection(AppAutoExitDirection direction,
                                                        uint32 firstStepReductionMs)
{
    const AppAutoExitMotionStep *profile;
    uint32 profileCount;

    profile = AppAutoExitProfile_Get(direction, &profileCount);
    AppAutoExitService_StartProfile(profile, profileCount, firstStepReductionMs);
}

static uint32 AppAutoExitService_GetCurrentStepDurationMs(void)
{
    uint32 durationMs;

    durationMs = g_autoExit.profile.steps[g_autoExit.profile.index].durationMs;

    if((g_autoExit.profile.index == 0u) &&
       (g_autoExit.profile.firstStepReductionMs > 0u))
    {
        if(g_autoExit.profile.firstStepReductionMs >= durationMs)
        {
            durationMs = APP_AUTO_EXIT_MIN_STEP_MS;
        }
        else
        {
            durationMs = durationMs - g_autoExit.profile.firstStepReductionMs;
        }
    }

    return durationMs;
}

static void AppAutoExitService_CompleteProfile(void)
{
    if(AppAutoExitMonitor_FinishAndValidate() == TRUE)
    {
        AppAutoExitMonitor_SetResult(APP_AUTO_EXIT_STATUS_COMPLETE);
    }
    else
    {
        AppAutoExitMonitor_SetResult(APP_AUTO_EXIT_STATUS_BLOCKED);
    }

    AppAutoExitService_StopProfile();
}

static void AppAutoExitService_ServiceProfile(void)
{
    if(g_autoExit.state != APP_AUTO_EXIT_STATE_RUN_PROFILE)
    {
        return;
    }

    if((g_autoExit.profile.steps == 0) ||
       (g_autoExit.profile.index >= g_autoExit.profile.count))
    {
        AppAutoExitService_StopProfile();
        return;
    }

    if(AppAutoExitPlanner_IsStepSafetyDanger(
           &g_autoExit.profile.steps[g_autoExit.profile.index]) == TRUE)
    {
        AppAutoExitService_EnterBlocked();
        return;
    }

    if(AppAutoExitService_HasElapsed(g_autoExit.profile.startTick,
                                     AppAutoExitService_GetCurrentStepDurationMs()) == FALSE)
    {
        return;
    }

    g_autoExit.profile.index++;

    if(g_autoExit.profile.index >= g_autoExit.profile.count)
    {
        AppAutoExitService_CompleteProfile();
        return;
    }

    g_autoExit.profile.startTick = xTaskGetTickCount();

    AppAutoExitService_SetCommand(
        g_autoExit.profile.steps[g_autoExit.profile.index].driveCmd,
        g_autoExit.profile.steps[g_autoExit.profile.index].steeringCmd);
}

static void AppAutoExitService_StartAvoidEscape(void)
{
    g_autoExit.avoid.escapeStartTick = xTaskGetTickCount();
    g_autoExit.state = APP_AUTO_EXIT_STATE_AVOID_ESCAPE;

    AppAutoExitService_SetCommand(APP_AUTO_EXIT_DRIVE_FORWARD,
                                  AppAutoExitPlanner_GetEscapeSteer(g_autoExit.direction));
}

static void AppAutoExitService_FinishAvoidEscape(void)
{
    g_autoExit.avoid.escapeElapsedMs =
        AppAutoExitService_GetElapsedMs(g_autoExit.avoid.escapeStartTick);

    g_autoExit.state = APP_AUTO_EXIT_STATE_AVOID_STOP_1;
    g_autoExit.stateStartTick = xTaskGetTickCount();

    AppAutoExitService_SetCommand(APP_AUTO_EXIT_DRIVE_STOP,
                                  APP_AUTO_EXIT_STEER_CENTER);
}

static void AppAutoExitService_StartAvoidRealign(void)
{
    g_autoExit.avoid.realignStartTick = xTaskGetTickCount();
    g_autoExit.state = APP_AUTO_EXIT_STATE_AVOID_REALIGN;

    AppAutoExitService_SetCommand(APP_AUTO_EXIT_DRIVE_FORWARD,
                                  AppAutoExitPlanner_GetRealignSteer(g_autoExit.direction));
}

static void AppAutoExitService_FinishAvoidRealign(void)
{
    g_autoExit.state = APP_AUTO_EXIT_STATE_AVOID_STOP_2;
    g_autoExit.stateStartTick = xTaskGetTickCount();

    AppAutoExitService_SetCommand(APP_AUTO_EXIT_DRIVE_STOP,
                                  APP_AUTO_EXIT_STEER_CENTER);
}

static void AppAutoExitService_ApplyAvoidPlan(const AppAutoExitAvoidPlan *avoidPlan)
{
    if(avoidPlan == 0)
    {
        g_autoExit.avoid.escapeMs = 0u;
        g_autoExit.avoid.realignMs = 0u;
        return;
    }

    g_autoExit.avoid.escapeMs = avoidPlan->escapeMs;
    g_autoExit.avoid.realignMs = avoidPlan->realignMs;
}

static void AppAutoExitService_StartAutoExit(AppAutoExitDirection direction)
{
    if(g_autoExit.state != APP_AUTO_EXIT_STATE_IDLE)
    {
        return;
    }

    AppAutoExitMonitor_Start(direction);

    g_autoExit.direction = direction;

    if(direction == APP_AUTO_EXIT_DIR_STRAIGHT)
    {
        AppAutoExitService_StartProfileForDirection(APP_AUTO_EXIT_DIR_STRAIGHT,
                                                    0u);
        return;
    }

    g_autoExit.active = TRUE;
    g_autoExit.state = APP_AUTO_EXIT_STATE_START_STOP;
    g_autoExit.stateStartTick = xTaskGetTickCount();

    AppAutoExitService_SetCommand(APP_AUTO_EXIT_DRIVE_STOP,
                                  APP_AUTO_EXIT_STEER_CENTER);
}

static void AppAutoExitService_StartNormalExitProfile(void)
{
    AppAutoExitService_StartProfileForDirection(g_autoExit.direction, 0u);
}

static void AppAutoExitService_StartResumeExitProfile(void)
{
    uint32 firstStepReductionMs;

    firstStepReductionMs =
        AppAutoExitPlanner_CalcFirstStepReductionMs(g_autoExit.avoid.escapeElapsedMs,
                                                    g_autoExit.avoid.realignMs);

    AppAutoExitService_StartProfileForDirection(g_autoExit.direction,
                                                firstStepReductionMs);
}

static void AppAutoExitService_ServiceState(void)
{
    AppAutoExitStrategy strategy;
    AppAutoExitAvoidPlan avoidPlan;

    switch(g_autoExit.state)
    {
        case APP_AUTO_EXIT_STATE_IDLE:
            break;

        case APP_AUTO_EXIT_STATE_START_STOP:
            if(AppAutoExitService_HasElapsed(g_autoExit.stateStartTick,
                                             APP_AUTO_EXIT_START_STOP_MS) == TRUE)
            {
                g_autoExit.state = APP_AUTO_EXIT_STATE_SELECT_STRATEGY;
            }
            break;

        case APP_AUTO_EXIT_STATE_SELECT_STRATEGY:
            strategy = AppAutoExitPlanner_SelectStrategy(g_autoExit.direction,
                                                         &avoidPlan);
            AppAutoExitService_ApplyAvoidPlan(&avoidPlan);

            if(strategy == APP_AUTO_EXIT_STRATEGY_NORMAL)
            {
                AppAutoExitService_StartNormalExitProfile();
            }
            else if(strategy == APP_AUTO_EXIT_STRATEGY_AVOID_AND_RESUME)
            {
                AppAutoExitService_StartAvoidEscape();
            }
            else
            {
                AppAutoExitService_EnterBlocked();
            }
            break;

        case APP_AUTO_EXIT_STATE_AVOID_ESCAPE:
            if(AppAutoExitPlanner_IsOppositeSideDangerDuringAvoid(g_autoExit.direction) == TRUE)
            {
                if(AppAutoExitService_HasElapsed(g_autoExit.avoid.escapeStartTick,
                                                 APP_AUTO_EXIT_AVOID_ESCAPE_MIN_MS) == FALSE)
                {
                    AppAutoExitService_EnterBlocked();
                }
                else
                {
                    AppAutoExitService_FinishAvoidEscape();
                }
            }
            else if(AppAutoExitService_HasElapsed(g_autoExit.avoid.escapeStartTick,
                                                  g_autoExit.avoid.escapeMs) == TRUE)
            {
                AppAutoExitService_FinishAvoidEscape();
            }
            break;

        case APP_AUTO_EXIT_STATE_AVOID_STOP_1:
            if(AppAutoExitService_HasElapsed(g_autoExit.stateStartTick,
                                             APP_AUTO_EXIT_SHIFT_STOP_MS) == TRUE)
            {
                AppAutoExitService_StartAvoidRealign();
            }
            break;

        case APP_AUTO_EXIT_STATE_AVOID_REALIGN:
            if(AppAutoExitService_HasElapsed(g_autoExit.avoid.realignStartTick,
                                             g_autoExit.avoid.realignMs) == TRUE)
            {
                AppAutoExitService_FinishAvoidRealign();
            }
            break;

        case APP_AUTO_EXIT_STATE_AVOID_STOP_2:
            if(AppAutoExitService_HasElapsed(g_autoExit.stateStartTick,
                                             APP_AUTO_EXIT_SHIFT_STOP_MS) == TRUE)
            {
                AppAutoExitService_StartResumeExitProfile();
            }
            break;

        case APP_AUTO_EXIT_STATE_RUN_PROFILE:
            AppAutoExitService_ServiceProfile();
            break;

        case APP_AUTO_EXIT_STATE_BLOCKED:
        case APP_AUTO_EXIT_STATE_STOPPED:
            if(AppAutoExitService_HasElapsed(g_autoExit.stateStartTick,
                                             APP_AUTO_EXIT_FINAL_STOP_MS) == TRUE)
            {
                AppAutoExitService_EnterIdle();
            }
            break;

        default:
            AppAutoExitService_EnterBlocked();
            break;
    }
}

static void AppAutoExitService_HandleCommand(AppAutoExitCmd command)
{
    switch(command)
    {
        case APP_AUTO_EXIT_CMD_START_STRAIGHT:
            AppAutoExitService_StartAutoExit(APP_AUTO_EXIT_DIR_STRAIGHT);
            break;

        case APP_AUTO_EXIT_CMD_START_LEFT:
            AppAutoExitService_StartAutoExit(APP_AUTO_EXIT_DIR_LEFT);
            break;

        case APP_AUTO_EXIT_CMD_START_RIGHT:
            AppAutoExitService_StartAutoExit(APP_AUTO_EXIT_DIR_RIGHT);
            break;

        case APP_AUTO_EXIT_CMD_STOP:
            if(g_autoExit.state != APP_AUTO_EXIT_STATE_IDLE)
            {
                AppAutoExitService_EnterStopped();
            }
            else
            {
                AppAutoExitMonitor_SetIdle();
            }
            break;

        case APP_AUTO_EXIT_CMD_NORMAL:
            if(g_autoExit.state == APP_AUTO_EXIT_STATE_IDLE)
            {
                AppAutoExitMonitor_SetIdle();
            }
            break;

        default:
            break;
    }
}

void AppAutoExitService_Init(void)
{
    AppAutoExitMonitor_Init();

    AppAutoExitService_EnterIdle();

    g_autoExit.cmd.driveCmd = APP_AUTO_EXIT_DRIVE_STOP;
    g_autoExit.cmd.steeringCmd = APP_AUTO_EXIT_STEER_CENTER;
    g_autoExit.lastCommand = APP_AUTO_EXIT_CMD_NORMAL;
}

boolean AppAutoExitService_IsActive(void)
{
    return g_autoExit.active;
}

BaseType_t AppAutoExitService_GetControlCommand(VehicleControlCmd_t *cmd)
{
    if(cmd == NULL)
    {
        return pdFAIL;
    }

    if((g_autoExit.active == FALSE) || (g_autoExit.cmdValid == FALSE))
    {
        return pdFAIL;
    }

    *cmd = g_autoExit.cmd;
    return pdPASS;
}

void AppAutoExitService_Task(void *arg)
{
    TickType_t lastWakeTime;
    AppAutoParkingState autoParking;

    (void)arg;

    lastWakeTime = xTaskGetTickCount();

    for(;;)
    {
        if(AppRxService_GetAutoParkingState(&autoParking) == pdPASS)
        {
            if(autoParking.cmd != g_autoExit.lastCommand)
            {
                AppAutoExitService_HandleCommand(autoParking.cmd);
                g_autoExit.lastCommand = autoParking.cmd;
            }
        }

        AppAutoExitService_ServiceState();
        AppAutoExitMonitor_Service();

        vTaskDelayUntil(&lastWakeTime,
                        pdMS_TO_TICKS(APP_AUTO_EXIT_SERVICE_PERIOD_MS));
    }
}
