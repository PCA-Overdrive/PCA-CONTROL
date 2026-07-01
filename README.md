# PCA-Control-ECU

PCA-Control-ECU는 PCA-Overdrive 프로젝트의 **판단 ECU(Control ECU)** 소프트웨어입니다.

판단 ECU는 Raspberry Pi, Sensor ECU, Motor ECU 사이에서 차량의 현재 상태를 해석하고, 주차 보조 및 자동출차에 필요한 판단을 수행한 뒤 최종 제어 명령을 생성합니다.

이 ECU는 다음 정보를 입력으로 사용합니다.

- Sensor ECU에서 수신한 초음파 거리값
- Sensor ECU에서 수신한 IMU yaw 값
- Sensor ECU에서 수신한 차량 속도
- Raspberry Pi에서 수신한 수동 주행 명령
- Raspberry Pi에서 수신한 기어 상태
- Raspberry Pi에서 수신한 PDW/PCA 활성화 상태
- Raspberry Pi에서 수신한 자동출차 명령
- Raspberry Pi에서 수신한 주차선/차량 기준 각도 보정값

판단 ECU는 이 정보를 바탕으로 다음 기능을 수행합니다.

- 초음파 거리 기반 PDW 판단
- 주차 보조 상태 송신
- 자동출차 방향 판단
- 자동출차 중 회피 전략 선택
- IMU yaw와 Raspberry Pi의 각도 보정값을 이용한 출차 정렬 판단
- Motor ECU로 최종 주행/조향 명령 송신

---

## 1. Project Overview

PCA-Overdrive 시스템은 Raspberry Pi, 판단 ECU, Sensor ECU, Motor ECU로 구성됩니다.

각 장치의 역할은 다음과 같습니다.

| Module | Role |
|---|---|
| Raspberry Pi | 사용자 인터페이스, 수동 조작 명령, 자동출차 명령, 주차선 각도 정보 제공 |
| Sensor ECU | 초음파 거리, IMU yaw, 차량 속도 측정 |
| Judgment ECU | 센서값과 사용자 명령을 기반으로 PDW, 자동출차, 최종 제어 판단 수행 |
| Motor ECU | 판단 ECU가 보낸 drive/steering 명령을 실제 차량 구동 명령으로 변환 |

PCA-Control-ECU는 이 중 **Judgment ECU**에 해당합니다.

전체 흐름은 다음과 같습니다.

```text
Raspberry Pi
  ├─ 수동 주행 명령
  ├─ 기어 상태
  ├─ PDW/PCA 활성화 상태
  ├─ 자동출차 시작/정지 명령
  └─ 주차선/차량 기준 각도 보정값
        ↓
Judgment ECU
  ├─ CAN 수신값 최신 상태 관리
  ├─ 수신 데이터 파싱
  ├─ PDW 위험도 판단
  ├─ 자동출차 전략 판단
  ├─ IMU yaw 기반 출차 각도 판단
  └─ 최종 주행/조향 명령 생성
        ↓
Motor ECU
  └─ 차량 구동 및 조향 제어
```

Sensor ECU는 판단 ECU로 초음파, IMU, 속도 정보를 전달합니다.

```text
Sensor ECU
  ├─ 초음파 거리값
  ├─ IMU yaw
  └─ 차량 속도
        ↓
Judgment ECU
```

판단 ECU는 판단 결과를 다시 Raspberry Pi/HMI로 전달합니다.

```text
Judgment ECU
  ├─ 0x400 PDW 상태
  └─ 0x401 자동출차 상태
        ↓
Raspberry Pi / HMI
```

---

## 2. System Architecture

