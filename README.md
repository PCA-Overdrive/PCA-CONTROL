# PCA-Control-ECU

PCA-Control-ECU는 PCA-Overdrive 프로젝트의 **판단 ECU(Control ECU)** 코드입니다.

이 ECU는 Sensor ECU와 Raspberry Pi로부터 CAN/CAN FD 메시지를 수신하고, 수신된 정보를 기반으로 **PDW 판단**, **자동출차 판단**, **최종 차량 제어 명령 생성**을 수행합니다.

최종적으로 판단 ECU는 Motor ECU로 차량 제어 명령을 송신하고, Raspberry Pi/HMI로 판단 상태를 전달합니다.

---

## 1. System Overview

전체 시스템에서 판단 ECU는 Raspberry Pi, Sensor ECU, Motor ECU 사이의 중간 판단 노드입니다.

```text
Raspberry Pi
  ├─ 0x201 차량 상태 / 수동 제어 입력
  └─ 0x300 자동출차 명령
        ↓
Judgment ECU / Control ECU
  ├─ CAN 수신값 최신값 관리
  ├─ CAN raw data → App struct 변환
  ├─ 초음파 기반 PDW 판단
  ├─ 자동출차 FSM / 회피 판단 / yaw 판단
  ├─ 0x400 PDW 상태 송신
  ├─ 0x401 자동출차 상태 송신
  └─ 0x100 차량 제어 명령 송신
        ↑
Sensor ECU
  └─ 0x200 초음파 / IMU yaw / 속도

Motor ECU
  ← 0x100 driveCmd / steeringCmd
```

---

## 2. Main Features

현재 구현된 주요 기능은 다음과 같습니다.

- CAN/CAN FD 수신 및 송신
- CAN ID별 최신 수신값 mailbox 관리
- CAN raw message를 App 내부 구조체로 변환
- 초음파 거리 기반 PDW level 판단
- PDW 상태를 Raspberry Pi/HMI로 송신
- Raspberry Pi 입력 기반 일반 주행 명령 전달
- Raspberry Pi timeout 감시 및 안전 정지
- 자동출차 명령 수신
- 직진 / 좌측 / 우측 자동출차 FSM 수행
- 출차 방향 주변 초음파 상태 기반 NORMAL / AVOID / BLOCKED 전략 선택
- 회피 후 기본 출차 profile 재개
- IMU yaw 기반 목표 각도 도달 판단
- Motor ECU로 최종 차량 제어 명령 송신

---

## 3. Folder Structure

```text
PCA-Control-ECU/
├─ App/
│  ├─ App.c
│  ├─ App.h
│  ├─ App_Types.h
│  │
│  ├─ App_Can/
│  │  ├─ App_Can.c
│  │  └─ App_Can.h
│  │
│  ├─ App_RxService/
│  │  ├─ App_RxService.c
│  │  └─ App_RxService.h
│  │
│  ├─ App_PdwService/
│  │  ├─ App_PdwService.c
│  │  └─ App_PdwService.h
│  │
│  ├─ App_StatusTxService/
│  │  ├─ App_StatusTxService.c
│  │  └─ App_StatusTxService.h
│  │
│  ├─ App_DriveService/
│  │  ├─ App_DriveService.c
│  │  └─ App_DriveService.h
│  │
│  └─ App_AutoExitService/
│     ├─ App_AutoExitService.c
│     ├─ App_AutoExitService.h
│     ├─ App_AutoExitService_Internal.h
│     ├─ App_AutoExitTypes.h
│     ├─ App_AutoExitProfile.c
│     ├─ App_AutoExitPlanner.c
│     └─ App_AutoExitMonitor.c
│
├─ Drivers/
│  └─ McmcanFd/
│     ├─ CanMsg.h
│     ├─ McmcanFd.c
│     ├─ McmcanFd.h
│     └─ McmcanFd_Cfg.h
│
├─ OS/
│  └─ FreeRTOS/
│
├─ Configurations/
├─ Libraries/
├─ Cpu0_Main.c
├─ Cpu1_Main.c
├─ Cpu2_Main.c
├─ App_Config.h
└─ README.md
```

---

## 4. Software Architecture

판단 ECU App은 CAN 수신값을 바로 판단 로직에 사용하지 않고, 다음과 같이 계층을 분리했습니다.

