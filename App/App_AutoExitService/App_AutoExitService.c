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

typedef enum
{
    APP_AUTO_EXIT_STATE_IDLE = 0,
    APP_AUTO_EXIT_STATE_RUN_PROFILE,
    APP_AUTO_EXIT_STATE_DONE,
    APP_AUTO_EXIT_STATE_STOPPED
} AppAutoExitState;

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

static void AppAutoExitService_SetStopCommand(void)
{
    g_autoExitCmd.driveCmd = APP_AUTO_EXIT_DRIVE_STOP;
    g_autoExitCmd.steeringCmd = APP_AUTO_EXIT_STEER_CENTER;
    g_autoExitCmdValid = TRUE;
}

static void AppAutoExitService_StartProfile(const AppAutoExitMotionStep *profile,
                                            uint32 profileCount)
{
    if((profile == 0) || (profileCount == 0u))
    {
        g_autoExitActive = FALSE;
        g_autoExitCmdValid = FALSE;
        g_autoExitState = APP_AUTO_EXIT_STATE_IDLE;
        AppAutoExitService_SetStopCommand();
        return;
    }

    g_activeProfile = profile;
    g_activeProfileCount = profileCount;
    g_activeStepIndex = 0u;
    g_stepStartTick = xTaskGetTickCount();

    g_autoExitActive = TRUE;
    g_autoExitCmdValid = TRUE;
    g_autoExitState = APP_AUTO_EXIT_STATE_RUN_PROFILE;

    g_autoExitCmd.driveCmd = g_activeProfile[0].driveCmd;
    g_autoExitCmd.steeringCmd = g_activeProfile[0].steeringCmd;
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

static void AppAutoExitService_ServiceProfile(void)
{
    if(g_autoExitState != APP_AUTO_EXIT_STATE_RUN_PROFILE)
    {
        return;
    }

    if((g_activeProfile == 0) || (g_activeStepIndex >= g_activeProfileCount))
    {
        AppAutoExitService_StopProfile();
        return;
    }

    if(AppAutoExitService_HasElapsed(g_stepStartTick,
                                     g_activeProfile[g_activeStepIndex].durationMs) == FALSE)
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
                        AppAutoExitService_StartProfile(g_profileStraight,
                                                         APP_AUTO_EXIT_PROFILE_STRAIGHT_COUNT);
                        break;

                    case APP_AUTO_EXIT_CMD_START_LEFT:
                        AppAutoExitService_StartProfile(g_profileLeft,
                                                         APP_AUTO_EXIT_PROFILE_LEFT_COUNT);
                        break;

                    case APP_AUTO_EXIT_CMD_START_RIGHT:
                        AppAutoExitService_StartProfile(g_profileRight,
                                                         APP_AUTO_EXIT_PROFILE_RIGHT_COUNT);
                        break;

                    case APP_AUTO_EXIT_CMD_STOP:
                        AppAutoExitService_StopProfile();
                        break;

                    case APP_AUTO_EXIT_CMD_NORMAL:
                    default:
                        break;
                }

                prevCmd = autoParking.cmd;
            }
        }

        AppAutoExitService_ServiceProfile();

        vTaskDelayUntil(&lastWakeTime,
                        pdMS_TO_TICKS(APP_AUTO_EXIT_SERVICE_PERIOD_MS));
    }
}
