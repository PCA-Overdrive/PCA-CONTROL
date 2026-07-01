# PCA-Control-ECU

## 1. 프로젝트 개요

`PCA-Control-ECU`는 PCA-Overdrive 프로젝트의 **Control ECU** 펌웨어입니다.

Control ECU는 Raspberry Pi, Sensor ECU, Motor ECU 사이에서 차량 상태를 해석하고,  
**PDW(Parking Distance Warning)** 및 **자동출차(Auto Exit)** 판단을 수행한 뒤  
최종 주행/조향 제어 명령을 생성합니다.

본 프로젝트에서 Control ECU는 다음 역할을 수행합니다.

- Sensor ECU 수신 데이터(초음파/IMU yaw/속도) 해석
- Raspberry Pi 수신 데이터(수동 명령/기어/활성화 상태/lineAngle/자동출차 명령) 해석
- 초음파 기반 PDW 위험도 판단
- 자동출차 방향/전략 판단 및 제어 명령 생성
- IMU yaw + lineAngle 기반 출차 정렬 판단
- Motor ECU로 최종 제어 명령 송신
- Raspberry Pi/HMI로 PDW/자동출차 상태 송신

---

## 2. 전체 시스템 구조

```text
Raspberry Pi / HMI
  ├─ 수동 주행 명령
  ├─ 기어 상태
  ├─ PDW/PCA 활성화 상태
  ├─ 자동출차 시작/정지 명령
  └─ 주차선/차량 기준 각도(lineAngle)
        │
        │ CAN (0x201, 0x300)
        ▼
Control ECU (PCA-Control-ECU)
  ├─ 수신 데이터 최신 상태 관리
  ├─ PDW 판단
  ├─ 자동출차 전략/제어 판단
  ├─ yaw 정렬 판단
  └─ 최종 제어 명령 생성
        │
        │ CAN (0x100)
        ▼
Motor ECU
```

```text
Sensor ECU
  ├─ 초음파 10방향 거리
  ├─ IMU yaw
  └─ 차량 속도
        │
        │ CAN FD (0x200)
        ▼
Control ECU
```

```text
Control ECU
  ├─ 0x400 PDW 상태
  └─ 0x401 자동출차 상태
        │
        ▼
Raspberry Pi / HMI
```

Control ECU는 센서/사용자 입력과 차량 제어 ECU 사이에서 **판단 + 제어 게이트웨이** 역할을 수행합니다.

---

## 3. 주요 기능

Control ECU의 핵심 기능은 다음과 같습니다.

- 초음파 거리 기반 PDW 위험도 계산
- 자동출차 시작 방향 처리 (`START_STRAIGHT`, `START_LEFT`, `START_RIGHT`, `STOP`)
- 출차 시작 시 주변 공간 분석 및 전략 선택
  - `NORMAL`
  - `AVOID_AND_RESUME`
  - `BLOCKED`
- 자동출차 profile step 기반 주행/조향 제어
- IMU yaw + lineAngle 기반 목표 yaw 정렬 판단
- 상태 메시지(`0x400`, `0x401`) 송신
- 최종 제어 메시지(`0x100`) 송신

---

## 4. RTOS 기반 소프트웨어 구조

PCA-Control-ECU는 FreeRTOS 기반으로 동작합니다.

CAN 수신 처리, PDW 판단, 자동출차 FSM, 상태 송신, 최종 제어 송신을  
단일 루프가 아닌 Task 단위로 분리하여 주기적으로 실행합니다.

| Task | 주기 | 역할 |
|---|---:|---|
| AppCan_RxTask | 1 ms | CAN 수신 프레임을 최신 mailbox에 반영 |
| AppPdwService_Task | 10 ms | 초음파 거리 기반 PDW level 판단 |
| AppAutoExitService_Task | 10 ms | 자동출차 명령 처리 및 FSM 진행 |
| AppStatusTxService_Task | 10 ms | `0x400` PDW 상태 송신 |
| AppDriveService_Task | 12 ms | `0x100` 최종 차량 제어 명령 송신 |

