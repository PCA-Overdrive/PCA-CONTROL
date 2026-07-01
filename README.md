# PCA-Control-ECU

PCA-Control-ECU는 PCA-Overdrive 프로젝트에서 사용하는 **판단 ECU(Control ECU)** 코드입니다.

이 ECU는 차량의 여러 ECU 중에서 **센서값을 읽고, 현재 상황을 판단하고, 최종 제어 명령을 만드는 중간 판단 노드** 역할을 합니다.

쉽게 말하면 다음과 같습니다.

```text
Sensor ECU가 거리와 IMU 값을 보냄
        ↓
판단 ECU가 위험한지, 출차 가능한지 판단함
        ↓
Motor ECU로 최종 주행/조향 명령을 보냄
        ↓
Raspberry Pi/HMI에는 현재 상태를 다시 알려줌
```

이 프로젝트는 Infineon AURIX TC375 기반 FreeRTOS 환경에서 동작하며, CAN/CAN FD 통신을 사용합니다.

---

## 1. 이 ECU가 하는 일

전체 시스템에는 Raspberry Pi, Sensor ECU, Motor ECU, 판단 ECU가 있습니다.

각 장치의 역할은 다음과 같습니다.

| 장치 | 역할 |
|---|---|
| Raspberry Pi | 사용자의 명령, HMI, 자동출차 시작/정지 명령 담당 |
| Sensor ECU | 초음파 거리, IMU yaw, 차량 속도 측정 |
| 판단 ECU | 센서값과 명령을 보고 PDW/자동출차/제어 판단 수행 |
| Motor ECU | 판단 ECU가 보낸 drive/steering 명령으로 실제 차량 구동 |

판단 ECU는 직접 센서를 측정하거나 모터를 직접 제어하지 않습니다.

대신 다음 일을 담당합니다.

```text
1. Sensor ECU에서 초음파 거리, IMU yaw, 차량 속도를 받음
2. Raspberry Pi에서 수동 주행 명령과 자동출차 명령을 받음
3. 초음파 거리값을 보고 장애물 위험도를 판단함
4. 자동출차가 가능한 상황인지 판단함
5. 필요하면 회피 동작을 먼저 수행함
6. 최종 driveCmd / steeringCmd를 만들어 Motor ECU로 보냄
7. 현재 PDW 상태와 자동출차 상태를 Raspberry Pi/HMI로 보냄
```

---

## 2. 전체 시스템 흐름

```text
                  ┌─────────────────────┐
                  │    Raspberry Pi      │
                  │  HMI / 사용자 명령    │
                  └──────────┬──────────┘
                             │
                 0x201 차량 상태 / 수동 명령
                 0x300 자동출차 명령
                             │
                             ▼
┌───────────────────────────────────────────────────────┐
│                  Judgment ECU                         │
│                PCA-Control-ECU                        │
│                                                       │
│  1. CAN 수신값 최신값 관리                            │
│  2. CAN raw data를 App 내부 구조체로 변환              │
│  3. 초음파 기반 PDW 판단                              │
│  4. 자동출차 FSM / 회피 판단 / yaw 판단                │
│  5. 최종 차량 제어 명령 선택                          │
│                                                       │
│  Tx: 0x400 PDW 상태                                   │
│  Tx: 0x401 자동출차 상태                              │
│  Tx: 0x100 차량 제어 명령                             │
└───────────────▲───────────────────────┬───────────────┘
                │                       │
                │ 0x200 센서 정보        │ 0x100 제어 명령
                │                       │
┌───────────────┴──────────────┐        ▼
│          Sensor ECU           │   ┌────────────────────┐
│ 초음파 / IMU / 속도 측정       │   │     Motor ECU       │
└──────────────────────────────┘   │ 실제 주행/조향 제어 │
                                   └────────────────────┘
```

---

## 3. 처음 보는 사람을 위한 용어 설명

