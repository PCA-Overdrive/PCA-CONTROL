#include "App_AutoExitService.h"

#include "App_RxService.h"
#include "task.h"

#define APP_AUTO_EXIT_SERVICE_PERIOD_MS 10u

#define APP_AUTO_EXIT_DRIVE_STOP       127u
#define APP_AUTO_EXIT_STEER_CENTER     127u

#define APP_AUTO_EXIT_DRIVE_FORWARD    80u
#define APP_AUTO_EXIT_DRIVE_REVERSE    200u

#define APP_AUTO_EXIT_STEER_LEFT       0u
#define APP_AUTO_EXIT_STEER_RIGHT      255u

#define APP_AUTO_EXIT_SHIFT_STOP_MS    300u
#define APP_AUTO_EXIT_FORWARD_1_MS     3000u
#define APP_AUTO_EXIT_TURN_1_MS        2500u
#define APP_AUTO_EXIT_REVERSE_1_MS     1800u
#define APP_AUTO_EXIT_TURN_2_MS        2700u
#define APP_AUTO_EXIT_REVERSE_TURN_MS  2400u
#define APP_AUTO_EXIT_TURN_3_MS        3000u
#define APP_AUTO_EXIT_FORWARD_2_MS     500u
#define APP_AUTO_EXIT_FINAL_STOP_MS    700u

#define APP_AUTO_EXIT_START_STOP_MS          300u

#define APP_AUTO_EXIT_FRONT_HARD_STOP_MM     250u
#define APP_AUTO_EXIT_FRONT_BLOCKED_MM       350u
#define APP_AUTO_EXIT_REAR_HARD_STOP_MM      250u

#define APP_AUTO_EXIT_SIDE_SAFE_MM           450u
#define APP_AUTO_EXIT_SIDE_MIN_MM            220u
#define APP_AUTO_EXIT_SIDE_TILT_DIFF_MM      50u

#define APP_AUTO_EXIT_SIDE_FRONT_CAUTION_MM  450u
#define APP_AUTO_EXIT_SIDE_REAR_CAUTION_MM   400u
#define APP_AUTO_EXIT_SIDE_BLOCKED_MM        180u

#define APP_AUTO_EXIT_AVOID_ESCAPE_MIN_MS    250u
#define APP_AUTO_EXIT_AVOID_ESCAPE_SHORT_MS  700u
#define APP_AUTO_EXIT_AVOID_ESCAPE_LONG_MS   1500u

#define APP_AUTO_EXIT_AVOID_REALIGN_SHORT_MS 700u
#define APP_AUTO_EXIT_AVOID_REALIGN_LONG_MS  1800u

#define APP_AUTO_EXIT_REALIGN_LEFT_STEER     64u
#define APP_AUTO_EXIT_REALIGN_RIGHT_STEER    191u

#define APP_AUTO_EXIT_AVOID_ESCAPE_FORWARD_RATIO_PERCENT  60u
#define APP_AUTO_EXIT_AVOID_REALIGN_FORWARD_RATIO_PERCENT 60u

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

typedef enum
{
    APP_AUTO_EXIT_DIR_STRAIGHT = 0,
    APP_AUTO_EXIT_DIR_LEFT,
    APP_AUTO_EXIT_DIR_RIGHT
} AppAutoExitDirection;

typedef enum
{
    APP_AUTO_EXIT_STRATEGY_NORMAL = 0,
    APP_AUTO_EXIT_STRATEGY_AVOID_AND_RESUME,
    APP_AUTO_EXIT_STRATEGY_BLOCKED
} AppAutoExitStrategy;

typedef enum
{
    APP_AUTO_EXIT_AVOID_NONE = 0,
    APP_AUTO_EXIT_AVOID_SHORT,
    APP_AUTO_EXIT_AVOID_LONG
} AppAutoExitAvoidLevel;

typedef struct
{
    uint16 frontMm;
    uint16 rearMm;
    uint16 minMm;
    boolean isSafe;
    boolean isFrontCloser;
    boolean isRearCloser;
} AppAutoExitSideInfo;

typedef struct
{
    uint8 driveCmd;
    uint8 steeringCmd;
    uint32 durationMs;
} AppAutoExitMotionStep;