RTOS 분리 구조를 통해 각 기능의 책임을 명확히 하고, 디버깅 및 확장 시 영향 범위를 줄였습니다.

---

## 5. 내부 모듈 구조

| Module | Role |
|---|---|
| `App_Can` | CAN 수신 최신값 관리, 송신 wrapper |
| `App_RxService` | CAN raw data를 App 내부 구조체로 변환 |
| `App_PdwService` | 초음파 거리 기반 PDW 위험도 판단 |
| `App_StatusTxService` | PDW/상태 메시지 송신 (`0x400`) |
| `App_DriveService` | 최종 차량 제어 명령 선택 및 송신 (`0x100`) |
| `App_AutoExitService` | 자동출차 FSM 및 제어 명령 생성 |
| `App_AutoExitPlanner` | 자동출차 전략 선택 |
| `App_AutoExitProfile` | 방향별 motion profile 정의 |
| `App_AutoExitMonitor` | 자동출차 상태 송신 및 yaw 정렬 판단 |

---

## 6. CAN / CAN FD Interface

### 6.1 Rx Messages

| CAN ID | Sender | Frame | Description |
|---|---|---|---|
| `0x200` | Sensor ECU | CAN FD | 초음파 거리(10ch), IMU yaw, 차량 속도 |
| `0x201` | Raspberry Pi | Classical CAN | 수동 주행/조향 명령, 기어, PDW/PCA 활성화, lineAngle |
| `0x300` | Raspberry Pi | Classical CAN | 자동출차 시작/정지 명령 |

#### `0x200` (23 bytes, CAN FD)
- B0~B19: 초음파 10방향 거리값 (uint16 × 10)
- B20~B21: imuYaw (int16)
- B22: vehicleSpeed (uint8)

#### `0x201` (6 bytes, Classical CAN)
- B0: driveCmd
- B1: steeringCmd
- B2: gearStatus
- B3: pcaActivated
- B4~B5: lineAngle (int16)

#### `0x300` (1 byte, Classical CAN)
- `0x00`: NORMAL
- `0x01`: START_STRAIGHT
- `0x02`: START_LEFT
- `0x03`: START_RIGHT
- `0x04`: STOP

---

### 6.2 Tx Messages

| CAN ID | Receiver | Frame | Description |
|---|---|---|---|
| `0x100` | Motor ECU | Classical CAN | 최종 drive/steering 제어 명령 |
| `0x400` | Raspberry Pi/HMI | CAN FD | 10방향 PDW level + 상태 정보 |
| `0x401` | Raspberry Pi/HMI | Classical CAN | 자동출차 진행 상태 |

#### `0x100` (2 bytes)
- B0: driveCmd
- B1: steeringCmd

```text
driveCmd
  0~126: forward
  127: stop
  128~255: reverse

steeringCmd
  0~126: left
  127: center
  128~255: right
```

#### `0x400` (14 bytes, CAN FD)
- B0~B9: 10방향 PDW level
- B10: pcaActivated
- B11: vehicleSpeed
- B12: gearStatus
- B13: emergencyStop

PDW level:
- `0x00` NO_OBSTACLE
- `0x01` SAFE
- `0x02` CAUTION
- `0x03` NEAR
- `0x04` DANGER

#### `0x401` (1 byte)
- `0x00`: IDLE / NORMAL
- `0x01`: RUNNING
- `0x02`: COMPLETE
- `0x03`: STOPPED / BLOCKED

---

## 7. CAN Rx Mailbox 정책

수신 메시지는 CAN ID별 **latest mailbox**(길이 1 queue)로 관리합니다.

```text
0x200 -> g_ultrasonicMailbox
0x201 -> g_vehicleStatusMailbox
0x300 -> g_autoParkingMailbox
```