```text
                  ┌──────────────────────┐
                  │     Raspberry Pi      │
                  │  HMI / User Command   │
                  │  Parking Line Angle   │
                  └───────────┬──────────┘
                              │
                              │ 0x201 Vehicle Status
                              │ 0x300 Auto Exit Command
                              ▼
┌─────────────────────────────────────────────────────────┐
│                  Judgment ECU                           │
│                PCA-Control-ECU                          │
│                                                         │
│  ┌──────────────┐    ┌──────────────┐                   │
│  │   App_Can    │ →  │ RxService    │                   │
│  └──────────────┘    └──────┬───────┘                   │
│                             │                           │
│          ┌──────────────────┼──────────────────┐        │
│          ▼                  ▼                  ▼        │
│  ┌──────────────┐   ┌────────────────┐   ┌────────────┐ │
│  │ PDW Service  │   │ AutoExitService│   │DriveService│ │
│  └──────┬───────┘   └───────┬────────┘   └─────┬──────┘ │
│         │                   │                  │        │
│         ▼                   ▼                  ▼        │
│  ┌──────────────┐   ┌────────────────┐   ┌────────────┐ │
│  │ Status Tx    │   │ AutoExitMonitor│   │ 0x100 Tx   │ │
│  └──────────────┘   └────────────────┘   └────────────┘ │
└───────────────▲───────────────────────────────┬─────────┘
                │                               │
                │ 0x200 Sensor Data             │ 0x100 Control Command
                │                               ▼
        ┌───────┴────────┐              ┌────────────────┐
        │   Sensor ECU    │              │   Motor ECU     │
        │ Ultrasonic/IMU  │              │ Drive/Steering  │
        └────────────────┘              └────────────────┘
```

---

## 3. Main Features

### 3.1 CAN/CAN FD Communication

판단 ECU는 CAN/CAN FD를 통해 다른 ECU와 통신합니다.

수신 메시지는 다음과 같습니다.

| CAN ID | Sender | Description |
|---|---|---|
| `0x200` | Sensor ECU | 초음파 거리, IMU yaw, 차량 속도 |
| `0x201` | Raspberry Pi | 수동 주행 명령, 기어 상태, PDW 활성화 상태, line angle |
| `0x300` | Raspberry Pi | 자동출차 명령 |

송신 메시지는 다음과 같습니다.

| CAN ID | Receiver | Description |
|---|---|---|
| `0x100` | Motor ECU | 최종 drive/steering 제어 명령 |
| `0x400` | Raspberry Pi/HMI | PDW 판단 상태 |
| `0x401` | Raspberry Pi/HMI | 자동출차 진행 상태 |

---

### 3.2 PDW

PDW는 Parking Distance Warning의 약자로, 초음파 센서를 이용한 주차 거리 경고 기능입니다.

Sensor ECU에서 받은 10방향 초음파 거리값을 판단 ECU가 다음과 같은 위험 level로 변환합니다.

| Level | Meaning |
|---|---|
| `NO_OBSTACLE` | 장애물 없음 |
| `SAFE` | 안전 거리 |
| `CAUTION` | 주의 거리 |
| `NEAR` | 가까움 |
| `DANGER` | 위험 거리 |

PDW 판단 결과는 `0x400` 메시지를 통해 Raspberry Pi/HMI로 전달됩니다.

---

### 3.3 Auto Exit

자동출차 기능은 후진 주차 상태에서 차량이 전방으로 빠져나오는 상황을 기준으로 구현되었습니다.

지원하는 출차 방향은 다음과 같습니다.

| Direction | Description |
|---|---|
| `START_STRAIGHT` | 직진 출차 |
| `START_LEFT` | 좌측 출차 |
| `START_RIGHT` | 우측 출차 |
| `STOP` | 자동출차 중지 |

자동출차는 단순히 정해진 주행 명령만 반복하는 구조가 아닙니다.

판단 ECU는 출차 시작 시 주변 초음파 상태를 확인하고, 다음 중 하나의 전략을 선택합니다.

| Strategy | Description |
|---|---|
| `NORMAL` | 기본 출차 profile을 그대로 수행 |
| `AVOID_AND_RESUME` | 먼저 반대 방향으로 회피한 뒤 기본 출차 profile 수행 |
| `BLOCKED` | 장애물로 인해 출차가 어렵다고 판단하고 정지 |