```text
McmcanFd
  ↓
App_Can
  ↓
App_RxService
  ↓
App_PdwService / App_AutoExitService
  ↓
App_StatusTxService / App_DriveService
```

각 계층의 역할은 다음과 같습니다.

| Layer | Role |
|---|---|
| `McmcanFd` | TC375 MCMCAN hardware driver |
| `App_Can` | CAN Rx latest mailbox 관리, Tx wrapper 제공 |
| `App_RxService` | CAN raw message를 App 내부 struct로 변환 |
| `App_PdwService` | 초음파 거리 기반 PDW level 판단 |
| `App_AutoExitService` | 자동출차 FSM, 회피 판단, yaw 판단 |
| `App_StatusTxService` | 0x400 PDW 상태 송신 |
| `App_DriveService` | 0x100 최종 차량 제어 명령 송신 |

---

## 5. RTOS Task Design

`Cpu0_Main.c`에서 `App_Init()`을 호출하고 FreeRTOS scheduler를 시작합니다.

`App_Init()`에서는 CAN, PDW, AutoExit service를 초기화한 뒤 RTOS task를 생성합니다.

```text
App_Init()
  ├─ AppCan_Init()
  ├─ AppPdwService_Init()
  ├─ AppAutoExitService_Init()
  │
  ├─ AppCan_RxTask
  ├─ AppPdwService_Task
  ├─ AppAutoExitService_Task
  ├─ AppStatusTxService_Task
  └─ AppDriveService_Task
```

현재 task 주기는 다음과 같습니다.

| Task | Period | Description |
|---|---:|---|
| CAN Rx | 1 ms | 수신 CAN frame을 latest mailbox에 반영 |
| PDW Service | 10 ms | 초음파 거리값을 PDW level로 판단 |
| AutoExit Service | 10 ms | 자동출차 명령 처리, FSM 진행, 회피/yaw 판단 |
| Status Tx | 10 ms | 0x400 PDW 상태 메시지 송신 |
| Drive Service | 12 ms | 최종 0x100 차량 제어 명령 송신 |

---

## 6. CAN Rx Mailbox Policy

수신 메시지는 CAN ID별 latest mailbox 방식으로 관리합니다.

```text
0x200 Sensor ECU → Judgment ECU
  → g_ultrasonicMailbox

0x201 Raspberry Pi → Judgment ECU
  → g_vehicleStatusMailbox

0x300 Raspberry Pi → Judgment ECU
  → g_autoParkingMailbox
```

각 mailbox는 길이 1의 FreeRTOS queue로 생성됩니다.

```c
xQueueCreate(1u, sizeof(...));
```

수신 task에서는 최신 프레임을 mailbox에 덮어씁니다.

```c
xQueueOverwrite(...);
```

각 service에서는 mailbox 값을 제거하지 않고 읽습니다.

```c
xQueuePeek(...);
```

이 구조를 사용한 이유는 다음과 같습니다.

- PDW와 자동출차는 과거 이력보다 최신 상태값이 중요함
- 같은 센서값을 여러 service가 동시에 참조해야 함
- 하나의 service가 값을 읽어도 다른 service에서 다시 읽을 수 있어야 함
- 수신, 파싱, 판단 계층을 분리해 디버깅 범위를 줄일 수 있음

---

## 7. CAN Interface

## 7.1 Rx Messages

### 0x200 Sensor ECU → Judgment ECU

Sensor ECU에서 초음파 거리, IMU yaw, 차량 속도 정보를 송신합니다.

```text
CAN FD
Payload: 23 bytes
```

| Byte | Signal | Type | Description |
|---:|---|---|---|
| B0~B1 | frontDist | uint16 | 전방 거리 |
| B2~B3 | frontRightDist | uint16 | 전방 우측 거리 |
| B4~B5 | rightFrontDist | uint16 | 우측 전방 거리 |
| B6~B7 | rightBehindDist | uint16 | 우측 후방 거리 |
| B8~B9 | behindRightDist | uint16 | 후방 우측 거리 |
| B10~B11 | behindDist | uint16 | 후방 거리 |
| B12~B13 | behindLeftDist | uint16 | 후방 좌측 거리 |
| B14~B15 | leftBehindDist | uint16 | 좌측 후방 거리 |
| B16~B17 | leftFrontDist | uint16 | 좌측 전방 거리 |
| B18~B19 | frontLeftDist | uint16 | 전방 좌측 거리 |
| B20~B21 | imuYaw | int16 | IMU yaw |
| B22 | vehicleSpeed | uint8 | 차량 속도 |

