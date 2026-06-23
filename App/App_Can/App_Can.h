/*
 * App_Can.h
 *
 *  Created on: 2026. 6. 24.
 *      Author: USER
 */

#ifndef APP_APP_CAN_APP_CAN_H_
#define APP_APP_CAN_APP_CAN_H_

#include "FreeRTOS.h"
#include "App_Types.h"
#include "CanMsg.h"

void AppCan_Init(void);
void AppCan_RxTask(void *arg);

BaseType_t AppCan_GetLatestUltrasonic(UltrasonicDistanceCmd_t *out);
BaseType_t AppCan_GetLatestVehicleStatus(VehicleStatusCmd_t *out);
BaseType_t AppCan_GetLatestAutoParking(AutoParkingCmd_t *out);

BaseType_t AppCan_SendDistanceLevel(const DistanceLevelCmd_t *cmd);
BaseType_t AppCan_SendExitComplete(const ExitCompleteCmd_t *cmd);
BaseType_t AppCan_SendVehicleControl(const VehicleControlCmd_t *cmd);

#endif /* APP_APP_CAN_APP_CAN_H_ */
