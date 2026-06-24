# Control ECU App README

현재까지 구현된 범위

- CAN 담당자 `McmcanFd` 코드 통합
- FreeRTOS 기반 App task 생성 구조
- CAN Rx 최신값 mailbox 구조
- `0x200`, `0x201`, `0x300` 수신값 파싱해서 구조체에 저장(RxService)
- PDW 거리 단계 판단
- `0x400` PDW 상태 송신(0x401, 0x100은 해야 함)

TO-DO

- 출차 서비스 로직 통합
- Driving service
- `0x401` 출차 완료 송신 로직
- `0x100` 차량 제어 송신 로직
<img width="1536" height="1024" alt="ChatGPT Image 2026년 6월 24일 오전 04_55_08" src="https://github.com/user-attachments/assets/c5dd8e53-f8b3-4840-967b-d7f1861aade3" />


## Folder Structure

```text
App/
  App.c
  App.h
  App_Types.h

  App_Can/
    App_Can.h
    App_Can.c

  App_RxService/
    App_RxService.h
    App_RxService.c

  App_PdwService/
    App_PdwService.h
    App_PdwService.c

  App_StatusTxService/
    App_StatusTxService.h
    App_StatusTxService.c

Drivers/
  McmcanFd/
    CanMsg.h
    McmcanFd.h
    McmcanFd.c
    McmcanFd_Cfg.h
```

## Layer Responsibility

```text
Drivers/McmcanFd
  TC375 MCMCAN hardware driver
  CAN/CAN FD 실제 송수신 담당

App/App_Can
  McmcanFd wrapper
  Rx latest mailbox 관리
  Tx direct wrapper 제공

App/App_RxService
  CAN raw message를 App 내부 struct로 변환

App/App_PdwService
  초음파 거리값을 PDW level로 분류
  PDW enabled, dangerDetected 판단

App/App_StatusTxService
  0x400 payload 생성
  PDW 결과 + RPi 입력 echo 값을 모아 송신
```

## CAN Module Placement

MCM 코드는 Drivers안에 있어요!!

```text
Drivers/McmcanFd/
```
`McmcanFd_Cfg.h`에서 판단 ECU node 설정이 되어 있어야 합니다.

```c
#define NODE_JUDGMENT_ECU
```

그리고 PCAN 설정은 CAN 담당자 코드와 동일해야 합니다.

```text
Nominal bitrate: 500 kbit/s
Data bitrate:    2 Mbit/s
Mode:            CAN FD
```

## CAN Interface Summary

### Rx (구현 완료)

```text
0x200 Sensor ECU -> 판단 ECU
CAN FD, 24 bytes
초음파 거리 10개, IMU yaw, vehicleSpeed

0x201 RPi -> 판단 ECU
Classical CAN, 4 bytes
DriveCmd, SteeringCmd, GearStatusCmd, PDW Switch

0x300 RPi -> 판단 ECU
Classical CAN
Auto parking start/stop
```

### Tx (하나만 구현)

```text
0x400 판단 ECU -> RPi
CAN FD, 16 bytes
PDW level[10], PDW enabled, DriveCmd echo, GearStatus echo, PDW danger alarm

0x401 판단 ECU -> RPi
Classical CAN
출차 완료 여부
아직 출차 service 담당 구현 예정

0x100 판단 ECU -> 차량 제어 ECU
Classical CAN
속도/조향 제어 명령
아직 driving service 담당 구현 예정
```

## Rx Mailbox Policy

현재 Rx는 CAN ID별 latest mailbox 방식입니다.

```text
AppCan_RxTask
  McmcanFd_RecvUltrasonic()
    -> g_ultrasonicMailbox      // 0x200

  McmcanFd_RecvVehicleStatus()
    -> g_vehicleStatusMailbox   // 0x201

  McmcanFd_RecvAutoParking()
    -> g_autoParkingMailbox     // 0x300
```

각 mailbox는 length 1 queue입니다.

```c
xQueueCreate(1u, sizeof(...));
xQueueOverwrite(...);
xQueuePeek(...);
```

이 구조를 쓰는 이유:

- Rx 값은 과거 이력보다 최신 상태가 중요함
- PDW service와 출차 service가 같은 초음파 최신값을 동시에 읽을 수 있음
- `xQueueReceive()`가 아니라 `xQueuePeek()`을 사용하므로 한 service가 읽어도 값이 사라지지 않음

## Tx Policy

현재 Tx queue는 사용하지 않습니다.

이전 프로젝트 스타일에 맞춰, 각 service가 자기 송신 메시지를 만들고 `App_Can`의 direct wrapper를 호출하는 구조입니다.

```text
App_StatusTxService
  -> AppCan_SendDistanceLevel()    // 0x400

AutoExitService 예정
  -> AppCan_SendExitComplete()     // 0x401

DriveService 예정
  -> AppCan_SendVehicleControl()   // 0x100
```

