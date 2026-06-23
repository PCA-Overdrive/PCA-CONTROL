#ifndef APP_APP_PDWSERVICE_APP_PDWSERVICE_H_
#define APP_APP_PDWSERVICE_APP_PDWSERVICE_H_

#include "FreeRTOS.h"
#include "../App_Types.h"

void AppPdwService_Init(void);
void AppPdwService_Task(void *arg);

void AppPdwService_Process(const AppRpiInputState *rpiInput,
                           const AppUltrasonicState *ultrasonic);

BaseType_t AppPdwService_GetState(AppPdwState *state);

#endif /* APP_APP_PDWSERVICE_APP_PDWSERVICE_H_ */