static const AppAutoExitMotionStep g_profileStraight[] =
{
    { APP_AUTO_EXIT_DRIVE_FORWARD, APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_FORWARD_1_MS },
    { APP_AUTO_EXIT_DRIVE_STOP,    APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_FINAL_STOP_MS }
};

static const AppAutoExitMotionStep g_profileRight[] =
{
    { APP_AUTO_EXIT_DRIVE_FORWARD, APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_FORWARD_1_MS },
    { APP_AUTO_EXIT_DRIVE_FORWARD, APP_AUTO_EXIT_STEER_RIGHT,  APP_AUTO_EXIT_TURN_1_MS },
    { APP_AUTO_EXIT_DRIVE_STOP,    APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_SHIFT_STOP_MS },
    { APP_AUTO_EXIT_DRIVE_REVERSE, APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_REVERSE_1_MS },
    { APP_AUTO_EXIT_DRIVE_STOP,    APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_SHIFT_STOP_MS },
    { APP_AUTO_EXIT_DRIVE_FORWARD, APP_AUTO_EXIT_STEER_RIGHT,  APP_AUTO_EXIT_TURN_2_MS },
    { APP_AUTO_EXIT_DRIVE_STOP,    APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_SHIFT_STOP_MS },
    { APP_AUTO_EXIT_DRIVE_REVERSE, APP_AUTO_EXIT_STEER_LEFT,   APP_AUTO_EXIT_REVERSE_TURN_MS },
    { APP_AUTO_EXIT_DRIVE_STOP,    APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_SHIFT_STOP_MS },
    { APP_AUTO_EXIT_DRIVE_FORWARD, APP_AUTO_EXIT_STEER_RIGHT,  APP_AUTO_EXIT_TURN_3_MS },
    { APP_AUTO_EXIT_DRIVE_FORWARD, APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_FORWARD_2_MS },
    { APP_AUTO_EXIT_DRIVE_STOP,    APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_FINAL_STOP_MS }
};

static const AppAutoExitMotionStep g_profileLeft[] =
{
    { APP_AUTO_EXIT_DRIVE_FORWARD, APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_FORWARD_1_MS },
    { APP_AUTO_EXIT_DRIVE_FORWARD, APP_AUTO_EXIT_STEER_LEFT,   APP_AUTO_EXIT_TURN_1_MS },
    { APP_AUTO_EXIT_DRIVE_STOP,    APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_SHIFT_STOP_MS },
    { APP_AUTO_EXIT_DRIVE_REVERSE, APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_REVERSE_1_MS },
    { APP_AUTO_EXIT_DRIVE_STOP,    APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_SHIFT_STOP_MS },
    { APP_AUTO_EXIT_DRIVE_FORWARD, APP_AUTO_EXIT_STEER_LEFT,   APP_AUTO_EXIT_TURN_2_MS },
    { APP_AUTO_EXIT_DRIVE_STOP,    APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_SHIFT_STOP_MS },
    { APP_AUTO_EXIT_DRIVE_REVERSE, APP_AUTO_EXIT_STEER_RIGHT,  APP_AUTO_EXIT_REVERSE_TURN_MS },
    { APP_AUTO_EXIT_DRIVE_STOP,    APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_SHIFT_STOP_MS },
    { APP_AUTO_EXIT_DRIVE_FORWARD, APP_AUTO_EXIT_STEER_LEFT,   APP_AUTO_EXIT_TURN_3_MS },
    { APP_AUTO_EXIT_DRIVE_FORWARD, APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_FORWARD_2_MS },
    { APP_AUTO_EXIT_DRIVE_STOP,    APP_AUTO_EXIT_STEER_CENTER, APP_AUTO_EXIT_FINAL_STOP_MS }
};

#define APP_AUTO_EXIT_PROFILE_STRAIGHT_COUNT \
    (sizeof(g_profileStraight) / sizeof(g_profileStraight[0]))

#define APP_AUTO_EXIT_PROFILE_RIGHT_COUNT \
    (sizeof(g_profileRight) / sizeof(g_profileRight[0]))

#define APP_AUTO_EXIT_PROFILE_LEFT_COUNT \
    (sizeof(g_profileLeft) / sizeof(g_profileLeft[0]))

static boolean g_autoExitActive = FALSE;
static boolean g_autoExitCmdValid = FALSE;
static VehicleControlCmd_t g_autoExitCmd;