---

### 3.4 Parking Line Angle Compensation

Raspberry Pi는 카메라 또는 HMI 판단 결과를 바탕으로 주차선 또는 차량 기준 각도 보정값을 판단 ECU로 전달합니다.

이 값은 `0x201` 메시지의 `lineAngle` 필드로 전달됩니다.

판단 ECU는 자동출차 시작 시 다음 정보를 함께 사용합니다.

```text
1. 자동출차 시작 시점의 IMU yaw
2. 출차 방향에 따른 목표 회전각
3. Raspberry Pi에서 받은 lineAngle 보정값
```

이를 통해 자동출차 완료 시 차량이 목표 방향에 맞게 정렬되었는지 판단합니다.

```text
startYaw
  + targetTurnAngle
  + lineAngle compensation
  → targetYaw
```

이 구조를 통해 단순 시간 기반 출차가 아니라, IMU yaw와 외부 각도 보정값을 함께 반영한 출차 정렬 판단을 수행합니다.

---

## 4. Software Structure

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

## 5. Application Layer

PCA-Control-ECU의 App 계층은 기능별 service로 나누어져 있습니다.

| Module | Role |
|---|---|
| `App_Can` | CAN 수신값 최신 상태 관리 및 송신 wrapper |
| `App_RxService` | CAN raw data를 App 내부 구조체로 변환 |
| `App_PdwService` | 초음파 거리 기반 PDW 위험도 판단 |
| `App_StatusTxService` | PDW 상태를 `0x400`으로 송신 |
| `App_DriveService` | 최종 차량 제어 명령을 `0x100`으로 송신 |
| `App_AutoExitService` | 자동출차 FSM 및 제어 명령 생성 |
| `App_AutoExitPlanner` | 자동출차 전략 선택 |
| `App_AutoExitProfile` | 출차 방향별 motion profile 정의 |
| `App_AutoExitMonitor` | 자동출차 상태 송신 및 yaw 판단 |

---

## 6. Internal Data Flow

판단 ECU는 수신한 CAN 데이터를 바로 판단 로직에 사용하지 않습니다.

먼저 CAN ID별 최신값을 저장하고, 이후 App에서 사용하기 쉬운 구조체로 변환한 뒤 판단 로직에서 사용합니다.

```text
McmcanFd Driver
  ↓
App_Can
  ↓
App_RxService
  ↓
PDW / AutoExit / Drive Service
```

이 구조를 통해 다음 장점을 얻을 수 있습니다.

- Driver 계층과 판단 로직 분리
- CAN raw data와 App 내부 데이터 구조 분리
- 각 service가 동일한 최신 수신값을 공유 가능
- RTOS task 단위 디버깅 가능
- 기능 추가 시 영향 범위 최소화

---

## 7. RTOS Task Design

`Cpu0_Main.c`에서 `App_Init()`을 호출하고, 이후 FreeRTOS scheduler가 시작됩니다.

`App_Init()`에서는 각 service를 초기화하고 RTOS task를 생성합니다.

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

Task 주기는 다음과 같습니다.

| Task | Period | Role |
|---|---:|---|
| CAN Rx | 1 ms | CAN 수신 프레임을 최신 mailbox에 반영 |
| PDW Service | 10 ms | 초음파 거리 기반 PDW level 판단 |
| AutoExit Service | 10 ms | 자동출차 명령 처리 및 FSM 진행 |
| Status Tx | 10 ms | `0x400` PDW 상태 송신 |
| Drive Service | 12 ms | `0x100` 최종 차량 제어 명령 송신 |

---

## 8. CAN Rx Mailbox Policy

수신 메시지는 CAN ID별 latest mailbox 방식으로 관리합니다.