---

### 0x201 Raspberry Pi → Judgment ECU

Raspberry Pi에서 수동 주행 명령, 기어 상태, PDW 활성화 상태, line angle 정보를 송신합니다.

```text
Classical CAN
Payload: 6 bytes
```

| Byte | Signal | Type | Description |
|---:|---|---|---|
| B0 | driveCmd | uint8 | 주행 명령 |
| B1 | steeringCmd | uint8 | 조향 명령 |
| B2 | gearStatus | uint8 | 기어 상태 |
| B3 | pcaActivated | uint8 | PDW/PCA 활성화 상태 |
| B4~B5 | lineAngle | int16 | 주차선 또는 차량 기준 각도 보정값 |

> 기존 문서에 0x201이 4 bytes로 적혀 있었다면 현재 코드 기준과 맞지 않습니다.  
> 현재 코드 기준 0x201은 `lineAngle`을 포함하므로 6 bytes입니다.

---

### 0x300 Raspberry Pi → Judgment ECU

Raspberry Pi에서 자동출차 명령을 송신합니다.

```text
Classical CAN
Payload: 1 byte
```

| Byte | Signal | Value | Description |
|---:|---|---:|---|
| B0 | autoParkingStart | 0x00 | NORMAL |
| B0 | autoParkingStart | 0x01 | START_STRAIGHT |
| B0 | autoParkingStart | 0x02 | START_LEFT |
| B0 | autoParkingStart | 0x03 | START_RIGHT |
| B0 | autoParkingStart | 0x04 | STOP |

---

## 7.2 Tx Messages

### 0x400 Judgment ECU → Raspberry Pi

판단 ECU가 PDW 판단 결과와 차량 상태를 Raspberry Pi/HMI로 송신합니다.

```text
CAN FD
Payload: 14 bytes
```

| Byte | Signal | Description |
|---:|---|---|
| B0 | frontLevel | 전방 PDW level |
| B1 | frontRightLevel | 전방 우측 PDW level |
| B2 | rightFrontLevel | 우측 전방 PDW level |
| B3 | rightBehindLevel | 우측 후방 PDW level |
| B4 | behindRightLevel | 후방 우측 PDW level |
| B5 | behindLevel | 후방 PDW level |
| B6 | behindLeftLevel | 후방 좌측 PDW level |
| B7 | leftBehindLevel | 좌측 후방 PDW level |
| B8 | leftFrontLevel | 좌측 전방 PDW level |
| B9 | frontLeftLevel | 전방 좌측 PDW level |
| B10 | pcaActivated | PDW 활성화 상태 |
| B11 | vehicleSpeed | 차량 속도 |
| B12 | gearStatus | 기어 상태 |
| B13 | emergencyStop | PDW danger 감지 상태 |

PDW level 값은 다음과 같습니다.

| Value | Level |
|---:|---|
| 0x00 | NO_OBSTACLE |
| 0x01 | SAFE |
| 0x02 | CAUTION |
| 0x03 | NEAR |
| 0x04 | DANGER |

---

### 0x401 Judgment ECU → Raspberry Pi

판단 ECU가 자동출차 진행 상태를 Raspberry Pi/HMI로 송신합니다.

```text
Classical CAN
Payload: 1 byte
```

| Byte | Signal | Value | Description |
|---:|---|---:|---|
| B0 | autoExitStatus | 0x00 | IDLE / NORMAL |
| B0 | autoExitStatus | 0x01 | IN_PROGRESS |
| B0 | autoExitStatus | 0x02 | COMPLETE |
| B0 | autoExitStatus | 0x03 | STOPPED |

현재 코드에는 내부적으로 BLOCKED 상태도 존재합니다.  
외부 인터페이스에서 STOPPED와 BLOCKED를 분리할지는 Raspberry Pi/HMI 정책에 맞춰 정리해야 합니다.

권장 상태값은 다음과 같습니다.

| Value | Status |
|---:|---|
| 0x00 | IDLE / NORMAL |
| 0x01 | IN_PROGRESS |
| 0x02 | COMPLETE |
| 0x03 | STOPPED |
| 0x04 | BLOCKED |

---

### 0x100 Judgment ECU → Motor ECU