| 용어 | 의미 |
|---|---|
| ECU | 차량 안에서 특정 기능을 담당하는 제어기 |
| 판단 ECU | 센서값과 명령을 보고 어떤 동작을 해야 할지 결정하는 ECU |
| CAN | ECU끼리 데이터를 주고받는 차량용 통신 방식 |
| CAN FD | CAN보다 한 번에 더 많은 데이터를 보낼 수 있는 확장 방식 |
| PDW | Parking Distance Warning, 주차 거리 경고 기능 |
| HMI | Human Machine Interface, 사용자가 보는 화면 또는 UI |
| yaw | 차량이 좌우로 얼마나 회전했는지를 나타내는 각도 |
| driveCmd | 전진/정지/후진을 나타내는 주행 명령 |
| steeringCmd | 좌/중앙/우 조향을 나타내는 조향 명령 |
| FSM | Finite State Machine, 상태를 단계별로 바꾸며 동작하는 구조 |
| mailbox | 최신 수신값을 저장해두는 공간 |

---

## 4. 주요 기능

현재 판단 ECU App에는 다음 기능이 구현되어 있습니다.

```text
CAN 수신/송신
  - Sensor ECU, Raspberry Pi, Motor ECU와 CAN/CAN FD 통신

Rx 최신값 관리
  - 수신된 CAN 메시지를 ID별 최신값 mailbox에 저장

RxService
  - CAN raw message를 App 내부 구조체로 변환

PDW Service
  - 초음파 거리값을 위험 level로 변환
  - SAFE / CAUTION / NEAR / DANGER 판단

StatusTxService
  - PDW 판단 결과를 0x400으로 Raspberry Pi/HMI에 송신

DriveService
  - Motor ECU로 최종 0x100 차량 제어 명령 송신
  - Raspberry Pi timeout 시 안전 정지
  - 자동출차 명령이 있으면 자동출차 제어 명령 우선 적용

AutoExitService
  - 0x300 자동출차 명령 처리
  - 직진 / 좌측 / 우측 출차 FSM 수행
  - 초음파 상태 기반 NORMAL / AVOID / BLOCKED 전략 선택
  - 회피 후 기본 출차 profile 재개
  - IMU yaw 기반 목표 각도 판단
  - 0x401 자동출차 상태 송신
```

---

## 5. 폴더 구조

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

## 6. 폴더별 역할

### 6.1 `Drivers/McmcanFd`

TC375의 MCMCAN 하드웨어를 직접 다루는 Driver 계층입니다.

이 계층은 실제 CAN/CAN FD 송수신을 담당합니다.

```text
역할:
  - CAN controller 초기화
  - CAN ID별 수신 처리
  - CAN/CAN FD frame 송신
  - App 계층이 사용할 송수신 함수 제공
```

App 계층은 하드웨어 레지스터를 직접 만지지 않고, 이 Driver 계층을 통해 CAN을 사용합니다.

---

### 6.2 `App/App_Can`

`App_Can`은 Driver와 Service 사이에 있는 CAN wrapper 계층입니다.

Driver에서 받은 CAN 메시지를 바로 판단 로직에 넣지 않고, 먼저 최신값 mailbox에 저장합니다.

```text
McmcanFd
  ↓
App_Can
  ↓
각 Service
```

이 구조를 쓰는 이유는 다음과 같습니다.

```text
1. 수신과 판단을 분리하기 위해
2. 최신 센서값만 관리하기 위해
3. 여러 Service가 같은 최신값을 읽을 수 있게 하기 위해
4. Driver 코드와 App 판단 코드를 분리하기 위해
```

---

### 6.3 `App/App_RxService`

`App_RxService`는 CAN raw message를 App에서 쓰기 편한 구조체로 바꿔주는 계층입니다.

예를 들어 CAN으로 들어온 `0x200` 메시지는 byte 배열에 가깝습니다.

하지만 판단 로직에서 byte 위치를 계속 직접 계산하면 코드가 복잡해집니다.

그래서 `RxService`가 다음처럼 변환합니다.

```text
0x200 CAN message
  → AppUltrasonicState

0x201 CAN message
  → AppRpiInputState

0x300 CAN message
  → AppAutoParkingState
```

즉, Service들은 CAN byte를 직접 해석하지 않고, 의미 있는 구조체만 사용합니다.

---

### 6.4 `App/App_PdwService`

`App_PdwService`는 초음파 거리값을 보고 주차 거리 경고 상태를 판단합니다.