```text
0x200 Sensor Data
  → g_ultrasonicMailbox

0x201 Vehicle Status
  → g_vehicleStatusMailbox

0x300 Auto Exit Command
  → g_autoParkingMailbox
```

각 mailbox는 길이 1의 FreeRTOS queue입니다.

새 메시지가 들어오면 기존 값을 덮어씁니다.

```c
xQueueOverwrite(...)
```

service에서 읽을 때는 값을 제거하지 않고 확인합니다.

```c
xQueuePeek(...)
```

이 방식은 센서값, 차량 상태, 자동출차 명령처럼 “가장 최근 상태”가 중요한 데이터에 적합합니다.

---

## 9. CAN Interface

## 9.1 Rx Messages

### 9.1.1 `0x200` Sensor ECU → Judgment ECU

Sensor ECU가 초음파 거리, IMU yaw, 차량 속도를 판단 ECU로 전달합니다.

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

### 9.1.2 `0x201` Raspberry Pi → Judgment ECU

Raspberry Pi가 차량 상태, 수동 제어 명령, 주차선 각도 정보를 판단 ECU로 전달합니다.

```text
Classical CAN
Payload: 6 bytes
```

| Byte | Signal | Type | Description |
|---:|---|---|---|
| B0 | driveCmd | uint8 | 수동 주행 명령 |
| B1 | steeringCmd | uint8 | 수동 조향 명령 |
| B2 | gearStatus | uint8 | 기어 상태 |
| B3 | pcaActivated | uint8 | PDW/PCA 활성화 상태 |
| B4~B5 | lineAngle | int16 | 주차선/차량 기준 각도 보정값 |

`lineAngle`은 자동출차 완료 각도 판단에 사용됩니다.

Raspberry Pi에서 판단한 주차선 또는 차량 기준 각도를 판단 ECU가 받아, IMU yaw 기반 목표 yaw 계산에 반영합니다.

---

### 9.1.3 `0x300` Raspberry Pi → Judgment ECU

Raspberry Pi가 자동출차 시작 또는 정지 명령을 판단 ECU로 전달합니다.

```text
Classical CAN
Payload: 1 byte
```

| Byte | Signal | Value | Description |
|---:|---|---:|---|
| B0 | autoParkingStart | `0x00` | NORMAL |
| B0 | autoParkingStart | `0x01` | START_STRAIGHT |
| B0 | autoParkingStart | `0x02` | START_LEFT |
| B0 | autoParkingStart | `0x03` | START_RIGHT |
| B0 | autoParkingStart | `0x04` | STOP |

---

## 9.2 Tx Messages

### 9.2.1 `0x100` Judgment ECU → Motor ECU

판단 ECU가 Motor ECU로 최종 차량 제어 명령을 송신합니다.

```text
Classical CAN
Payload: 2 bytes
```

| Byte | Signal | Description |
|---:|---|---|
| B0 | driveCmd | 전진/정지/후진 명령 |
| B1 | steeringCmd | 좌/중앙/우 조향 명령 |

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

### 9.2.2 `0x400` Judgment ECU → Raspberry Pi

판단 ECU가 PDW 판단 결과를 Raspberry Pi/HMI로 송신합니다.

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
| B10 | pcaActivated | PDW/PCA 활성화 상태 |
| B11 | vehicleSpeed | 차량 속도 |
| B12 | gearStatus | 기어 상태 |
| B13 | emergencyStop | 위험 감지 상태 |

PDW level 값은 다음과 같습니다.

| Value | Level | Meaning |
|---:|---|---|
| `0x00` | NO_OBSTACLE | 장애물 없음 |
| `0x01` | SAFE | 안전 |
| `0x02` | CAUTION | 주의 |
| `0x03` | NEAR | 가까움 |
| `0x04` | DANGER | 위험 |

---

### 9.2.3 `0x401` Judgment ECU → Raspberry Pi

판단 ECU가 자동출차 상태를 Raspberry Pi/HMI로 송신합니다.

```text
Classical CAN
Payload: 1 byte
```

