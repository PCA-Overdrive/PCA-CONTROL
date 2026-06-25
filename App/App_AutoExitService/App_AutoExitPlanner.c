#include "App_AutoExitService_Internal.h"

#include "App_PdwService.h"

/*
 * 출차 방향 쪽의 측면 거리 정보를 정리한 구조체
 *
 * 예를 들어 우측 출차라면:
 *  - frontMm : 오른쪽 앞 초음파 거리
 *  - rearMm  : 오른쪽 뒤 초음파 거리
 *
 * 이 정보를 이용해서
 *  1. 옆 차량이 가까운지
 *  2. 앞쪽이 더 가까운지
 *  3. 뒤쪽이 더 가까운지
 *  4. 회피가 필요한지
 * 판단한다.
 */
typedef struct
{
    /* 측면 앞쪽 거리 */
    uint16 frontMm;

    /* 측면 뒤쪽 거리 */
    uint16 rearMm;

    /* 앞/뒤 중 더 가까운 거리 */
    uint16 minMm;

    /* 앞/뒤 모두 안전 거리보다 멀면 TRUE */
    boolean isSafe;

    /* 앞쪽이 뒤쪽보다 확실히 가까운 경우 TRUE */
    boolean isFrontCloser;

    /* 뒤쪽이 앞쪽보다 확실히 가까운 경우 TRUE */
    boolean isRearCloser;
} AppAutoExitSideInfo;

/*
 * 출차 판단에 필요한 양쪽 측면 정보를 묶은 구조체
 *
 * exitSide:
 *  - 실제로 나가려는 방향의 측면 정보
 *
 * oppositeSide:
 *  - 회피할 때 반대로 붙게 되는 쪽의 측면 정보
 *
 * exitFrontCornerMm:
 *  - 나가려는 방향의 전방 코너 거리
 *  - 좌측 출차면 FRONT_LEFT
 *  - 우측 출차면 FRONT_RIGHT
 */
typedef struct
{
    AppAutoExitSideInfo exitSide;
    AppAutoExitSideInfo oppositeSide;
    uint16 exitFrontCornerMm;
} AppAutoExitSideContext;

/*
 * 여러 방향 중 하나라도 targetLevel 이상인지 확인
 *
 * 예:
 *  - 전진 중 FRONT / FRONT_LEFT / FRONT_RIGHT 중 하나라도 DANGER인지 확인
 *  - 후진 중 BEHIND / BEHIND_LEFT / BEHIND_RIGHT 중 하나라도 DANGER인지 확인
 *
 * AppPdwLevel enum 값이
 * NO_OBSTACLE < SAFE < CAUTION < NEAR < DANGER
 * 순서라고 가정하고 >= 비교를 사용한다.
 */