초음파 센서값은 단순한 거리값입니다.

```text
예:
  frontDist = 500 mm
  behindDist = 180 mm
```

PDW Service는 이 거리값을 사람이 이해하기 쉬운 level로 바꿉니다.

```text
500 mm → SAFE
180 mm → DANGER
```

최종적으로 다음 정보를 관리합니다.

```text
AppPdwState
  ├─ enabled
  ├─ dangerDetected
  ├─ level[10]
  └─ distanceMm[10]
```

---

### 6.5 `App/App_StatusTxService`

`App_StatusTxService`는 PDW 판단 결과를 Raspberry Pi/HMI로 보내는 역할입니다.

PDW Service가 내부적으로 판단만 하고 끝나면 HMI는 현재 상태를 알 수 없습니다.

그래서 StatusTxService가 `0x400` 메시지를 만들어 송신합니다.

```text
PDW 판단 결과
  ↓
0x400 CAN FD message
  ↓
Raspberry Pi / HMI
```

---

### 6.6 `App/App_DriveService`

`App_DriveService`는 Motor ECU로 최종 제어 명령을 보내는 역할입니다.

판단 ECU 안에는 여러 명령 후보가 있을 수 있습니다.

```text
1. Raspberry Pi가 보낸 수동 주행 명령
2. 자동출차 Service가 만든 출차 제어 명령
3. PDW danger로 인한 정지 명령
4. Raspberry Pi timeout으로 인한 정지 명령
```

DriveService는 이 중에서 가장 우선순위가 높은 명령을 선택해서 Motor ECU로 보냅니다.

```text
최종 선택된 driveCmd / steeringCmd
  ↓
0x100 CAN message
  ↓
Motor ECU
```

---

### 6.7 `App/App_AutoExitService`

`App_AutoExitService`는 자동출차 기능을 담당합니다.

Raspberry Pi에서 `0x300`으로 자동출차 시작 명령이 들어오면, 판단 ECU는 현재 초음파 상태와 IMU yaw를 보고 출차를 진행합니다.

자동출차는 단순히 정해진 명령을 순서대로 보내는 기능이 아닙니다.

현재 코드에서는 다음 판단을 함께 수행합니다.

```text
1. 출차 방향 선택
2. 출차 방향 주변 장애물 상태 확인
3. 바로 출차할지, 먼저 회피할지, 막힌 상태인지 판단
4. 필요하면 반대 방향으로 먼저 회피
5. 회피 후 다시 출차 방향으로 정렬
6. 기본 출차 profile 실행
7. 마지막에는 yaw 기준으로 각도 도달 여부 확인
```

---

## 7. 내부 구현 구조

판단 ECU App은 다음 순서로 데이터를 처리합니다.

```text
CAN 수신
  ↓
최신값 저장
  ↓
App 구조체로 변환
  ↓
PDW / 자동출차 판단
  ↓
상태 송신 / 제어 명령 송신
```

구현 계층으로 보면 다음과 같습니다.

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

각 계층의 책임은 다음과 같습니다.

| 계층 | 책임 |
|---|---|
| `McmcanFd` | TC375 CAN 하드웨어 송수신 |
| `App_Can` | CAN 수신값 최신 mailbox 관리, Tx wrapper 제공 |
| `App_RxService` | CAN raw data를 App 내부 구조체로 변환 |
| `App_PdwService` | 초음파 거리 기반 위험 level 판단 |
| `App_AutoExitService` | 자동출차 FSM, 회피 판단, yaw 판단 |
| `App_StatusTxService` | 0x400 PDW 상태 송신 |
| `App_DriveService` | 0x100 최종 차량 제어 명령 송신 |

---

## 8. RTOS Task 구조

`Cpu0_Main.c`에서는 `App_Init()`을 호출하고 FreeRTOS scheduler를 시작합니다.

```text
Cpu0_Main.c
  ↓
App_Init()
  ↓
FreeRTOS Task 생성
  ↓
vTaskStartScheduler()
```

`App_Init()`에서는 다음 Task들을 생성합니다.