`App_Can`은 실제로 `McmcanFd_Send...()`를 호출합니다.

```c
BaseType_t AppCan_SendDistanceLevel(const DistanceLevelCmd_t *cmd);
BaseType_t AppCan_SendExitComplete(const ExitCompleteCmd_t *cmd);
BaseType_t AppCan_SendVehicleControl(const VehicleControlCmd_t *cmd);
```

## App_Types.h Purpose

`App_Types.h`는 App layer에서 공통으로 사용하는 enum/struct를 모아두는 파일입니다.

대표 타입:

```c
AppGearStatus
AppPdwLevel
AppPdwDirection
AppRpiInputState
AppUltrasonicState
AppAutoParkingState
AppPdwState
```

각 타입의 의미:

```text
AppRpiInputState
  0x201 수신값을 App 내부에서 쓰기 좋게 변환한 상태
  driveCmd, steeringCmd, gear, pdwSwitchOn 포함

AppUltrasonicState
  0x200 수신값을 App 내부에서 쓰기 좋게 변환한 상태
  distanceMm[10], imuYaw, vehicleSpeed 포함

AppAutoParkingState
  0x300 수신값을 App 내부에서 쓰기 좋게 변환한 상태

AppPdwState
  PDW 판단 결과
  level[10], enabled, dangerDetected 포함
```

주의:

`App_Types.h`는 원칙적으로 타입 정의용입니다. Task period, threshold 같은 service 내부 설정값은 각 service `.c` 파일에 두는 것을 권장합니다.

## Task Creation

`Cpu0_Main.c`에서는 `App_Init()`만 호출합니다.

```c
App_Init();
vTaskStartScheduler();
```

Task 생성은 `App.c`에서 합니다.

현재 task:

```text
AppCan_RxTask
  CAN 수신 polling 및 Rx mailbox update

AppPdwService_Task
  PDW 거리 단계 판단

AppStatusTxService_Task
  0x400 상태 송신
```

`stack size`와 `priority`는 `App.c`에 정의합니다.

```c
#define APP_CAN_RX_STACK_SIZE          (configMINIMAL_STACK_SIZE)
#define APP_PDW_STACK_SIZE             (configMINIMAL_STACK_SIZE)
#define APP_STATUS_TX_STACK_SIZE       (configMINIMAL_STACK_SIZE)

#define APP_CAN_RX_PRIORITY            (tskIDLE_PRIORITY + 4u)
#define APP_PDW_PRIORITY               (tskIDLE_PRIORITY + 3u)
#define APP_STATUS_TX_PRIORITY         (tskIDLE_PRIORITY + 2u)
```

`xTaskCreate()` 결과는 가능하면 `app_assert_pass()`로 확인하는 것을 권장합니다.

```c
app_assert_pass(xTaskCreate(AppPdwService_Task,
                            "Pdw",
                            APP_PDW_STACK_SIZE,
                            NULL,
                            APP_PDW_PRIORITY,
                            NULL));
```

## Task Period

권장 위치:

```text
App_Can.c
  APP_CAN_RX_TASK_PERIOD_MS

App_PdwService.c
  APP_PDW_SERVICE_PERIOD_MS

App_StatusTxService.c
  APP_TX_STATUS_PERIOD_MS
```

현재 테스트에서는 `10ms` 기준이 안정적입니다.

```c
#define APP_CAN_RX_TASK_PERIOD_MS      (1u)
#define APP_PDW_SERVICE_PERIOD_MS      (10u)
#define APP_TX_STATUS_PERIOD_MS        (10u)
```

## RxService Usage

다른 service는 `McmcanFd_Recv...()`를 직접 호출하지 않습니다.
반드시 `App_RxService`를 통해 App 내부 타입으로 읽습니다.

### RPi 입력 읽기

```c
AppRpiInputState rpiInput;

if(AppRxService_GetRpiInput(&rpiInput) == pdPASS)
{
    /* rpiInput.driveCmd
     * rpiInput.steeringCmd
     * rpiInput.gear
     * rpiInput.pdwSwitchOn
     */
}
```

### 초음파 값 읽기

예시 : 출차 service에서 초음파 거리값이 필요하면 아래처럼 사용합니다.

```c
AppUltrasonicState ultrasonic;

if(AppRxService_GetUltrasonicState(&ultrasonic) == pdPASS)
{
    uint16 front = ultrasonic.distanceMm[APP_PDW_DIR_FRONT];
    uint16 behind = ultrasonic.distanceMm[APP_PDW_DIR_BEHIND];
    sint16 yaw = ultrasonic.imuYaw;
}
```

이 함수는 mailbox를 `peek`하므로, PDW service와 출차 service가 동시에 호출해도 됩니다.