static boolean AppAutoExitPlanner_IsAnyLevelAtLeast(
    const AppPdwState *pdw,
    const AppPdwDirection *directions,
    uint32 directionCount,
    AppPdwLevel targetLevel)
{
    uint32 index;

    if(pdw == 0)
    {
        return FALSE;
    }

    for(index = 0u; index < directionCount; index++)
    {
        if(pdw->level[directions[index]] >= targetLevel)
        {
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * 측면 앞/뒤 거리값을 이용해 AppAutoExitSideInfo 생성
 *
 * 여기서 판단하는 것:
 *  1. 앞/뒤 중 최소 거리
 *  2. 양쪽 모두 충분히 멀어서 안전한지
 *  3. 옆 차량이 기울어져 보이는지
 *     - 앞쪽이 더 가까우면 isFrontCloser
 *     - 뒤쪽이 더 가까우면 isRearCloser
 *
 * 기울기 판단은 단순히 앞/뒤 거리 차이가
 * APP_AUTO_EXIT_SIDE_TILT_DIFF_MM 이상 벌어지는지로 판단한다.
 */
static AppAutoExitSideInfo AppAutoExitPlanner_MakeSideInfo(uint16 frontMm,
                                                           uint16 rearMm)
{
    AppAutoExitSideInfo info;
    sint32 diffMm;

    info.frontMm = frontMm;
    info.rearMm = rearMm;

    /* 앞/뒤 중 더 가까운 값을 minMm으로 저장 */
    info.minMm = (frontMm < rearMm) ? frontMm : rearMm;

    /*
     * 앞/뒤 모두 SIDE_SAFE_MM보다 멀면 측면이 안전하다고 판단
     */
    info.isSafe = ((frontMm > APP_AUTO_EXIT_SIDE_SAFE_MM) &&
                   (rearMm > APP_AUTO_EXIT_SIDE_SAFE_MM)) ? TRUE : FALSE;

    /*
     * 앞쪽 거리 - 뒤쪽 거리
     *
     * diffMm < 0:
     *  - frontMm이 rearMm보다 작음
     *  - 즉 앞쪽이 더 가까움
     *
     * diffMm > 0:
     *  - rearMm이 frontMm보다 작음
     *  - 즉 뒤쪽이 더 가까움
     */
    diffMm = (sint32)frontMm - (sint32)rearMm;

    if(diffMm < -(sint32)APP_AUTO_EXIT_SIDE_TILT_DIFF_MM)
    {
        /*
         * 앞쪽이 확실히 더 가까운 경우
         * 출차 방향 앞쪽에 여유가 부족하므로 긴 회피가 필요할 수 있음
         */
        info.isFrontCloser = TRUE;
        info.isRearCloser = FALSE;
    }
    else if(diffMm > (sint32)APP_AUTO_EXIT_SIDE_TILT_DIFF_MM)
    {
        /*
         * 뒤쪽이 확실히 더 가까운 경우
         * 후방 쪽이 좁은 상황으로 보고 짧은 회피가 필요할 수 있음
         */
        info.isFrontCloser = FALSE;
        info.isRearCloser = TRUE;
    }
    else
    {
        /*
         * 앞/뒤 거리 차이가 크지 않으면
         * 옆 차량이 크게 기울어진 상황은 아니라고 판단
         */
        info.isFrontCloser = FALSE;
        info.isRearCloser = FALSE;
    }

    return info;
}

/*
 * 출차 방향 측면 정보를 보고 회피 정도를 결정
 *
 * 반환값:
 *  - APP_AUTO_EXIT_AVOID_NONE  : 회피 필요 없음
 *  - APP_AUTO_EXIT_AVOID_SHORT : 짧은 회피 필요
 *  - APP_AUTO_EXIT_AVOID_LONG  : 긴 회피 필요
 *
 * 판단 우선순위:
 *  1. 측면 최소 거리가 너무 가까우면 LONG
 *  2. 앞쪽이 caution 거리보다 가까우면 LONG
 *  3. 뒤쪽이 caution 거리보다 가까우면 SHORT
 *  4. 앞쪽이 더 가까운 형태로 기울어져 있고 안전거리보다 가까우면 LONG
 *  5. 뒤쪽이 더 가까운 형태로 기울어져 있고 안전거리보다 가까우면 SHORT
 */
static AppAutoExitAvoidLevel AppAutoExitPlanner_GetAvoidLevel(const AppAutoExitSideInfo *exitSide)
{
    /*
     * 출차 방향 측면 자체가 너무 가까우면
     * 짧게 피해서는 부족하므로 긴 회피
     */
    if(exitSide->minMm < APP_AUTO_EXIT_SIDE_BLOCKED_MM)
    {
        return APP_AUTO_EXIT_AVOID_LONG;
    }

    /*
     * 앞쪽이 가까우면 출차하며 회전할 때 앞 코너가 걸릴 수 있으므로 긴 회피
     */
    if(exitSide->frontMm < APP_AUTO_EXIT_SIDE_FRONT_CAUTION_MM)
    {
        return APP_AUTO_EXIT_AVOID_LONG;
    }

    /*
     * 뒤쪽이 가까우면 초기 출차 시 뒤쪽 간섭 가능성이 있으므로 짧은 회피
     */
    if(exitSide->rearMm < APP_AUTO_EXIT_SIDE_REAR_CAUTION_MM)
    {
        return APP_AUTO_EXIT_AVOID_SHORT;
    }

    /*
     * 옆차의 앞쪽이 더 가까운 기울어진 상태라면
     * 앞으로 나갈 때 앞쪽 간섭 가능성이 커서 긴 회피
     */
    if((exitSide->isFrontCloser == TRUE) &&
       (exitSide->frontMm < APP_AUTO_EXIT_SIDE_SAFE_MM))
    {
        return APP_AUTO_EXIT_AVOID_LONG;
    }

    /*
     * 옆차의 뒤쪽이 더 가까운 상태라면
     * 후방 쪽 여유가 부족한 것으로 보고 짧은 회피
     */
    if((exitSide->isRearCloser == TRUE) &&
       (exitSide->rearMm < APP_AUTO_EXIT_SIDE_SAFE_MM))
    {
        return APP_AUTO_EXIT_AVOID_SHORT;
    }

    return APP_AUTO_EXIT_AVOID_NONE;
}

/*
 * 출차 방향 기준으로 측면 context 생성
 *
 * 좌측 출차:
 *  - exitSide     = LEFT_FRONT / LEFT_BEHIND
 *  - oppositeSide = RIGHT_FRONT / RIGHT_BEHIND
 *  - exitFrontCornerMm = FRONT_LEFT
 *
 * 우측 출차:
 *  - exitSide     = RIGHT_FRONT / RIGHT_BEHIND
 *  - oppositeSide = LEFT_FRONT / LEFT_BEHIND
 *  - exitFrontCornerMm = FRONT_RIGHT
 *
 * 여기서는 PDW level이 아니라 distanceMm을 사용한다.
 * 이유:
 *  - 회피 판단은 단순 위험 레벨보다 실제 거리 차이가 중요함
 *  - 옆차 기울기 판단도 앞/뒤 거리 차이로 해야 함
 */
static AppAutoExitSideContext AppAutoExitPlanner_MakeSideContext(
    const AppPdwState *pdw,
    AppAutoExitDirection exitDirection)
{
    AppAutoExitSideContext context;

    if(exitDirection == APP_AUTO_EXIT_DIR_LEFT)
    {
        context.exitSide = AppAutoExitPlanner_MakeSideInfo(
            pdw->distanceMm[APP_PDW_DIR_LEFT_FRONT],
            pdw->distanceMm[APP_PDW_DIR_LEFT_BEHIND]);

        context.oppositeSide = AppAutoExitPlanner_MakeSideInfo(
            pdw->distanceMm[APP_PDW_DIR_RIGHT_FRONT],
            pdw->distanceMm[APP_PDW_DIR_RIGHT_BEHIND]);

        context.exitFrontCornerMm = pdw->distanceMm[APP_PDW_DIR_FRONT_LEFT];
    }
    else
    {
        context.exitSide = AppAutoExitPlanner_MakeSideInfo(
            pdw->distanceMm[APP_PDW_DIR_RIGHT_FRONT],
            pdw->distanceMm[APP_PDW_DIR_RIGHT_BEHIND]);

        context.oppositeSide = AppAutoExitPlanner_MakeSideInfo(
            pdw->distanceMm[APP_PDW_DIR_LEFT_FRONT],
            pdw->distanceMm[APP_PDW_DIR_LEFT_BEHIND]);

        context.exitFrontCornerMm = pdw->distanceMm[APP_PDW_DIR_FRONT_RIGHT];
    }

    return context;
}

/*
 * 회피 level에 따라 실제 회피 시간 plan 설정
 *
 * avoidPlan에는
 *  - escapeMs  : 반대 방향으로 빠져나가는 시간
 *  - realignMs : 다시 원래 출차 방향으로 정렬하는 시간
 * 이 들어간다.
 */
static void AppAutoExitPlanner_SetAvoidPlan(AppAutoExitAvoidPlan *avoidPlan,
                                            AppAutoExitAvoidLevel level)
{
    if(avoidPlan == 0)
    {
        return;
    }

    if(level == APP_AUTO_EXIT_AVOID_LONG)
    {
        /*
         * 긴 회피:
         * 옆차가 많이 가깝거나 앞쪽 간섭 위험이 큰 경우
         */
        avoidPlan->escapeMs = APP_AUTO_EXIT_AVOID_ESCAPE_LONG_MS;
        avoidPlan->realignMs = APP_AUTO_EXIT_AVOID_REALIGN_LONG_MS;
    }
    else if(level == APP_AUTO_EXIT_AVOID_SHORT)
    {
        /*
         * 짧은 회피:
         * 약간의 보정만 필요한 경우
         */
        avoidPlan->escapeMs = APP_AUTO_EXIT_AVOID_ESCAPE_SHORT_MS;
        avoidPlan->realignMs = APP_AUTO_EXIT_AVOID_REALIGN_SHORT_MS;
    }
    else
    {
        /*
         * 회피 없음
         */
        avoidPlan->escapeMs = 0u;
        avoidPlan->realignMs = 0u;
    }
}

/*
 * 회피 중 반대편이 위험한지 판단
 *
 * 예:
 *  - 좌측으로 출차하려고 오른쪽으로 회피 중이면,
 *    오른쪽 측면과 오른쪽 전방 코너가 위험한지 확인
 *
 * 여기서는 정지 기준을 APP_PDW_LEVEL_DANGER로 본다.
 */
static boolean AppAutoExitPlanner_IsAvoidSideDanger(const AppPdwState *pdw,
                                                    AppPdwDirection sideFrontDirection,
                                                    AppPdwDirection sideRearDirection,
                                                    AppPdwDirection frontCornerDirection)
{
    if(pdw == 0)
    {
        return FALSE;
    }

    if(pdw->level[sideFrontDirection] >= APP_PDW_LEVEL_DANGER)
    {
        return TRUE;
    }

    if(pdw->level[sideRearDirection] >= APP_PDW_LEVEL_DANGER)
    {
        return TRUE;
    }

    if(pdw->level[frontCornerDirection] >= APP_PDW_LEVEL_DANGER)
    {
        return TRUE;
    }

    return FALSE;
}

/*
 * 현재 motion step을 수행해도 안전한지 확인
 *
 * 이 함수는 자동출차 주행 중 계속 호출되어,
 * 현재 진행 방향 기준으로 DANGER가 있는지 확인한다.
 *
 * 전진 중:
 *  - FRONT
 *  - FRONT_LEFT
 *  - FRONT_RIGHT
 *
 * 후진 중:
 *  - BEHIND
 *  - BEHIND_LEFT
 *  - BEHIND_RIGHT
 *
 * 하나라도 DANGER이면 TRUE를 반환한다.
 * 즉, TRUE는 "위험하다 / 멈춰야 한다"는 의미다.
 */
boolean AppAutoExitPlanner_IsStepSafetyDanger(const AppAutoExitMotionStep *step)
{
    AppPdwState pdw;

    static const AppPdwDirection frontDirections[] =
    {
        APP_PDW_DIR_FRONT,
        APP_PDW_DIR_FRONT_LEFT,
        APP_PDW_DIR_FRONT_RIGHT
    };

    static const AppPdwDirection rearDirections[] =
    {
        APP_PDW_DIR_BEHIND,
        APP_PDW_DIR_BEHIND_LEFT,
        APP_PDW_DIR_BEHIND_RIGHT
    };

    if(step == 0)
    {
        return FALSE;
    }

    /*
     * PDW Service에서 계산된 최신 level/distance 상태를 가져온다.
     */
    if(AppPdwService_GetState(&pdw) != pdPASS)
    {
        return FALSE;
    }

    /*
     * PDW가 비활성 상태이면 안전 판단을 하지 않는다.
     *
     * 단, 현재 App_PdwService 쪽에서 자동출차 중에는
     * pdw.enabled가 TRUE가 되도록 만들어야 이 함수가 의미 있게 동작한다.
     */
    if(pdw.enabled == FALSE)
    {
        return FALSE;
    }

    /*
     * driveCmd가 STOP 기준값보다 작으면 전진으로 판단
     */
    if(step->driveCmd < APP_AUTO_EXIT_DRIVE_STOP)
    {
        return AppAutoExitPlanner_IsAnyLevelAtLeast(&pdw,
                                                    frontDirections,
                                                    APP_AUTO_EXIT_ARRAY_COUNT(frontDirections),
                                                    APP_PDW_LEVEL_DANGER);
    }

    /*
     * driveCmd가 STOP 기준값보다 크면 후진으로 판단
     */
    if(step->driveCmd > APP_AUTO_EXIT_DRIVE_STOP)
    {
        return AppAutoExitPlanner_IsAnyLevelAtLeast(&pdw,
                                                    rearDirections,
                                                    APP_AUTO_EXIT_ARRAY_COUNT(rearDirections),
                                                    APP_PDW_LEVEL_DANGER);
    }

    /*
     * driveCmd가 STOP이면 이동 중이 아니므로 위험 step으로 보지 않는다.
     */
    return FALSE;
}

/*
 * 자동출차 시작 전에 어떤 전략을 사용할지 선택
 *
 * 반환 전략:
 *  - APP_AUTO_EXIT_STRATEGY_NORMAL
 *      그냥 기본 출차 시퀀스 수행
 *
 *  - APP_AUTO_EXIT_STRATEGY_AVOID_AND_RESUME
 *      먼저 반대 방향으로 살짝 회피한 뒤 기본 출차 시퀀스 수행
 *
 *  - APP_AUTO_EXIT_STRATEGY_BLOCKED
 *      전방 또는 출차 방향이 너무 가까워 출차 불가
 *
 * 판단 흐름:
 *  1. 직진 출차면 회피 없이 NORMAL
 *  2. PDW 상태 읽기 실패하면 NORMAL
 *  3. PDW 비활성이면 NORMAL
 *  4. 전방 또는 출차 방향 전방 코너가 DANGER면 BLOCKED
 *  5. 출차 방향 측면 거리를 보고 회피 필요성 판단
 *  6. 반대편이 안전하면 AVOID_AND_RESUME
 *  7. 반대편도 안전하지 않으면 BLOCKED
 */
AppAutoExitStrategy AppAutoExitPlanner_SelectStrategy(AppAutoExitDirection exitDirection,
                                                      AppAutoExitAvoidPlan *avoidPlan)
{
    AppPdwState pdw;
    AppAutoExitSideContext sideContext;
    AppAutoExitAvoidLevel avoidLevel;

    /*
     * 기본 avoidPlan은 회피 없음으로 초기화
     */
    AppAutoExitPlanner_SetAvoidPlan(avoidPlan, APP_AUTO_EXIT_AVOID_NONE);

    /*
     * 직진 출차는 좌/우 회피 판단 대상이 아니므로 NORMAL
     */
    if(exitDirection == APP_AUTO_EXIT_DIR_STRAIGHT)
    {
        return APP_AUTO_EXIT_STRATEGY_NORMAL;
    }

    /*
     * PDW 상태를 읽지 못하면 보수적으로 막는 대신,
     * 현재 로직에서는 NORMAL 출차로 진행한다.
     */
    if(AppPdwService_GetState(&pdw) != pdPASS)
    {
        return APP_AUTO_EXIT_STRATEGY_NORMAL;
    }

    /*
     * PDW가 꺼져 있으면 출차 회피 판단을 하지 않고 NORMAL 진행
     */
    if(pdw.enabled == FALSE)
    {
        return APP_AUTO_EXIT_STRATEGY_NORMAL;
    }

    /*
     * 출차 방향 기준으로
     * exitSide / oppositeSide / exitFrontCornerMm 구성
     */
    sideContext = AppAutoExitPlanner_MakeSideContext(&pdw, exitDirection);

    /*
     * 전방 중앙이 DANGER면 출차 시작 자체가 위험하므로 BLOCKED
     */
    if(pdw.level[APP_PDW_DIR_FRONT] >= APP_PDW_LEVEL_DANGER)
    {
        return APP_AUTO_EXIT_STRATEGY_BLOCKED;
    }

    /*
     * 좌측 출차라면 FRONT_LEFT가 DANGER인지 확인
     * 나가려는 쪽 앞 코너가 걸릴 수 있기 때문
     */
    if(exitDirection == APP_AUTO_EXIT_DIR_LEFT)
    {
        if(pdw.level[APP_PDW_DIR_FRONT_LEFT] >= APP_PDW_LEVEL_DANGER)
        {
            return APP_AUTO_EXIT_STRATEGY_BLOCKED;
        }
    }
    else
    {
        /*
         * 우측 출차라면 FRONT_RIGHT가 DANGER인지 확인
         */
        if(pdw.level[APP_PDW_DIR_FRONT_RIGHT] >= APP_PDW_LEVEL_DANGER)
        {
            return APP_AUTO_EXIT_STRATEGY_BLOCKED;
        }
    }

    /*
     * 출차 방향 측면 거리/기울기를 보고 회피 필요 정도 계산
     */
    avoidLevel = AppAutoExitPlanner_GetAvoidLevel(&sideContext.exitSide);

    /*
     * 회피가 필요 없고, 출차 방향 측면이 안전하면 NORMAL 출차
     */
    if((avoidLevel == APP_AUTO_EXIT_AVOID_NONE) &&
       (sideContext.exitSide.isSafe == TRUE))
    {
        return APP_AUTO_EXIT_STRATEGY_NORMAL;
    }

    /*
     * 출차 방향이 좁아서 회피가 필요할 때,
     * 반대편이 안전하면 반대편으로 살짝 회피한 뒤 출차 진행
     */
    if(sideContext.oppositeSide.isSafe == TRUE)
    {
        /*
         * avoidLevel이 NONE인데 여기까지 왔다는 것은
         * exitSide가 완전히 안전하다고 보기 애매한 상황이므로
         * 최소한 SHORT 회피를 적용한다.
         */
        if(avoidLevel == APP_AUTO_EXIT_AVOID_NONE)
        {
            avoidLevel = APP_AUTO_EXIT_AVOID_SHORT;
        }

        AppAutoExitPlanner_SetAvoidPlan(avoidPlan, avoidLevel);
        return APP_AUTO_EXIT_STRATEGY_AVOID_AND_RESUME;
    }

    /*
     * 출차 방향도 애매하고, 반대편도 안전하지 않으면 출차 불가
     */
    return APP_AUTO_EXIT_STRATEGY_BLOCKED;
}

/*
 * 회피할 때 사용할 조향값 반환
 *
 * 좌측 출차:
 *  - 바로 좌측으로 나가려는데 왼쪽이 좁으면
 *    먼저 오른쪽으로 피해야 하므로 STEER_RIGHT
 *
 * 우측 출차:
 *  - 바로 우측으로 나가려는데 오른쪽이 좁으면
 *    먼저 왼쪽으로 피해야 하므로 STEER_LEFT
 */
uint8 AppAutoExitPlanner_GetEscapeSteer(AppAutoExitDirection exitDirection)
{
    if(exitDirection == APP_AUTO_EXIT_DIR_LEFT)
    {
        return APP_AUTO_EXIT_STEER_RIGHT;
    }

    return APP_AUTO_EXIT_STEER_LEFT;
}

/*
 * 회피 후 다시 원래 출차 방향으로 복귀할 때 사용할 조향값 반환
 *
 * 좌측 출차:
 *  - 오른쪽으로 회피한 뒤 다시 왼쪽으로 정렬
 *
 * 우측 출차:
 *  - 왼쪽으로 회피한 뒤 다시 오른쪽으로 정렬
 */
uint8 AppAutoExitPlanner_GetRealignSteer(AppAutoExitDirection exitDirection)
{
    if(exitDirection == APP_AUTO_EXIT_DIR_LEFT)
    {
        return APP_AUTO_EXIT_REALIGN_LEFT_STEER;
    }

    return APP_AUTO_EXIT_REALIGN_RIGHT_STEER;
}

/*
 * 회피 동작 중 반대편이 위험해졌는지 확인
 *
 * 예:
 *  - 좌측 출차를 위해 오른쪽으로 회피하는 중
 *    오른쪽 측면/오른쪽 앞 코너가 DANGER인지 확인
 *
 *  - 우측 출차를 위해 왼쪽으로 회피하는 중
 *    왼쪽 측면/왼쪽 앞 코너가 DANGER인지 확인
 *
 * TRUE이면 회피 중 위험하므로 정지 또는 BLOCKED 처리 대상이 된다.
 */
boolean AppAutoExitPlanner_IsOppositeSideDangerDuringAvoid(AppAutoExitDirection exitDirection)
{
    AppPdwState pdw;

    if(AppPdwService_GetState(&pdw) != pdPASS)
    {
        return FALSE;
    }

    if(pdw.enabled == FALSE)
    {
        return FALSE;
    }

    if(exitDirection == APP_AUTO_EXIT_DIR_LEFT)
    {
        /*
         * 좌측 출차의 회피 방향은 오른쪽이므로
         * 오른쪽 측면과 오른쪽 전방 코너를 확인
         */
        return AppAutoExitPlanner_IsAvoidSideDanger(&pdw,
                                                    APP_PDW_DIR_RIGHT_FRONT,
                                                    APP_PDW_DIR_RIGHT_BEHIND,
                                                    APP_PDW_DIR_FRONT_RIGHT);
    }

    /*
     * 우측 출차의 회피 방향은 왼쪽이므로
     * 왼쪽 측면과 왼쪽 전방 코너를 확인
     */
    return AppAutoExitPlanner_IsAvoidSideDanger(&pdw,
                                                APP_PDW_DIR_LEFT_FRONT,
                                                APP_PDW_DIR_LEFT_BEHIND,
                                                APP_PDW_DIR_FRONT_LEFT);
}

/*
 * 회피 동작을 추가했을 때,
 * 기존 첫 번째 전진 시간을 얼마나 줄일지 계산
 *
 * 이유:
 *  - 회피 escape 동작과 realign 동작에서도 차량이 어느 정도 전진/이동할 수 있음
 *  - 그 상태에서 기존 FORWARD_1 시간을 그대로 쓰면 너무 많이 나갈 수 있음
 *  - 그래서 회피 시간의 일정 비율만큼 첫 전진 시간을 줄인다.
 */
uint32 AppAutoExitPlanner_CalcFirstStepReductionMs(uint32 escapeMs,
                                                   uint32 realignMs)
{
    uint32 reductionMs;

    /*
     * escapeMs와 realignMs 중 일부 비율을 전진 시간 감소량으로 환산
     */
    reductionMs =
        ((escapeMs * APP_AUTO_EXIT_AVOID_ESCAPE_FORWARD_RATIO_PERCENT) / 100u) +
        ((realignMs * APP_AUTO_EXIT_AVOID_REALIGN_FORWARD_RATIO_PERCENT) / 100u);

    /*
     * 감소량이 첫 전진 시간보다 크거나 같으면
     * 최소 step 시간은 남기도록 제한한다.
     */
    if(reductionMs >= APP_AUTO_EXIT_FORWARD_1_MS)
    {
        return APP_AUTO_EXIT_FORWARD_1_MS - APP_AUTO_EXIT_MIN_STEP_MS;
    }

    return reductionMs;
}