- write: `xQueueOverwrite(...)`
- read: `xQueuePeek(...)`

이 방식은 센서/상태 데이터처럼 “가장 최신 값”이 중요한 제어 시스템에 적합합니다.

---

## 8. PDW Service

PDW Service는 10방향 거리값을 위험 level로 변환합니다.

| Distance | Level |
|---:|---|
| 0 mm | NO_OBSTACLE |
| 0 < d <= 200 mm | DANGER |
| 200 < d <= 250 mm | NEAR |
| 250 < d <= 300 mm | CAUTION |
| 300 < d <= 1000 mm | SAFE |
| 1000 < d | NO_OBSTACLE |

PDW 상태 구조:
```text
AppPdwState
  ├─ enabled
  ├─ dangerDetected
  ├─ level[10]
  └─ distanceMm[10]
```

`dangerDetected`는 Drive Service의 안전 우선 제어에 반영됩니다.

---

## 9. Drive Service (최종 제어 선택)

Drive Service는 여러 제어 후보 중 최종 `0x100` 송신값을 선택합니다.

우선순위는 다음과 같습니다.

```text
1) Raspberry Pi timeout      -> stop / center
2) AutoExitService active    -> AutoExit 제어 명령
3) PDW dangerDetected        -> stop / center
4) Normal driving            -> Raspberry Pi 수동 명령
```

이 정책으로 통신 이상/충돌 위험 시 정지를 우선 보장합니다.

---

## 10. Auto Exit Service

Auto Exit Service는 Raspberry Pi에서 전달한 자동출차 명령(`0x300`)을 기반으로 출차 동작을 수행합니다.

자동출차 처리 흐름:

```text
0x300 자동출차 명령 수신
  ↓
출차 방향 결정 (STRAIGHT / LEFT / RIGHT / STOP)
  ↓
초기 초음파 상태 확인
  ↓
전략 선택 (NORMAL / AVOID_AND_RESUME / BLOCKED)
  ↓
회피 또는 기본 profile 수행
  ↓
IMU yaw + lineAngle 기반 정렬 판단
  ↓
완료/중지 상태 0x401 송신
```

### 10.1 Direction별 Motion Profile

Auto Exit은 방향별 step profile(drive, steering, duration) 배열로 동작합니다.

- `START_STRAIGHT`
  1) forward + center  
  2) stop + center

- `START_RIGHT`
  1) forward + center  
  2) forward + right  
  3) stop + center  
  4) reverse + center  
  5) stop + center  
  6) forward + right  
  7) stop + center  
  8) reverse + left  
  9) stop + center  
  10) forward + right  
  11) forward + center  
  12) stop + center

- `START_LEFT`
  1) forward + center  
  2) forward + left  
  3) stop + center  
  4) reverse + center  
  5) stop + center  
  6) forward + left  
  7) stop + center  
  8) reverse + right  
  9) stop + center  
  10) forward + left  
  11) forward + center  
  12) stop + center

### 10.2 Planner 전략 선택 로직

출차 시작 시, 출차 방향 측면의 초음파 상태를 확인하여 전략을 선택합니다.

- 우측 출차 시 확인 영역:
  - `RIGHT_FRONT`
  - `RIGHT_BEHIND`
  - `FRONT_RIGHT`

- 좌측 출차 시 확인 영역:
  - `LEFT_FRONT`
  - `LEFT_BEHIND`
  - `FRONT_LEFT`

측면 상태는 다음과 같이 분류됩니다.

- `SAFE`
- `CRITICAL`
- `NARROW_BOTH`
- `NEAR_FRONT`
- `NEAR_REAR`
- `TILTED_FRONT`
- `TILTED_REAR`

전략 선택:

- `NORMAL`
  - 기본 출차 profile 수행
- `AVOID_AND_RESUME`
  - 출차 반대 방향으로 먼저 회피하여 공간 확보 후 원래 방향 출차 수행
