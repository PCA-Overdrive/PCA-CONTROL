#include "App_Can.h"

#include "McmcanFd.h"
#include "queue.h"
#include "task.h"

#define APP_CAN_RX_TASK_PERIOD_MS      (1u)

static QueueHandle_t g_ultrasonicMailbox;
static QueueHandle_t g_vehicleStatusMailbox;
static QueueHandle_t g_autoParkingMailbox;

void AppCan_Init(void)
{
    McmcanFd_Init();

    g_ultrasonicMailbox = xQueueCreate(1u, sizeof(UltrasonicDistanceCmd_t));
    g_vehicleStatusMailbox = xQueueCreate(1u, sizeof(VehicleStatusCmd_t));
    g_autoParkingMailbox = xQueueCreate(1u, sizeof(AutoParkingCmd_t));
}

void AppCan_RxTask(void *arg)
{
    UltrasonicDistanceCmd_t ultrasonic;
    VehicleStatusCmd_t vehicleStatus;
    AutoParkingCmd_t autoParking;

    (void)arg;

    for(;;)
    {
        if(McmcanFd_RecvUltrasonic(&ultrasonic) == TRUE)
        {
            (void)xQueueOverwrite(g_ultrasonicMailbox, &ultrasonic);
        }

        if(McmcanFd_RecvVehicleStatus(&vehicleStatus) == TRUE)
        {
            (void)xQueueOverwrite(g_vehicleStatusMailbox, &vehicleStatus);
        }

        if(McmcanFd_RecvAutoParking(&autoParking) == TRUE)
        {
            (void)xQueueOverwrite(g_autoParkingMailbox, &autoParking);
        }

        vTaskDelay(pdMS_TO_TICKS(APP_CAN_RX_TASK_PERIOD_MS));
    }
}

BaseType_t AppCan_GetLatestUltrasonic(UltrasonicDistanceCmd_t *out)
{
    if((g_ultrasonicMailbox == NULL) || (out == NULL))
    {
        return pdFAIL;
    }

    return xQueuePeek(g_ultrasonicMailbox, out, 0u);
}

BaseType_t AppCan_GetLatestVehicleStatus(VehicleStatusCmd_t *out)
{
    if((g_vehicleStatusMailbox == NULL) || (out == NULL))
    {
        return pdFAIL;
    }

    return xQueuePeek(g_vehicleStatusMailbox, out, 0u);
}

BaseType_t AppCan_GetLatestAutoParking(AutoParkingCmd_t *out)
{
    if((g_autoParkingMailbox == NULL) || (out == NULL))
    {
        return pdFAIL;
    }

    return xQueuePeek(g_autoParkingMailbox, out, 0u);
}

BaseType_t AppCan_SendDistanceLevel(const DistanceLevelCmd_t *cmd)
{
    if(cmd == NULL)
    {
        return pdFAIL;
    }

    McmcanFd_SendDistanceLevel(cmd);
    return pdPASS;
}

BaseType_t AppCan_SendExitComplete(const ExitCompleteCmd_t *cmd)
{
    if(cmd == NULL)
    {
        return pdFAIL;
    }

    McmcanFd_SendExitComplete(cmd);
    return pdPASS;
}

BaseType_t AppCan_SendVehicleControl(const VehicleControlCmd_t *cmd)
{
    if(cmd == NULL)
    {
        return pdFAIL;
    }

    McmcanFd_SendVehicleControl(cmd);
    return pdPASS;
}