```text
App_Init()
  ├─ AppCan_Init()
  ├─ AppPdwService_Init()
  ├─ AppAutoExitService_Init()
  │
  ├─ AppCan_RxTask
  ├─ AppPdwService_Task
  ├─ AppDriveService_Task
  ├─ AppStatusTxService_Task
  └─ AppAutoExitService_Task
```

현재 Task 주기는 다음과 같습니다.

| Task | 주기 | 역할 |
|---|---:|---|
| CAN Rx | 1 ms | CAN 수신 프레임을 latest mailbox에 반영 |
| PDW Service | 10 ms | 초음파 거리값을 PDW level로 판단 |
| AutoExit Service | 10 ms | 자동출차 명령 처리, FSM 진행, 회피/yaw 판단 |
| Status Tx | 10 ms | 0x400 PDW 상태 송신 |
| Drive Service | 12 ms | 최종 0x100 차량 제어 명령 송신 |

Task를 나눈 이유는 다음과 같습니다.

```text
1. CAN 수신, 판단, 송신 책임을 분리하기 위해
2. 특정 기능을 수정해도 다른 기능에 영향을 줄이기 위해
3. RTOS task 단위로 디버깅하기 위해
4. 수신 주기와 판단 주기, 송신 주기를 다르게 가져가기 위해
```

---

## 9. CAN Rx Mailbox Policy

수신 메시지는 CAN ID별 latest mailbox 방식으로 관리합니다.

```text
0x200 Sensor ECU → Judgment ECU
  → g_ultrasonicMailbox

0x201 Raspberry Pi → Judgment ECU
  → g_vehicleStatusMailbox

0x300 Raspberry Pi → Judgment ECU
  → g_autoParkingMailbox
```

각 mailbox는 길이 1의 FreeRTOS queue입니다.

```c
xQueueCreate(1u, sizeof(...));
```

새로운 CAN frame이 들어오면 기존 값을 덮어씁니다.

```c
xQueueOverwrite(...);
```

Service에서 읽을 때는 값을 제거하지 않고 읽습니다.

```c
xQueuePeek(...);
```

이 구조의 의미는 다음과 같습니다.

```text
일반 queue:
  읽으면 값이 사라짐

latest mailbox:
  읽어도 값이 남아 있음
  항상 가장 최근 값만 유지함
```

이 프로젝트에서 latest mailbox를 쓰는 이유는 센서값과 차량 상태는 오래된 이력보다 최신 상태가 중요하기 때문입니다.

---

## 10. CAN Interface

## 10.1 Rx Messages

### 0x200 Sensor ECU → Judgment ECU

Sensor ECU가 판단 ECU로 보내는 센서 정보입니다.

```text
CAN FD
Payload: 23 bytes
DLC: 12
실제 CAN FD frame 크기: 24 bytes
```

포함 정보는 다음과 같습니다.

```text
초음파 거리 10개
IMU yaw 1개
차량 속도 1개
```

초음파 방향은 전방을 시작으로 시계방향 10채널입니다.

| Byte | Signal | Type | 설명 |
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

Raspberry Pi가 판단 ECU로 보내는 차량 상태 및 수동 제어 입력입니다.

```text
Classical CAN
Payload: 6 bytes
```

| Byte | Signal | Type | 설명 |
|---:|---|---|---|
| B0 | driveCmd | uint8 | 주행 명령 |
| B1 | steeringCmd | uint8 | 조향 명령 |
| B2 | gearStatus | uint8 | 기어 상태 |
| B3 | pcaActivated | uint8 | PDW/PCA 활성화 상태 |
| B4~B5 | lineAngle | int16 | 주차선 또는 차량 기준 각도 보정값 |

주의할 점은 `lineAngle`입니다.

기존 문서나 예전 설명에는 0x201이 4 bytes로 적혀 있을 수 있습니다.  
하지만 현재 코드 기준으로는 `lineAngle`이 추가되어 있으므로 6 bytes입니다.

---

### 0x300 Raspberry Pi → Judgment ECU

Raspberry Pi가 판단 ECU로 보내는 자동출차 명령입니다.

```text
Classical CAN
Payload: 1 byte
```