판단 ECU가 Motor ECU로 최종 차량 제어 명령을 송신합니다.

```text
Classical CAN
Payload: 2 bytes
```

| Byte | Signal | Description |
|---:|---|---|
| B0 | driveCmd | 주행 명령 |
| B1 | steeringCmd | 조향 명령 |

제어값 기준은 다음과 같습니다.

```text
driveCmd
  0 ~ 126   : forward
  127       : stop
  128 ~ 255 : reverse

steeringCmd
  0 ~ 126   : left
  127       : center
  128 ~ 255 : right
```

---

## 8. App Types

`App_Types.h`는 App layer에서 공통으로 사용하는 enum과 struct를 정의합니다.

주요 타입은 다음과 같습니다.

| Type | Description |
|---|---|
| `AppGearStatus` | 기어 상태 |
| `AppPdwLevel` | PDW 위험 level |
| `AppPdwDirection` | 초음파 센서 방향 |
| `AppRpiInputState` | 0x201을 App 내부에서 사용하기 위한 상태 |
| `AppUltrasonicState` | 0x200을 App 내부에서 사용하기 위한 상태 |
| `AppAutoParkingState` | 0x300을 App 내부에서 사용하기 위한 상태 |
| `AppPdwState` | PDW 판단 결과 |

---

## 9. PDW Service

`App_PdwService`는 초음파 거리값을 PDW level로 변환합니다.

거리 판단 기준은 다음과 같습니다.

| Distance | PDW Level |
|---:|---|
| 0 mm | NO_OBSTACLE |
| 0 < distance <= 200 mm | DANGER |
| 200 mm < distance <= 250 mm | NEAR |
| 250 mm < distance <= 300 mm | CAUTION |
| 300 mm < distance <= 1000 mm | SAFE |
| 1000 mm < distance | NO_OBSTACLE |

PDW 판단 결과는 `AppPdwState`에 저장됩니다.

```text
AppPdwState
  ├─ enabled
  ├─ dangerDetected
  ├─ level[10]
  └─ distanceMm[10]
```

PDW는 다음 조건에서 활성화됩니다.

```text
조건 1.
  Raspberry Pi에서 PDW/PCA 활성화
  AND gear가 P가 아님
  AND vehicleSpeed가 기준 속도 이하

조건 2.
  자동출차 명령이 NORMAL/STOP이 아닌 상태
```

---

## 10. StatusTxService

`App_StatusTxService`는 PDW 판단 결과와 차량 상태를 묶어 `0x400`으로 송신합니다.

```text
Input:
  - AppPdwService_GetState()
  - AppRxService_GetRpiInput()
  - AppRxService_GetUltrasonicState()

Output:
  - DistanceLevelCmd_t
  - CAN ID 0x400
```

`0x400`에는 다음 정보가 포함됩니다.

- 10방향 PDW level
- PDW/PCA 활성화 상태
- 차량 속도
- 기어 상태
- emergencyStop 상태

---

## 11. DriveService

`App_DriveService`는 Motor ECU로 최종 `0x100` 차량 제어 명령을 송신합니다.

제어 우선순위는 다음과 같습니다.

```text
1. Raspberry Pi timeout
   → stop / center 명령 송신

2. AutoExitService active
   → 자동출차 제어 명령을 0x100으로 송신

3. PDW dangerDetected
   → stop / center 명령 송신

4. 일반 상태
   → Raspberry Pi의 driveCmd / steeringCmd를 Motor ECU로 전달
```

즉, 일반 주행 중에는 Raspberry Pi 입력을 그대로 Motor ECU로 전달하지만, 자동출차가 진행 중이면 자동출차 제어 명령이 우선 적용됩니다.

Raspberry Pi 입력이 일정 시간 이상 수신되지 않으면 안전을 위해 정지 명령을 송신합니다.

---

## 12. AutoExitService

`App_AutoExitService`는 Raspberry Pi의 `0x300` 명령을 기반으로 자동출차를 수행합니다.

지원하는 명령은 다음과 같습니다.

| Command | Description |
|---|---|
| NORMAL | 자동출차 대기 |
| START_STRAIGHT | 직진 출차 시작 |
| START_LEFT | 좌측 출차 시작 |
| START_RIGHT | 우측 출차 시작 |
| STOP | 자동출차 중지 |

자동출차 내부 흐름은 다음과 같습니다.