### 자동 출차 요청 읽기

```c
AppAutoParkingState autoParking;

if(AppRxService_GetAutoParkingState(&autoParking) == pdPASS)
{
    if(autoParking.autoParkingStart == TRUE)
    {
        /* AutoExitService start */
    }
}
```

## StatusTxService

`App_StatusTxService`는 `0x400`만 담당합니다.(PdwService에서 바로 보내고 싶엇는데 400 payload에 gear랑 speed 정보도 있어서 따로 이것만 이렇게 service 만들었습니다.)
(다른 can 송신은 해당 service에서 직접 호출해서 보내도 될 것 같습니다.)

`0x400` payload 구성:

```text
B0~B9   PDW level[10]
B10     PDW enabled
B11     VehicleSpeedCmd = 0x201 DriveCmd echo
B12     GearStatusCmd   = 0x201 GearStatus echo
B13     PDW danger alarm
B14~B15 padding
```

## AutoExitService Integration Guide

출차 service를 추가할 때 권장 구조:

```text
App_AutoExitService/
  App_AutoExitService.h
  App_AutoExitService.c
```

출차 service가 읽을 값:

```c
AppRxService_GetAutoParkingState(&autoParking);
AppRxService_GetUltrasonicState(&ultrasonic);
AppRxService_GetRpiInput(&rpiInput);
```

출차 완료 송신:

```c
ExitCompleteCmd_t tx;

tx.exitComplete = 1u;
(void)AppCan_SendExitComplete(&tx);
```

출차 service가 직접 `McmcanFd_SendExitComplete()`를 호출하지 않습니다. 반드시 `AppCan_SendExitComplete()`를 사용합니다.

## DriveService Integration Guide

Driving service를 추가할 때 권장 구조:

```text
App_DriveService/
  App_DriveService.h
  App_DriveService.c
```

Driving service가 읽을 값:

```c
AppRxService_GetRpiInput(&rpiInput);
AppPdwService_GetState(&pdwState);
```

예:

```c
if(pdwState.dangerDetected == TRUE)
{
    /* stop command */
}
else
{
    /* use rpiInput.driveCmd / rpiInput.steeringCmd */
}
```

차량 제어 송신:

```c
VehicleControlCmd_t tx;

tx.driveCmd = driveCmd;
tx.steeringCmd = steeringCmd;
(void)AppCan_SendVehicleControl(&tx);
```

Driving service가 직접 `McmcanFd_SendVehicleControl()`를 호출하지 않습니다. 반드시 `AppCan_SendVehicleControl()`을 사용합니다.

## PCAN Test Notes

### 0x201 Test Frame

```text
ID: 201h
Type: Classical CAN
Length: 4
Data: 7F 7F 01 01

B0 DriveCmd      = 0x7F
B1 SteeringCmd   = 0x7F
B2 GearStatus    = 0x01, D
B3 PDW Switch    = 0x01, On
```

### 0x200 Test Frame

`UltrasonicDistanceCmd_t`는 거리 하나가 `uint16`입니다.

`100mm`는 `0x0064`이고, little-endian payload에서는 `64 00`입니다.

전 방향 100mm:

```text
ID: 200h
Type: CAN FD
Length: 24
Data:
64 00 64 00 64 00 64 00
64 00 64 00 64 00 64 00
64 00 64 00 00 00 00 00
```

기대 `0x400`:

```text
ID: 400h
Type: CAN FD
Length: 16
Data:
04 04 04 04 04 04 04 04
04 04 01 7F 01 01 00 00
```

## Current Verification Status

확인된 것:

- TC375 -> PCAN `0x400` CAN FD 송신 확인
- PCAN -> TC375 `0x201` 수신 후 `0x400` B10/B11/B12 반영 확인
- PCAN -> TC375 `0x200` 수신 후 PDW level `0x04` 반영 확인
- `0x400` 정상 payload 예:

```text
ID=1024 decimal = 0x400
Data=04 04 04 04 04 04 04 04 04 04 01 7F 01 01
```
## Include Paths

ADS include path에 최소 아래 경로가 필요합니다.

```text
${workspace_loc:/${ProjName}/App}
${workspace_loc:/${ProjName}/App/App_Can}
${workspace_loc:/${ProjName}/App/App_RxService}
${workspace_loc:/${ProjName}/App/App_PdwService}
${workspace_loc:/${ProjName}/App/App_StatusTxService}
${workspace_loc:/${ProjName}/Drivers/McmcanFd}
${workspace_loc:/${ProjName}/Libraries/iLLD/TC37A/Tricore/Can}
${workspace_loc:/${ProjName}/Libraries/iLLD/TC37A/Tricore/Can/Can}
${workspace_loc:/${ProjName}/Libraries/iLLD/TC37A/Tricore/Can/Std}
```