| Byte | Signal | Value | 설명 |
|---:|---|---:|---|
| B0 | autoParkingStart | 0x00 | NORMAL |
| B0 | autoParkingStart | 0x01 | START_STRAIGHT |
| B0 | autoParkingStart | 0x02 | START_LEFT |
| B0 | autoParkingStart | 0x03 | START_RIGHT |
| B0 | autoParkingStart | 0x04 | STOP |

---

## 10.2 Tx Messages

### 0x400 Judgment ECU → Raspberry Pi

판단 ECU가 PDW 판단 결과를 Raspberry Pi/HMI로 보내는 메시지입니다.

```text
CAN FD
Payload: 14 bytes
DLC: 10
실제 CAN FD frame 크기: 16 bytes
```

| Byte | Signal | 설명 |
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
| B13 | emergencyStop | PDW danger 감지 상태 |

PDW level 값은 다음과 같습니다.

| Value | Level | 의미 |
|---:|---|---|
| 0x00 | NO_OBSTACLE | 장애물 없음 |
| 0x01 | SAFE | 안전 거리 |
| 0x02 | CAUTION | 주의 거리 |
| 0x03 | NEAR | 가까움 |
| 0x04 | DANGER | 위험 거리 |

---

### 0x401 Judgment ECU → Raspberry Pi

판단 ECU가 자동출차 진행 상태를 Raspberry Pi/HMI로 보내는 메시지입니다.

```text
Classical CAN
Payload: 1 byte
```

| Byte | Signal | Value | 설명 |
|---:|---|---:|---|
| B0 | autoExitStatus | 0x00 | IDLE / NORMAL |
| B0 | autoExitStatus | 0x01 | RUNNING |
| B0 | autoExitStatus | 0x02 | COMPLETE |
| B0 | autoExitStatus | 0x03 | STOPPED |

현재 코드에는 내부적으로 BLOCKED 개념도 존재합니다.

BLOCKED는 사용자가 STOP을 누른 것이 아니라, 장애물 때문에 출차를 계속할 수 없다고 판단한 상태입니다.

외부 HMI에서 STOPPED와 BLOCKED를 구분하고 싶다면 다음처럼 확장할 수 있습니다.

```text
0x00 IDLE / NORMAL
0x01 RUNNING
0x02 COMPLETE
0x03 STOPPED
0x04 BLOCKED
```

---

### 0x100 Judgment ECU → Motor ECU

판단 ECU가 Motor ECU로 보내는 최종 차량 제어 명령입니다.

```text
Classical CAN
Payload: 2 bytes
```

| Byte | Signal | 설명 |
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

## 11. App Types

`App_Types.h`는 App 계층에서 공통으로 사용하는 enum과 struct를 모아둔 파일입니다.

CAN message 구조체를 그대로 사용하지 않고 App 전용 타입을 사용하는 이유는 다음과 같습니다.

```text
1. CAN byte 구조와 판단 로직을 분리하기 위해
2. 판단 로직에서 의미 있는 이름으로 값을 사용하기 위해
3. 나중에 CAN payload가 바뀌어도 판단 로직 수정 범위를 줄이기 위해
```

주요 타입은 다음과 같습니다.

| Type | 설명 |
|---|---|
| `AppGearStatus` | 기어 상태 |
| `AppPdwLevel` | PDW 위험 level |
| `AppPdwDirection` | 초음파 센서 방향 |
| `AppRpiInputState` | 0x201을 App 내부에서 사용하는 구조체 |
| `AppUltrasonicState` | 0x200을 App 내부에서 사용하는 구조체 |
| `AppAutoParkingState` | 0x300을 App 내부에서 사용하는 구조체 |
| `AppPdwState` | PDW 판단 결과 구조체 |

---

## 12. PDW Service

PDW Service는 초음파 거리값을 위험 level로 바꾸는 기능입니다.

거리값만 보면 HMI나 다른 로직에서 판단하기 어렵기 때문에, 다음처럼 level로 변환합니다.

```text
거리값
  ↓
PDW level
```

거리 판단 기준은 다음과 같습니다.

| Distance | PDW Level | 의미 |
|---:|---|---|
| 0 mm | NO_OBSTACLE | 센서가 장애물을 보지 못함 |
| 0 < distance <= 200 mm | DANGER | 매우 가까움 |
| 200 mm < distance <= 250 mm | NEAR | 가까움 |
| 250 mm < distance <= 300 mm | CAUTION | 주의 |
| 300 mm < distance <= 1000 mm | SAFE | 안전 |
| 1000 mm < distance | NO_OBSTACLE | 충분히 멀어서 장애물 없음으로 판단 |