| Byte | Signal | Value | Description |
|---:|---|---:|---|
| B0 | autoExitStatus | `0x00` | IDLE / NORMAL |
| B0 | autoExitStatus | `0x01` | RUNNING |
| B0 | autoExitStatus | `0x02` | COMPLETE |
| B0 | autoExitStatus | `0x03` | STOPPED / BLOCKED |

---

## 10. PDW Service

PDW Service는 10방향 초음파 거리값을 위험 level로 변환합니다.

거리 판단 기준은 다음과 같습니다.

| Distance | Level |
|---:|---|
| 0 mm | NO_OBSTACLE |
| 0 < distance <= 200 mm | DANGER |
| 200 mm < distance <= 250 mm | NEAR |
| 250 mm < distance <= 300 mm | CAUTION |
| 300 mm < distance <= 1000 mm | SAFE |
| 1000 mm < distance | NO_OBSTACLE |

PDW 판단 결과는 다음 상태로 관리됩니다.

```text
AppPdwState
  ├─ enabled
  ├─ dangerDetected
  ├─ level[10]
  └─ distanceMm[10]
```

PDW는 일반 주행 중 주차 보조 기능으로 동작하며, 자동출차 중에도 주변 장애물 판단에 사용됩니다.

---

## 11. Drive Service

Drive Service는 Motor ECU로 송신할 최종 `0x100` 제어 명령을 결정합니다.

판단 ECU 내부에는 여러 제어 후보가 존재할 수 있습니다.

- Raspberry Pi의 수동 주행 명령
- AutoExitService의 자동출차 제어 명령
- PDW 위험 감지에 따른 정지 명령
- Raspberry Pi timeout에 따른 정지 명령

Drive Service는 이 중 가장 우선순위가 높은 명령을 선택해 Motor ECU로 송신합니다.

우선순위는 다음과 같습니다.

```text
1. Raspberry Pi timeout
   → stop / center

2. AutoExitService active
   → AutoExit 제어 명령

3. PDW dangerDetected
   → stop / center

4. Normal driving
   → Raspberry Pi 수동 명령
```

---

## 12. AutoExit Service

AutoExit Service는 Raspberry Pi에서 전달한 자동출차 명령을 기반으로 출차 동작을 수행합니다.

자동출차는 다음 단계로 진행됩니다.

```text
0x300 자동출차 명령 수신
  ↓
출차 방향 결정
  ↓
현재 초음파 상태 확인
  ↓
출차 전략 선택
  ↓
회피 또는 기본 profile 수행
  ↓
IMU yaw 및 lineAngle 기반 정렬 확인
  ↓
완료 상태 송신
```

---

## 13. AutoExit Motion Profile

자동출차는 방향별 motion profile을 기반으로 동작합니다.

각 profile은 drive command, steering command, duration으로 구성된 step 배열입니다.

### 13.1 Straight Exit

```text
1. forward + center
2. stop + center
```

직진 출차는 차량을 앞으로 이동시킨 뒤 정지합니다.

---

### 13.2 Right Exit

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

우측 출차는 전진, 우측 조향, 후진, 반대 조향 보정을 조합하여 차량이 오른쪽으로 빠져나오도록 구성되어 있습니다.

---

### 13.3 Left Exit

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

좌측 출차는 우측 출차 profile을 좌우 반전한 구조입니다.

---

## 14. AutoExit Planner

AutoExit Planner는 출차 시작 시 주변 초음파 상태를 분석하여 출차 전략을 선택합니다.

전략은 다음 세 가지입니다.

| Strategy | Description |
|---|---|
| NORMAL | 기본 출차 profile 수행 |
| AVOID_AND_RESUME | 반대 방향으로 먼저 회피한 뒤 기본 출차 profile 수행 |
| BLOCKED | 출차가 어렵다고 판단하고 정지 |

Planner는 출차 방향의 옆 공간을 확인합니다.