- `BLOCKED`
  - 출차 곤란으로 판단, 정지 유지

### 10.3 Yaw + lineAngle 기반 완료 판단

Auto Exit Monitor는 다음 상태를 관리합니다.

- 자동출차 시작 시점 yaw(`startYaw`) 저장
- Raspberry Pi의 `lineAngle` 저장
- 방향별 목표 회전각(`targetTurnAngle`) 적용
- 현재 yaw와 목표 yaw 비교
- `0x401` 상태 송신

목표 yaw 계산:

```text
targetYaw = startYaw + targetTurnAngle + lineAngle
```

즉, Auto Exit은 단순 시간 종료가 아니라 **각도 정렬 조건**까지 포함해 완료를 판단합니다.

---

## 11. 실행 흐름 요약

```text
1) Sensor ECU가 0x200 송신 (초음파/IMU/속도)
2) Raspberry Pi가 0x201 송신 (수동명령/상태/lineAngle)
3) 필요 시 0x300 자동출차 명령 송신
4) App_Can이 latest mailbox 갱신
5) RxService가 App 구조체로 변환
6) PDW Service가 위험 level 계산
7) AutoExitService가 전략/제어 명령 생성
8) DriveService가 최종 0x100 선택
9) Motor ECU가 차량 구동/조향 수행
10) StatusTx/Monitor가 0x400/0x401 송신
```

---

## 12. 빌드 및 실행

- Target: **Infineon AURIX TC375**
- OS: **FreeRTOS**
- 진입 흐름:
```text
Cpu0_Main.c
  ↓
App_Init()
  ↓
Task 생성
  ↓
vTaskStartScheduler()
```

노드 설정:
```c
#define NODE_JUDGMENT_ECU
```

권장 CAN 설정:
```text
Nominal bitrate : 500 kbit/s
Data bitrate    : 2 Mbit/s
Mode            : CAN FD
```

---

## 13. 프로젝트 구조

```text
PCA-Control-ECU/
├─ App/
│  ├─ App.c, App.h, App_Types.h
│  ├─ App_Can/
│  ├─ App_RxService/
│  ├─ App_PdwService/
│  ├─ App_StatusTxService/
│  ├─ App_DriveService/
│  └─ App_AutoExitService/
│     ├─ App_AutoExitService.c/h
│     ├─ App_AutoExitPlanner.c
│     ├─ App_AutoExitProfile.c
│     └─ App_AutoExitMonitor.c
├─ Drivers/McmcanFd/
├─ OS/FreeRTOS/
├─ Cpu0_Main.c
├─ Cpu1_Main.c
├─ Cpu2_Main.c
└─ README.md
```

---

## 14. 핵심 특징

- PCA-Overdrive **Control ECU** 전용 펌웨어
- FreeRTOS 기반 주기 Task 구조
- CAN/CAN FD 기반 ECU 간 통신
- 초음파 10채널 기반 PDW 판단
- 자동출차 방향/전략/상태 관리
- IMU yaw + lineAngle 기반 정렬 판단
- 안전 우선 제어 우선순위 적용
- latest mailbox 구조 기반 최신값 중심 판단

---

## 15. 요약

PCA-Control-ECU는 Sensor ECU와 Raspberry Pi에서 수신한 정보를 기반으로  
차량 주변 위험도를 판단하고(PDW), 자동출차 전략을 결정하며, Motor ECU로 최종 제어 명령을 송신하는 **Control ECU**입니다.

특히 자동출차 과정에서 초음파 기반 공간 분석, 회피 전략, IMU yaw 및 lineAngle 보정을 함께 사용하여  
단순 시간 기반이 아닌 **상태 기반 출차 제어**를 수행합니다.

최종적으로 Control ECU는 `0x100` 제어 명령, `0x400` PDW 상태, `0x401` 자동출차 상태를 송신하여  
PCA-Overdrive 시스템의 안전하고 일관된 동작을 지원합니다.