PDW 판단 결과는 다음 구조로 저장됩니다.

```text
AppPdwState
  ├─ enabled
  │   └─ 현재 PDW 기능이 활성화되어 있는지
  │
  ├─ dangerDetected
  │   └─ 10개 방향 중 하나라도 DANGER인지
  │
  ├─ level[10]
  │   └─ 10개 방향의 PDW level
  │
  └─ distanceMm[10]
      └─ 10개 방향의 원본 거리값
```

PDW는 다음 조건에서 활성화됩니다.

```text
조건 1. 일반 PDW 상황
  Raspberry Pi에서 PDW/PCA 활성화
  AND 기어가 P가 아님
  AND 차량 속도가 기준 이하

조건 2. 자동출차 상황
  자동출차 명령이 NORMAL/STOP이 아님
```

즉, 사용자가 일반 주행 중 PDW를 켜도 동작하고, 자동출차가 진행 중일 때도 PDW 판단이 사용됩니다.

---

## 13. StatusTxService

StatusTxService는 판단 ECU 내부의 PDW 결과를 Raspberry Pi/HMI로 보내는 송신 Task입니다.

PDW Service가 내부적으로 `AppPdwState`를 만들면, StatusTxService는 그 값을 읽어 `0x400 DistanceLevelCmd`로 변환합니다.

```text
AppPdwState
  ↓
DistanceLevelCmd_t
  ↓
CAN ID 0x400
  ↓
Raspberry Pi / HMI
```

`0x400`에는 다음 정보가 포함됩니다.

```text
1. 10개 방향의 PDW level
2. PDW/PCA 활성화 상태
3. 차량 속도
4. 기어 상태
5. emergencyStop 상태
```

---

## 14. DriveService

DriveService는 Motor ECU로 최종 `0x100` 차량 제어 명령을 보내는 Task입니다.

여기서 중요한 점은 **여러 명령 후보 중 무엇을 우선할지 결정한다는 것**입니다.

우선순위는 다음과 같습니다.

```text
1순위. Raspberry Pi timeout
  - Raspberry Pi 명령이 일정 시간 이상 들어오지 않으면 정지
  - driveCmd = 127
  - steeringCmd = 127

2순위. AutoExitService active
  - 자동출차가 진행 중이면 자동출차 제어 명령을 사용

3순위. PDW dangerDetected
  - PDW에서 DANGER가 감지되면 정지

4순위. 일반 주행
  - Raspberry Pi에서 받은 driveCmd / steeringCmd를 그대로 전달
```

흐름으로 보면 다음과 같습니다.

```text
Raspberry Pi timeout?
  ├─ YES → 정지 명령 송신
  └─ NO
      ↓
AutoExit 명령 있음?
  ├─ YES → 자동출차 명령 송신
  └─ NO
      ↓
PDW DANGER?
  ├─ YES → 정지 명령 송신
  └─ NO
      ↓
RPi 수동 명령 송신
```

---

## 15. AutoExitService

AutoExitService는 자동출차 기능을 담당합니다.

Raspberry Pi에서 `0x300`으로 자동출차 명령이 들어오면, 판단 ECU는 현재 주변 상태를 보고 출차를 수행합니다.

지원 명령은 다음과 같습니다.

| Command | 설명 |
|---|---|
| NORMAL | 자동출차 대기 |
| START_STRAIGHT | 직진 출차 시작 |
| START_LEFT | 좌측 출차 시작 |
| START_RIGHT | 우측 출차 시작 |
| STOP | 자동출차 중지 |

자동출차 흐름은 다음과 같습니다.

```text
0x300 명령 수신
  ↓
출차 방향 결정
  ↓
현재 PDW 상태 확인
  ↓
전략 선택
  ├─ NORMAL
  ├─ AVOID_AND_RESUME
  └─ BLOCKED
  ↓
선택된 전략에 따라 제어 명령 생성
  ↓
DriveService가 0x100으로 Motor ECU에 송신
```

