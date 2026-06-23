/*
 * App_RxService.h
 *
 *  Created on: 2026. 6. 23.
 *      Author: USER
 */

#ifndef APP_APP_RXSERVICE_APP_RXSERVICE_H_
#define APP_APP_RXSERVICE_APP_RXSERVICE_H_
#include "FreeRTOS.h"
#include "App_Types.h"

BaseType_t AppRxService_GetRpiInput(AppRpiInputState *input);
BaseType_t AppRxService_GetUltrasonicState(AppUltrasonicState *state);
BaseType_t AppRxService_GetAutoParkingState(AppAutoParkingState *state);

#endif /* APP_APP_RXSERVICE_APP_RXSERVICE_H_ */