```text
IDLE
  ↓
START_STOP
  ↓
SELECT_STRATEGY
  ├─ NORMAL
  │    ↓
  │  RUN_PROFILE
  │
  ├─ AVOID_AND_RESUME
  │    ↓
  │  AVOID_ESCAPE
  │    ↓
  │  AVOID_STOP_1
  │    ↓
  │  AVOID_REALIGN
  │    ↓
  │  AVOID_STOP_2
  │    ↓
  │  RUN_PROFILE
  │
  └─ BLOCKED
```

---

## 13. AutoExit Motion Profile

자동출차 profile은 `App_AutoExitProfile.c`에 하드코딩되어 있습니다.

### 13.1 Straight Profile

```text
1. forward + center
2. stop + center
```

### 13.2 Right Exit Profile

```text
1. forward + center
2. forward + right
3. stop + center
4. reverse + center
5. stop + center
6. forward + right
7. stop + center
8. reverse + left
9. stop + center
10. forward + right
11. forward + center
12. stop + center
```

### 13.3 Left Exit Profile

Right profile의 좌우 반전입니다.

```text
1. forward + center
2. forward + left
3. stop + center
4. reverse + center
5. stop + center
6. forward + left
7. stop + center
8. reverse + right
9. stop + center
10. forward + left
11. forward + center
12. stop + center
```

---

## 14. AutoExit Planner

`App_AutoExitPlanner`는 자동출차 시작 시 주변 초음파 상태를 보고 출차 전략을 선택합니다.

전략은 다음과 같습니다.

| Strategy | Description |
|---|---|
| `NORMAL` | 기본 출차 profile을 바로 수행 |
| `AVOID_AND_RESUME` | 먼저 반대 방향으로 회피한 뒤 기본 profile 재개 |
| `BLOCKED` | 출차 불가로 판단하고 정지 |

Planner는 출차 방향의 측면 초음파 센서를 이용해 장애물의 가까운 정도와 기울어진 정도를 판단합니다.

우측 출차 기준으로는 주로 다음 센서를 봅니다.

```text
출차 방향:
  - RIGHT_FRONT
  - RIGHT_BEHIND

반대 방향:
  - LEFT_FRONT
  - LEFT_BEHIND

회피 중 앞쪽 간섭:
  - FRONT_LEFT
```

좌측 출차는 위 관계가 좌우 반전됩니다.

측면 risk는 다음과 같이 분류됩니다.

| Risk | Description |
|---|---|
| SAFE | 기본 출차 가능 |
| CRITICAL | 너무 가까워 출차 불가 가능성이 큼 |
| NARROW_BOTH | 앞/뒤 모두 가까워 공간이 좁음 |
| NEAR_FRONT | 앞쪽이 가까움 |
| NEAR_REAR | 뒤쪽이 가까움 |
| TILTED_FRONT | 앞쪽으로 갈수록 가까워짐 |
| TILTED_REAR | 뒤쪽이 더 가까움 |

회피 방향은 출차 방향의 반대입니다.

```text
좌측 출차:
  오른쪽으로 escape
  → 왼쪽으로 realign
  → 좌측 출차 profile 재개

우측 출차:
  왼쪽으로 escape
  → 오른쪽으로 realign
  → 우측 출차 profile 재개
```

---

## 15. AutoExit Monitor

`App_AutoExitMonitor`는 자동출차 상태 관리와 `0x401` 송신을 담당합니다.

주요 역할은 다음과 같습니다.

- 자동출차 상태 관리
- 자동출차 시작 yaw 저장
- Raspberry Pi에서 받은 lineAngle 반영
- 목표 회전각 계산
- 목표 yaw 계산
- 현재 yaw 갱신
- 목표 yaw 도달 여부 판단
- COMPLETE / STOPPED / BLOCKED 상태 유지 후 IDLE 복귀
- 0x401 자동출차 상태 송신

yaw 판단 흐름은 다음과 같습니다.

```text
startYawDeg
  + targetTurnDeg
  + lineAngle 보정
  → targetYawDeg
```

자동출차 profile의 마지막 회전 구간은 시간뿐 아니라 목표 yaw 도달 여부를 기준으로 종료할 수 있습니다.

---

## 16. Build / Run

이 프로젝트는 Infineon AURIX TC375 기반 FreeRTOS 프로젝트입니다.

기본 실행 흐름은 다음과 같습니다.