---

## 16. AutoExit FSM

자동출차는 FSM 방식으로 동작합니다.

FSM은 상태를 하나씩 바꾸면서 동작하는 구조입니다.

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

각 상태의 의미는 다음과 같습니다.

| State | 의미 |
|---|---|
| `IDLE` | 자동출차 대기 상태 |
| `START_STOP` | 출차 시작 전 정지 상태 |
| `SELECT_STRATEGY` | 주변 상태를 보고 전략 선택 |
| `RUN_PROFILE` | 기본 출차 profile 실행 |
| `AVOID_ESCAPE` | 출차 방향 반대쪽으로 먼저 회피 |
| `AVOID_STOP_1` | 회피 후 잠깐 정지 |
| `AVOID_REALIGN` | 다시 출차 방향으로 정렬 |
| `AVOID_STOP_2` | 정렬 후 잠깐 정지 |
| `BLOCKED` | 출차 불가 상태 |

---

## 17. AutoExit Motion Profile

자동출차 profile은 `App_AutoExitProfile.c`에 하드코딩된 주행 순서입니다.

각 step은 다음 정보를 가집니다.

```text
driveCmd
steeringCmd
durationMs
```

### 17.1 Straight Profile

직진 출차는 가장 단순합니다.

```text
1. forward + center
2. stop + center
```

의미는 다음과 같습니다.

```text
앞으로 곧게 나감
  ↓
정지
```

---

### 17.2 Right Exit Profile

우측 출차는 오른쪽으로 빠져나가기 위한 전진/후진/조향 조합입니다.

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

의미는 다음과 같습니다.

```text
앞으로 조금 나감
  ↓
오른쪽으로 꺾으며 나감
  ↓
공간 확보를 위해 후진
  ↓
다시 오른쪽으로 나감
  ↓
필요하면 반대 조향 후진으로 자세 보정
  ↓
오른쪽으로 빠져나간 뒤 정렬
  ↓
정지
```

---

### 17.3 Left Exit Profile

좌측 출차는 우측 출차 profile의 좌우 반전입니다.

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

의미는 다음과 같습니다.

```text
앞으로 조금 나감
  ↓
왼쪽으로 꺾으며 나감
  ↓
공간 확보를 위해 후진
  ↓
다시 왼쪽으로 나감
  ↓
필요하면 반대 조향 후진으로 자세 보정
  ↓
왼쪽으로 빠져나간 뒤 정렬
  ↓
정지
```

---

## 18. AutoExit Planner

AutoExit Planner는 자동출차 시작 시 주변 상태를 보고 전략을 선택합니다.

전략은 세 가지입니다.

| Strategy | 설명 |
|---|---|
| `NORMAL` | 기본 출차 profile을 바로 수행 |
| `AVOID_AND_RESUME` | 먼저 반대 방향으로 회피한 뒤 기본 profile 재개 |
| `BLOCKED` | 출차가 위험하다고 판단하고 정지 |

Planner는 단순히 장애물이 있는지만 보지 않습니다.

출차 방향의 앞쪽과 뒤쪽 거리 차이를 보고, 옆 차량 또는 장애물이 어떤 식으로 기울어져 있는지도 봅니다.

예를 들어 우측 출차라면 오른쪽 앞/뒤 센서를 봅니다.

```text
우측 출차 시 주요 센서:
  RIGHT_FRONT
  RIGHT_BEHIND
  FRONT_RIGHT
```

좌측 출차라면 좌측 방향 센서를 봅니다.

```text
좌측 출차 시 주요 센서:
  LEFT_FRONT
  LEFT_BEHIND
  FRONT_LEFT
```

측면 위험 상태는 다음처럼 분류할 수 있습니다.

| Risk | 설명 |
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
  먼저 오른쪽으로 escape
  → 다시 왼쪽으로 realign
  → 좌측 출차 profile 재개

우측 출차:
  먼저 왼쪽으로 escape
  → 다시 오른쪽으로 realign
  → 우측 출차 profile 재개