static AppAutoExitState g_autoExitState = APP_AUTO_EXIT_STATE_IDLE;

static const AppAutoExitMotionStep *g_activeProfile = 0;
static uint32 g_activeProfileCount = 0u;
static uint32 g_activeStepIndex = 0u;
static TickType_t g_stepStartTick = 0u;

static AppAutoExitDirection g_exitDirection = APP_AUTO_EXIT_DIR_STRAIGHT;
static TickType_t g_stateStartTick = 0u;

static AppAutoExitAvoidLevel g_avoidLevel = APP_AUTO_EXIT_AVOID_NONE;
static uint32 g_avoidEscapeMs = 0u;
static uint32 g_avoidRealignMs = 0u;
static TickType_t g_avoidEscapeStartTick = 0u;
static TickType_t g_avoidRealignStartTick = 0u;
static uint32 g_avoidEscapeElapsedMs = 0u;

static uint32 g_firstStepReductionMs = 0u;

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

static boolean AppAutoExitService_HasElapsed(TickType_t startTick, uint32 durationMs)
{
    TickType_t nowTick;

    nowTick = xTaskGetTickCount();

    if((nowTick - startTick) >= pdMS_TO_TICKS(durationMs))
    {
        return TRUE;
    }

    return FALSE;
}

static uint32 AppAutoExitService_GetElapsedMs(TickType_t startTick)
{
    TickType_t elapsedTick;

    elapsedTick = xTaskGetTickCount() - startTick;

    return (uint32)((elapsedTick * 1000u) / configTICK_RATE_HZ);
}

static void AppAutoExitService_SetCommand(uint8 driveCmd, uint8 steeringCmd)
{
    g_autoExitCmd.driveCmd = driveCmd;
    g_autoExitCmd.steeringCmd = steeringCmd;
    g_autoExitCmdValid = TRUE;
}

static void AppAutoExitService_EnterIdle(void)
{
    g_autoExitActive = FALSE;
    g_autoExitCmdValid = FALSE;

    g_autoExitState = APP_AUTO_EXIT_STATE_IDLE;

    g_activeProfile = 0;
    g_activeProfileCount = 0u;
    g_activeStepIndex = 0u;
    g_firstStepReductionMs = 0u;
}

static void AppAutoExitService_EnterBlocked(void)
{
    g_autoExitActive = TRUE;
    g_autoExitState = APP_AUTO_EXIT_STATE_BLOCKED;
    g_stateStartTick = xTaskGetTickCount();

    AppAutoExitService_SetCommand(APP_AUTO_EXIT_DRIVE_STOP,
                                  APP_AUTO_EXIT_STEER_CENTER);
}

static void AppAutoExitService_EnterStopped(void)
{
    g_autoExitActive = TRUE;
    g_autoExitState = APP_AUTO_EXIT_STATE_STOPPED;
    g_stateStartTick = xTaskGetTickCount();

    AppAutoExitService_SetCommand(APP_AUTO_EXIT_DRIVE_STOP,
                                  APP_AUTO_EXIT_STEER_CENTER);
}

static void AppAutoExitService_SetStopCommand(void)
{
    g_autoExitCmd.driveCmd = APP_AUTO_EXIT_DRIVE_STOP;
    g_autoExitCmd.steeringCmd = APP_AUTO_EXIT_STEER_CENTER;
    g_autoExitCmdValid = TRUE;
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

    g_activeProfile = profile;
    g_activeProfileCount = profileCount;
    g_activeStepIndex = 0u;
    g_firstStepReductionMs = firstStepReductionMs;
    g_stepStartTick = xTaskGetTickCount();

    g_autoExitActive = TRUE;
    g_autoExitCmdValid = TRUE;
    g_autoExitState = APP_AUTO_EXIT_STATE_RUN_PROFILE;

    AppAutoExitService_SetCommand(g_activeProfile[0].driveCmd,
                                  g_activeProfile[0].steeringCmd);
}

static void AppAutoExitService_StopProfile(void)
{
    g_activeProfile = 0;
    g_activeProfileCount = 0u;
    g_activeStepIndex = 0u;

    g_autoExitActive = FALSE;
    g_autoExitCmdValid = FALSE;
    g_autoExitState = APP_AUTO_EXIT_STATE_IDLE;

    AppAutoExitService_SetStopCommand();
}

