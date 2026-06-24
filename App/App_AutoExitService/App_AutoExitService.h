#ifndef APP_AUTO_EXIT_SERVICE_H
#define APP_AUTO_EXIT_SERVICE_H

#include "FreeRTOS.h"
#include "App_Types.h"
#include "CanMsg.h"

void AppAutoExitService_Init(void);
void AppAutoExitService_Task(void *arg);

BaseType_t AppAutoExitService_GetControlCommand(VehicleControlCmd_t *cmd);
boolean AppAutoExitService_IsActive(void);

#endif /* APP_AUTO_EXIT_SERVICE_H */