```text
Cpu0_Main.c
  ↓
App_Init()
  ↓
FreeRTOS task 생성
  ↓
vTaskStartScheduler()
```

CAN node 설정은 `Drivers/McmcanFd/McmcanFd_Cfg.h`에서 판단 ECU 설정을 사용합니다.

```c
#define NODE_JUDGMENT_ECU
```

CAN 설정은 시스템의 Raspberry Pi, Sensor ECU, Motor ECU와 동일하게 맞춰야 합니다.

```text
Nominal bitrate : 500 kbit/s
Data bitrate    : 2 Mbit/s
Mode            : CAN FD
```

---

## 17. Current Implementation Notes

### 17.1 0x201 Payload Size

현재 코드 기준 0x201은 6 bytes입니다.

```text
B0      driveCmd
B1      steeringCmd
B2      gearStatus
B3      pcaActivated
B4~B5   lineAngle
```

Raspberry Pi 송신 코드, PCAN 테스트 프레임, README 문서가 모두 이 기준과 일치해야 합니다.

---

### 17.2 PDW Service Input Dependency

현재 PDW Service는 0x201, 0x200, 0x300 상태를 함께 읽어 PDW 판단을 수행합니다.

일반 PDW 기능은 자동출차 명령이 없어도 동작해야 하므로, 0x300은 필수 입력이 아니라 optional 입력으로 다루는 구조가 더 자연스럽습니다.

개선 예시는 다음과 같습니다.

```c
AppAutoParkingState autoParkingState;

if (AppRxService_GetAutoParkingState(&autoParkingState) != pdPASS)
{
    autoParkingState.cmd = APP_AUTO_EXIT_CMD_NORMAL;
}

if ((AppRxService_GetRpiInput(&rpiInput) == pdPASS) &&
    (AppRxService_GetUltrasonicState(&ultrasonic) == pdPASS))
{
    AppPdwService_Process(&rpiInput, &ultrasonic, &autoParkingState);
}
```

---

### 17.3 AutoExit Yaw Timeout

yaw 기반 종료 step에서는 IMU yaw가 정상적으로 갱신되지 않거나 목표 yaw에 도달하지 못하는 경우를 대비해 timeout fallback을 두는 것이 좋습니다.

---

### 17.4 STOPPED / BLOCKED Status

현재 내부적으로는 BLOCKED 개념이 존재합니다.

외부 0x401 상태값에서 STOPPED와 BLOCKED를 구분하면 Raspberry Pi/HMI에서 자동출차 중지 원인을 더 명확하게 표시할 수 있습니다.

권장 상태값은 다음과 같습니다.

```text
0x00 IDLE / NORMAL
0x01 IN_PROGRESS
0x02 COMPLETE
0x03 STOPPED
0x04 BLOCKED
```

---

## 18. Development Status

| Module | Status |
|---|---|
| McmcanFd Driver integration | Implemented |
| App_Can latest mailbox | Implemented |
| RxService parsing | Implemented |
| PDW level judgment | Implemented |
| 0x400 StatusTx | Implemented |
| DriveService 0x100 Tx | Implemented |
| AutoExitService FSM | Implemented |
| AutoExit motion profile | Implemented |
| AutoExit planner / avoid strategy | Implemented |
| AutoExit monitor / 0x401 Tx | Implemented |
| yaw target judgment | Implemented |
| 실차 parameter tuning | In progress |
| PDW 0x300 optional handling | Improvement candidate |
| yaw timeout fallback | Improvement candidate |
| STOPPED / BLOCKED status separation | Improvement candidate |

---

## 19. Summary

PCA-Control-ECU는 판단 ECU의 App layer를 다음 구조로 분리했습니다.

```text
McmcanFd
  → CAN hardware 송수신

App_Can
  → 최신 수신값 mailbox 관리

App_RxService
  → CAN raw data를 App struct로 변환

App_PdwService
  → 초음파 기반 PDW 판단

App_AutoExitService
  → 자동출차 FSM / 회피 판단 / yaw 판단

App_StatusTxService
  → 0x400 PDW 상태 송신

App_DriveService
  → 0x100 최종 차량 제어 명령 송신
```

이 구조를 통해 CAN 수신, 데이터 변환, 판단 로직, 상태 송신, 차량 제어 송신의 책임을 분리하고, RTOS task 단위로 디버깅할 수 있도록 구성했습니다.