static uint32 AppAutoExitService_GetCurrentStepDurationMs(void)
{
    uint32 durationMs;

    durationMs = g_activeProfile[g_activeStepIndex].durationMs;

    if((g_activeStepIndex == 0u) && (g_firstStepReductionMs > 0u))
    {
        if(g_firstStepReductionMs >= durationMs)
        {
            durationMs = 100u;
        }
        else
        {
            durationMs = durationMs - g_firstStepReductionMs;
        }
    }

    return durationMs;
}


static boolean AppAutoExitService_IsStepSafetyDanger(const AppAutoExitMotionStep *step)
{
    AppUltrasonicState ultrasonic;

    if(step == 0)
    {
        return FALSE;
    }

    if(AppRxService_GetUltrasonicState(&ultrasonic) != pdPASS)
    {
        /*
         * 현재 테스트 단계에서는 센서가 없어도 시간 기반 프로파일이 돌 수 있게 FALSE.
         * 실차 단계에서는 TRUE 또는 BLOCKED로 바꾸는 게 안전함.
         */
        return FALSE;
    }

    if(step->driveCmd < APP_AUTO_EXIT_DRIVE_STOP)
    {
        if(ultrasonic.distanceMm[APP_PDW_DIR_FRONT] < APP_AUTO_EXIT_FRONT_HARD_STOP_MM)
        {
            return TRUE;
        }

        if(ultrasonic.distanceMm[APP_PDW_DIR_FRONT_LEFT] < APP_AUTO_EXIT_FRONT_HARD_STOP_MM)
        {
            return TRUE;
        }

        if(ultrasonic.distanceMm[APP_PDW_DIR_FRONT_RIGHT] < APP_AUTO_EXIT_FRONT_HARD_STOP_MM)
        {
            return TRUE;
        }
    }
    else if(step->driveCmd > APP_AUTO_EXIT_DRIVE_STOP)
    {
        if(ultrasonic.distanceMm[APP_PDW_DIR_BEHIND] < APP_AUTO_EXIT_REAR_HARD_STOP_MM)
        {
            return TRUE;
        }

        if(ultrasonic.distanceMm[APP_PDW_DIR_BEHIND_LEFT] < APP_AUTO_EXIT_REAR_HARD_STOP_MM)
        {
            return TRUE;
        }

        if(ultrasonic.distanceMm[APP_PDW_DIR_BEHIND_RIGHT] < APP_AUTO_EXIT_REAR_HARD_STOP_MM)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static void AppAutoExitService_ServiceProfile(void)
{

    if(AppAutoExitService_IsStepSafetyDanger(&g_activeProfile[g_activeStepIndex]) == TRUE)
    {
        AppAutoExitService_EnterBlocked();
        return;
    }

    if(g_autoExitState != APP_AUTO_EXIT_STATE_RUN_PROFILE)
    {
        return;
    }

    if((g_activeProfile == 0) || (g_activeStepIndex >= g_activeProfileCount))
    {
        AppAutoExitService_StopProfile();
        return;
    }

    if(AppAutoExitService_HasElapsed(g_stepStartTick, AppAutoExitService_GetCurrentStepDurationMs()) == FALSE)
    {
        return;
    }

    g_activeStepIndex++;

    if(g_activeStepIndex >= g_activeProfileCount)
    {
        AppAutoExitService_StopProfile();
        return;
    }

    g_stepStartTick = xTaskGetTickCount();

    g_autoExitCmd.driveCmd = g_activeProfile[g_activeStepIndex].driveCmd;
    g_autoExitCmd.steeringCmd = g_activeProfile[g_activeStepIndex].steeringCmd;
    g_autoExitCmdValid = TRUE;
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

static AppAutoExitSideInfo AppAutoExitService_MakeSideInfo(uint16 frontMm,
                                                           uint16 rearMm)
{
    AppAutoExitSideInfo info;
    sint16 diffMm;

    info.frontMm = frontMm;
    info.rearMm = rearMm;
    info.minMm = (frontMm < rearMm) ? frontMm : rearMm;

    info.isSafe = ((frontMm > APP_AUTO_EXIT_SIDE_SAFE_MM) &&
                   (rearMm > APP_AUTO_EXIT_SIDE_SAFE_MM)) ? TRUE : FALSE;

    diffMm = (sint16)frontMm - (sint16)rearMm;

    if(diffMm < -(sint16)APP_AUTO_EXIT_SIDE_TILT_DIFF_MM)
    {
        info.isFrontCloser = TRUE;
        info.isRearCloser = FALSE;
    }
    else if(diffMm > (sint16)APP_AUTO_EXIT_SIDE_TILT_DIFF_MM)
    {
        info.isFrontCloser = FALSE;
        info.isRearCloser = TRUE;
    }
    else
    {
        info.isFrontCloser = FALSE;
        info.isRearCloser = FALSE;
    }

    return info;
}

static AppAutoExitAvoidLevel AppAutoExitService_GetAvoidLevel(const AppAutoExitSideInfo *exitSide)
{
    if(exitSide == 0)
    {
        return APP_AUTO_EXIT_AVOID_LONG;
    }

    if(exitSide->minMm < APP_AUTO_EXIT_SIDE_BLOCKED_MM)
    {
        return APP_AUTO_EXIT_AVOID_LONG;
    }

    if(exitSide->frontMm < APP_AUTO_EXIT_SIDE_FRONT_CAUTION_MM)
    {
        return APP_AUTO_EXIT_AVOID_LONG;
    }

    if(exitSide->rearMm < APP_AUTO_EXIT_SIDE_REAR_CAUTION_MM)
    {
        return APP_AUTO_EXIT_AVOID_SHORT;
    }

    if((exitSide->isFrontCloser == TRUE) &&
       (exitSide->frontMm < APP_AUTO_EXIT_SIDE_SAFE_MM))
    {
        return APP_AUTO_EXIT_AVOID_LONG;
    }

    if((exitSide->isRearCloser == TRUE) &&
       (exitSide->rearMm < APP_AUTO_EXIT_SIDE_SAFE_MM))
    {
        return APP_AUTO_EXIT_AVOID_SHORT;
    }

    return APP_AUTO_EXIT_AVOID_NONE;
}

static void AppAutoExitService_ApplyAvoidLevel(AppAutoExitAvoidLevel level)
{
    g_avoidLevel = level;

    if(level == APP_AUTO_EXIT_AVOID_LONG)
    {
        g_avoidEscapeMs = APP_AUTO_EXIT_AVOID_ESCAPE_LONG_MS;
        g_avoidRealignMs = APP_AUTO_EXIT_AVOID_REALIGN_LONG_MS;
    }
    else if(level == APP_AUTO_EXIT_AVOID_SHORT)
    {
        g_avoidEscapeMs = APP_AUTO_EXIT_AVOID_ESCAPE_SHORT_MS;
        g_avoidRealignMs = APP_AUTO_EXIT_AVOID_REALIGN_SHORT_MS;
    }
    else
    {
        g_avoidEscapeMs = 0u;
        g_avoidRealignMs = 0u;
    }
}

static AppAutoExitStrategy AppAutoExitService_SelectStrategy(void)
{
    AppUltrasonicState ultrasonic;
    AppAutoExitSideInfo exitSide;
    AppAutoExitSideInfo oppositeSide;
    AppAutoExitAvoidLevel avoidLevel;
    uint16 exitFrontCornerMm;

    if(g_exitDirection == APP_AUTO_EXIT_DIR_STRAIGHT)
    {
        return APP_AUTO_EXIT_STRATEGY_NORMAL;
    }

    if(AppRxService_GetUltrasonicState(&ultrasonic) != pdPASS)
    {
        /*
         * 테스트 단계: 센서 없으면 NORMAL 허용.
         * 실차 단계: BLOCKED 권장.
         */
        AppAutoExitService_ApplyAvoidLevel(APP_AUTO_EXIT_AVOID_NONE);
        return APP_AUTO_EXIT_STRATEGY_NORMAL;
    }

    if(g_exitDirection == APP_AUTO_EXIT_DIR_LEFT)
    {
        exitSide = AppAutoExitService_MakeSideInfo(
            ultrasonic.distanceMm[APP_PDW_DIR_LEFT_FRONT],
            ultrasonic.distanceMm[APP_PDW_DIR_LEFT_BEHIND]);

        oppositeSide = AppAutoExitService_MakeSideInfo(
            ultrasonic.distanceMm[APP_PDW_DIR_RIGHT_FRONT],
            ultrasonic.distanceMm[APP_PDW_DIR_RIGHT_BEHIND]);

        exitFrontCornerMm = ultrasonic.distanceMm[APP_PDW_DIR_FRONT_LEFT];
    }
    else
    {
        exitSide = AppAutoExitService_MakeSideInfo(
            ultrasonic.distanceMm[APP_PDW_DIR_RIGHT_FRONT],
            ultrasonic.distanceMm[APP_PDW_DIR_RIGHT_BEHIND]);

        oppositeSide = AppAutoExitService_MakeSideInfo(
            ultrasonic.distanceMm[APP_PDW_DIR_LEFT_FRONT],
            ultrasonic.distanceMm[APP_PDW_DIR_LEFT_BEHIND]);

        exitFrontCornerMm = ultrasonic.distanceMm[APP_PDW_DIR_FRONT_RIGHT];
    }

    if(ultrasonic.distanceMm[APP_PDW_DIR_FRONT] < APP_AUTO_EXIT_FRONT_BLOCKED_MM)
    {
        return APP_AUTO_EXIT_STRATEGY_BLOCKED;
    }

    if(exitFrontCornerMm < APP_AUTO_EXIT_FRONT_BLOCKED_MM)
    {
        return APP_AUTO_EXIT_STRATEGY_BLOCKED;
    }

    avoidLevel = AppAutoExitService_GetAvoidLevel(&exitSide);

    if((avoidLevel == APP_AUTO_EXIT_AVOID_NONE) &&
       (exitSide.isSafe == TRUE))
    {
        AppAutoExitService_ApplyAvoidLevel(APP_AUTO_EXIT_AVOID_NONE);
        return APP_AUTO_EXIT_STRATEGY_NORMAL;
    }

    if(oppositeSide.isSafe == TRUE)
    {
        if(avoidLevel == APP_AUTO_EXIT_AVOID_NONE)
        {
            avoidLevel = APP_AUTO_EXIT_AVOID_SHORT;
        }

        AppAutoExitService_ApplyAvoidLevel(avoidLevel);
        return APP_AUTO_EXIT_STRATEGY_AVOID_AND_RESUME;
    }

    return APP_AUTO_EXIT_STRATEGY_BLOCKED;
}

static uint8 AppAutoExitService_GetEscapeSteer(void)
{
    if(g_exitDirection == APP_AUTO_EXIT_DIR_LEFT)
    {
        return APP_AUTO_EXIT_STEER_RIGHT;
    }

    return APP_AUTO_EXIT_STEER_LEFT;
}

static uint8 AppAutoExitService_GetRealignSteer(void)
{
    if(g_exitDirection == APP_AUTO_EXIT_DIR_LEFT)
    {
        return APP_AUTO_EXIT_REALIGN_LEFT_STEER;
    }

    return APP_AUTO_EXIT_REALIGN_RIGHT_STEER;
}

static boolean AppAutoExitService_IsOppositeSideDangerDuringAvoid(void)
{
    AppUltrasonicState ultrasonic;

    if(AppRxService_GetUltrasonicState(&ultrasonic) != pdPASS)
    {
        return FALSE;
    }

    if(g_exitDirection == APP_AUTO_EXIT_DIR_LEFT)
    {
        if(ultrasonic.distanceMm[APP_PDW_DIR_RIGHT_FRONT] < APP_AUTO_EXIT_SIDE_MIN_MM)
        {
            return TRUE;
        }

        if(ultrasonic.distanceMm[APP_PDW_DIR_RIGHT_BEHIND] < APP_AUTO_EXIT_SIDE_MIN_MM)
        {
            return TRUE;
        }

        if(ultrasonic.distanceMm[APP_PDW_DIR_FRONT_RIGHT] < APP_AUTO_EXIT_FRONT_HARD_STOP_MM)
        {
            return TRUE;
        }
    }
    else
    {
        if(ultrasonic.distanceMm[APP_PDW_DIR_LEFT_FRONT] < APP_AUTO_EXIT_SIDE_MIN_MM)
        {
            return TRUE;
        }

        if(ultrasonic.distanceMm[APP_PDW_DIR_LEFT_BEHIND] < APP_AUTO_EXIT_SIDE_MIN_MM)
        {
            return TRUE;
        }

        if(ultrasonic.distanceMm[APP_PDW_DIR_FRONT_LEFT] < APP_AUTO_EXIT_FRONT_HARD_STOP_MM)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static uint32 AppAutoExitService_CalcFirstStepReductionMs(uint32 escapeMs,
                                                          uint32 realignMs)
{
    uint32 reductionMs;

    reductionMs =
        ((escapeMs * APP_AUTO_EXIT_AVOID_ESCAPE_FORWARD_RATIO_PERCENT) / 100u) +
        ((realignMs * APP_AUTO_EXIT_AVOID_REALIGN_FORWARD_RATIO_PERCENT) / 100u);

    if(reductionMs >= APP_AUTO_EXIT_FORWARD_1_MS)
    {
        return APP_AUTO_EXIT_FORWARD_1_MS - 100u;
    }

    return reductionMs;
}

static void AppAutoExitService_StartAvoidEscape(void)
{
    g_avoidEscapeStartTick = xTaskGetTickCount();

    g_autoExitState = APP_AUTO_EXIT_STATE_AVOID_ESCAPE;

    AppAutoExitService_SetCommand(APP_AUTO_EXIT_DRIVE_FORWARD,
                                  AppAutoExitService_GetEscapeSteer());
}

static void AppAutoExitService_FinishAvoidEscape(void)
{
    g_avoidEscapeElapsedMs =
        AppAutoExitService_GetElapsedMs(g_avoidEscapeStartTick);

    g_autoExitState = APP_AUTO_EXIT_STATE_AVOID_STOP_1;
    g_stateStartTick = xTaskGetTickCount();

    AppAutoExitService_SetCommand(APP_AUTO_EXIT_DRIVE_STOP,
                                  APP_AUTO_EXIT_STEER_CENTER);
}

static void AppAutoExitService_StartAvoidRealign(void)
{
    g_avoidRealignStartTick = xTaskGetTickCount();

    g_autoExitState = APP_AUTO_EXIT_STATE_AVOID_REALIGN;

    AppAutoExitService_SetCommand(APP_AUTO_EXIT_DRIVE_FORWARD,
                                  AppAutoExitService_GetRealignSteer());
}

static void AppAutoExitService_FinishAvoidRealign(void)
{
    g_autoExitState = APP_AUTO_EXIT_STATE_AVOID_STOP_2;
    g_stateStartTick = xTaskGetTickCount();

    AppAutoExitService_SetCommand(APP_AUTO_EXIT_DRIVE_STOP,
                                  APP_AUTO_EXIT_STEER_CENTER);
}

static void AppAutoExitService_StartAutoExit(AppAutoExitDirection dir)
{
    if(g_autoExitState != APP_AUTO_EXIT_STATE_IDLE)
    {
        return;
    }

    g_exitDirection = dir;
    g_autoExitActive = TRUE;
    g_autoExitCmdValid = TRUE;

    if(dir == APP_AUTO_EXIT_DIR_STRAIGHT)
    {
        AppAutoExitService_StartProfile(g_profileStraight,
                                        APP_AUTO_EXIT_PROFILE_STRAIGHT_COUNT,
                                        0u);
        return;
    }

    g_autoExitState = APP_AUTO_EXIT_STATE_START_STOP;
    g_stateStartTick = xTaskGetTickCount();

    AppAutoExitService_SetCommand(APP_AUTO_EXIT_DRIVE_STOP,
                                  APP_AUTO_EXIT_STEER_CENTER);
}

static void AppAutoExitService_ServiceState(void)
{
    AppAutoExitStrategy strategy;
    uint32 firstStepReductionMs;

    switch(g_autoExitState)
    {
        case APP_AUTO_EXIT_STATE_IDLE:
            break;

        case APP_AUTO_EXIT_STATE_START_STOP:
            if(AppAutoExitService_HasElapsed(g_stateStartTick,
                                             APP_AUTO_EXIT_START_STOP_MS) == TRUE)
            {
                g_autoExitState = APP_AUTO_EXIT_STATE_SELECT_STRATEGY;
            }
            break;

        case APP_AUTO_EXIT_STATE_SELECT_STRATEGY:
            strategy = AppAutoExitService_SelectStrategy();

            if(strategy == APP_AUTO_EXIT_STRATEGY_NORMAL)
            {
                if(g_exitDirection == APP_AUTO_EXIT_DIR_LEFT)
                {
                    AppAutoExitService_StartProfile(g_profileLeft,
                                                    APP_AUTO_EXIT_PROFILE_LEFT_COUNT,
                                                    0u);
                }
                else
                {
                    AppAutoExitService_StartProfile(g_profileRight,
                                                    APP_AUTO_EXIT_PROFILE_RIGHT_COUNT,
                                                    0u);
                }
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
            if(AppAutoExitService_IsOppositeSideDangerDuringAvoid() == TRUE)
            {
                if(AppAutoExitService_HasElapsed(g_avoidEscapeStartTick,
                                                 APP_AUTO_EXIT_AVOID_ESCAPE_MIN_MS) == FALSE)
                {
                    AppAutoExitService_EnterBlocked();
                }
                else
                {
                    AppAutoExitService_FinishAvoidEscape();
                }
            }
            else if(AppAutoExitService_HasElapsed(g_avoidEscapeStartTick,
                                                  g_avoidEscapeMs) == TRUE)
            {
                AppAutoExitService_FinishAvoidEscape();
            }
            break;

        case APP_AUTO_EXIT_STATE_AVOID_STOP_1:
            if(AppAutoExitService_HasElapsed(g_stateStartTick,
                                             APP_AUTO_EXIT_SHIFT_STOP_MS) == TRUE)
            {
                AppAutoExitService_StartAvoidRealign();
            }
            break;

        case APP_AUTO_EXIT_STATE_AVOID_REALIGN:
            if(AppAutoExitService_HasElapsed(g_avoidRealignStartTick,
                                             g_avoidRealignMs) == TRUE)
            {
                AppAutoExitService_FinishAvoidRealign();
            }
            break;

        case APP_AUTO_EXIT_STATE_AVOID_STOP_2:
            if(AppAutoExitService_HasElapsed(g_stateStartTick,
                                             APP_AUTO_EXIT_SHIFT_STOP_MS) == TRUE)
            {
                firstStepReductionMs =
                    AppAutoExitService_CalcFirstStepReductionMs(g_avoidEscapeElapsedMs,
                                                                g_avoidRealignMs);

                if(g_exitDirection == APP_AUTO_EXIT_DIR_LEFT)
                {
                    AppAutoExitService_StartProfile(g_profileLeft,
                                                    APP_AUTO_EXIT_PROFILE_LEFT_COUNT,
                                                    firstStepReductionMs);
                }
                else
                {
                    AppAutoExitService_StartProfile(g_profileRight,
                                                    APP_AUTO_EXIT_PROFILE_RIGHT_COUNT,
                                                    firstStepReductionMs);
                }
            }
            break;

        case APP_AUTO_EXIT_STATE_RUN_PROFILE:
            AppAutoExitService_ServiceProfile();
            break;

        case APP_AUTO_EXIT_STATE_BLOCKED:
            if(AppAutoExitService_HasElapsed(g_stateStartTick,
                                             APP_AUTO_EXIT_FINAL_STOP_MS) == TRUE)
            {
                AppAutoExitService_EnterIdle();
            }
            break;

        case APP_AUTO_EXIT_STATE_STOPPED:
            if(AppAutoExitService_HasElapsed(g_stateStartTick,
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

void AppAutoExitService_Task(void *arg)
{
    TickType_t lastWakeTime;
    AppAutoParkingState autoParking;
    static AppAutoExitCmd prevCmd = APP_AUTO_EXIT_CMD_NORMAL;

    (void)arg;

    lastWakeTime = xTaskGetTickCount();

    for(;;)
    {
        if(AppRxService_GetAutoParkingState(&autoParking) == pdPASS)
        {
            if(autoParking.cmd != prevCmd)
            {
                switch(autoParking.cmd)
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
                        AppAutoExitService_EnterStopped();
                        break;

                    case APP_AUTO_EXIT_CMD_NORMAL:
                    default:
                        break;
                }

                prevCmd = autoParking.cmd;
            }
        }

        AppAutoExitService_ServiceState();

        vTaskDelayUntil(&lastWakeTime,
                        pdMS_TO_TICKS(APP_AUTO_EXIT_SERVICE_PERIOD_MS));
    }
}