```

이 구조를 쓰는 이유는 옆 차량이 가까운 경우 바로 출차 방향으로 꺾으면 앞코너나 옆면이 간섭될 수 있기 때문입니다.

---

## 19. AutoExit Monitor

AutoExit Monitor는 자동출차 상태와 yaw 판단을 담당합니다.

주요 역할은 다음과 같습니다.

```text
1. 자동출차 시작 상태 기록
2. 시작 yaw 저장
3. Raspberry Pi에서 받은 lineAngle 반영
4. 목표 회전각 계산
5. 목표 yaw 계산
6. 현재 yaw와 목표 yaw 비교
7. 자동출차 상태를 0x401로 송신
```

yaw 판단은 다음 흐름으로 이해할 수 있습니다.

```text
출차 시작 yaw
  + 목표 회전각
  + lineAngle 보정
  ↓
목표 yaw
```

자동출차 마지막 회전 구간에서는 단순히 정해진 시간만 기다리는 것이 아니라, 목표 yaw에 도달했는지도 확인할 수 있습니다.

---

## 20. Build / Run

이 프로젝트는 Infineon AURIX TC375 기반 FreeRTOS 프로젝트입니다.

기본 실행 흐름은 다음과 같습니다.

```text
Cpu0_Main.c
  ↓
App_Init()
  ↓
Task 생성
  ↓
vTaskStartScheduler()
```

CAN node 설정은 `Drivers/McmcanFd/McmcanFd_Cfg.h`에서 판단 ECU 설정을 사용해야 합니다.

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

## 21. 디버깅할 때 보는 순서

처음 이 코드를 보는 사람은 아래 순서로 보면 이해하기 쉽습니다.

```text
1. CanMsg.h
   - CAN ID와 payload 구조 확인

2. App.c
   - 어떤 Task가 생성되는지 확인

3. App_Can.c
   - CAN 수신값이 mailbox에 어떻게 저장되는지 확인

4. App_RxService.c
   - CAN raw message가 App struct로 어떻게 바뀌는지 확인

5. App_PdwService.c
   - 초음파 거리값이 PDW level로 어떻게 바뀌는지 확인

6. App_StatusTxService.c
   - 0x400 상태 메시지가 어떻게 만들어지는지 확인

7. App_DriveService.c
   - 최종 0x100 명령 우선순위 확인

8. App_AutoExitService.c
   - 자동출차 FSM 흐름 확인

9. App_AutoExitProfile.c
   - 기본 출차 주행 순서 확인

10. App_AutoExitPlanner.c
   - 회피 전략과 BLOCKED 판단 확인

11. App_AutoExitMonitor.c
   - 0x401 상태 송신과 yaw 판단 확인
```

---

## 22. Current Implementation Notes

### 22.1 0x201 Payload Size

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

### 22.2 PDW Service Input Dependency

현재 PDW Service는 0x201, 0x200, 0x300 상태를 함께 읽어 PDW 판단을 수행하는 구조입니다.

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

### 22.3 AutoExit Yaw Timeout

yaw 기반 종료 step에서는 IMU yaw가 정상적으로 갱신되지 않거나 목표 yaw에 도달하지 못하는 경우를 대비해야 합니다.

실차 안정성을 위해 yaw 기반 종료에도 timeout fallback을 두는 것이 좋습니다.

---

### 22.4 STOPPED / BLOCKED Status

현재 내부적으로는 BLOCKED 개념이 존재합니다.

외부 0x401 상태값에서 STOPPED와 BLOCKED를 구분하면 Raspberry Pi/HMI에서 자동출차 중지 원인을 더 명확하게 표시할 수 있습니다.

권장 상태값은 다음과 같습니다.

```text
0x00 IDLE / NORMAL
0x01 RUNNING
0x02 COMPLETE
0x03 STOPPED
0x04 BLOCKED
```

---

## 23. Development Status

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

## 24. Summary

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

이 구조의 핵심은 수신, 변환, 판단, 송신 책임을 분리한 것입니다.

처음 코드를 보는 사람은 전체 흐름을 다음 한 줄로 이해하면 됩니다.

```text
CAN으로 들어온 센서값과 사용자 명령을 판단 ECU가 해석하고,
PDW와 자동출차 로직을 거쳐,
Motor ECU에 최종 주행/조향 명령을 보내는 구조
```