우측 출차 시에는 오른쪽 앞/뒤 센서 상태를 확인합니다.

```text
RIGHT_FRONT
RIGHT_BEHIND
FRONT_RIGHT
```

좌측 출차 시에는 왼쪽 앞/뒤 센서 상태를 확인합니다.

```text
LEFT_FRONT
LEFT_BEHIND
FRONT_LEFT
```

측면 상태는 다음과 같이 분류됩니다.

| Risk | Description |
|---|---|
| SAFE | 출차 가능 |
| CRITICAL | 매우 가까움 |
| NARROW_BOTH | 앞/뒤 모두 가까움 |
| NEAR_FRONT | 앞쪽이 가까움 |
| NEAR_REAR | 뒤쪽이 가까움 |
| TILTED_FRONT | 앞쪽으로 갈수록 가까움 |
| TILTED_REAR | 뒤쪽이 더 가까움 |

회피 전략은 출차 방향의 반대 방향으로 먼저 공간을 확보한 뒤, 다시 원래 출차 방향으로 정렬하는 방식입니다.

```text
좌측 출차:
  오른쪽으로 먼저 회피
  → 왼쪽으로 다시 정렬
  → 좌측 출차 profile 수행

우측 출차:
  왼쪽으로 먼저 회피
  → 오른쪽으로 다시 정렬
  → 우측 출차 profile 수행
```

---

## 15. AutoExit Monitor

AutoExit Monitor는 자동출차 상태와 yaw 기반 정렬 판단을 담당합니다.

주요 역할은 다음과 같습니다.

- 자동출차 시작 yaw 저장
- Raspberry Pi에서 수신한 lineAngle 저장
- 출차 방향에 따른 목표 yaw 계산
- 현재 IMU yaw와 목표 yaw 비교
- 자동출차 상태를 `0x401`로 송신

목표 yaw 계산 흐름은 다음과 같습니다.

```text
startYaw
  + targetTurnAngle
  + lineAngle compensation
  → targetYaw
```

이를 통해 판단 ECU는 자동출차가 단순 시간 기반으로 끝나는 것이 아니라, 차량이 목표 각도에 도달했는지도 함께 판단합니다.

---

## 16. Build and Run

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

## 17. Execution Flow

최종 실행 흐름은 다음과 같습니다.

```text
1. Sensor ECU가 0x200으로 초음파/IMU/속도 정보 송신
2. Raspberry Pi가 0x201으로 차량 상태, 수동 명령, lineAngle 송신
3. Raspberry Pi가 필요 시 0x300으로 자동출차 명령 송신
4. App_Can이 수신값을 최신 mailbox에 저장
5. RxService가 CAN raw data를 App 구조체로 변환
6. PDW Service가 초음파 거리 기반 위험 level 판단
7. AutoExitService가 자동출차 명령과 주변 상태를 기반으로 출차 제어 명령 생성
8. DriveService가 최종 0x100 제어 명령 선택
9. Motor ECU가 0x100을 수신해 차량 구동
10. StatusTxService와 AutoExitMonitor가 0x400/0x401 상태를 Raspberry Pi/HMI로 송신
```

---

## 18. Summary

PCA-Control-ECU는 PCA-Overdrive 시스템에서 판단 ECU 역할을 수행합니다.

이 ECU는 Sensor ECU와 Raspberry Pi로부터 들어오는 CAN 데이터를 기반으로 차량 주변 상태와 사용자 명령을 해석하고, PDW 및 자동출차 판단을 수행합니다.

특히 자동출차에서는 초음파 기반 주변 공간 판단, 회피 전략, IMU yaw, Raspberry Pi에서 받은 주차선 각도 보정값을 함께 사용하여 출차 동작을 수행합니다.

최종적으로 판단 ECU는 Motor ECU로 `0x100` 제어 명령을 송신하고, Raspberry Pi/HMI로 `0x400` PDW 상태와 `0x401` 자동출차 상태를 송신합니다.
