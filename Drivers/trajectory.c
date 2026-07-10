#include "trajectory.h"
#include "motor_control.h"

/* ==================== 机器人硬件常量 (需按实车标定) ==================== */

#define WHEEL_BASE     0.14f     /* 左右轮距 L (m) */
#define WHEEL_RADIUS   0.048f    /* 轮子半径 r (m) */
#define GEAR_RATIO     20.0f     /* 减速比 1:20, 电机轴 = 输出轴 × 20 */
#define CTRL_DT        0.01f     /* 控制周期 (s), 由 TIMG12 10ms ISR 保证 */

#define PI_F           3.1415926f

/* 电机 ID 与左右轮映射 (按接线调整)
 * 后驱车: 电机1=右后轮, 电机2=左后轮 */
#define MOTOR_L_ID     2
#define MOTOR_R_ID     1

/* 轮转向符号: 正 RPM = 后退, 改为 -1 使正线速度映射为负 RPM = 前进 */
#define MOTOR_L_SIGN   (-1)
#define MOTOR_R_SIGN   (-1)

/* v(m/s) -> 电机轴 RPM 的转换系数 */
#define MPS_TO_MOTOR_RPM  (60.0f / (2.0f * PI_F * WHEEL_RADIUS) * GEAR_RATIO)

/* ==================== 轨迹状态结构体 ==================== */

static struct {
    traj_status_t status;

    /* --- 路径 (分段) --- */
    const traj_segment_t *segs;     /* 段数组 (调用者持有) */
    uint8_t  num_segs;
    uint8_t  seg_index;             /* 当前段 */
    uint8_t  loop;                  /* 1 = 闭合曲线循环 */

    /* --- 当前段前馈 (开环) 轮速 m/s --- */
    float    ff_v_L;
    float    ff_v_R;
    uint32_t remaining_ticks;       /* 当前段剩余 10ms tick 数 */

    /* --- 闭环预留 --- */
    uint8_t          closed_loop;   /* 0 = 纯开环, 1 = 叠加反馈 */
    traj_feedback_fn feedback;      /* 循迹反馈回调, 返回修正 (m/s) */
} g_traj;

/* 单段便捷 API 的内部段存储 */
static traj_segment_t g_single;

static inline float fabs_f(float x) { return (x < 0.0f) ? -x : x; }

/* ==================== 内部: 段解算与下发 ==================== */

/* 解算第 i 段的前馈轮速与持续时间, 装载为当前段 */
static void load_segment(uint8_t i)
{
    const traj_segment_t *s = &g_traj.segs[i];
    float v = s->v;
    float duration;

    if (s->type == SEG_ARC) {
        float w = (float)s->direction * (v / s->R);
        g_traj.ff_v_L = v - (w * WHEEL_BASE / 2.0f);
        g_traj.ff_v_R = v + (w * WHEEL_BASE / 2.0f);
        float aw = fabs_f(w);
        duration = (aw < 1e-6f) ? 0.0f : (s->length / aw);   /* length = theta */
    } else {                                                 /* SEG_STRAIGHT */
        g_traj.ff_v_L = v;
        g_traj.ff_v_R = v;
        duration = (v > 1e-6f) ? (s->length / v) : 0.0f;     /* length = distance */
    }

    uint32_t ticks = (uint32_t)((duration / CTRL_DT) + 0.5f);
    if (ticks == 0) ticks = 1;
    g_traj.remaining_ticks = ticks;
    g_traj.seg_index = i;
}

/* 每 tick 下发轮速: 前馈 + (预留)闭环修正 */
static void apply_speed(void)
{
    float corr = 0.0f;
    if (g_traj.closed_loop && g_traj.feedback)
        corr = g_traj.feedback();

    float v_L = g_traj.ff_v_L - corr;
    float v_R = g_traj.ff_v_R + corr;

    motor_control_set_speed(MOTOR_L_ID,
        (int32_t)(v_L * MPS_TO_MOTOR_RPM) * MOTOR_L_SIGN);
    motor_control_set_speed(MOTOR_R_ID,
        (int32_t)(v_R * MPS_TO_MOTOR_RPM) * MOTOR_R_SIGN);
}

/* ==================== 路径 API ==================== */

int trajectory_run_path(const traj_segment_t *segs, uint8_t num, uint8_t loop)
{
    if (segs == 0 || num == 0) return -1;

    g_traj.segs     = segs;
    g_traj.num_segs = num;
    g_traj.loop     = loop ? 1 : 0;

    load_segment(0);
    g_traj.status = TRAJ_RUNNING;
    return 0;
}

/* ==================== 单段便捷 API ==================== */

int trajectory_arc(float R, float theta, float v_target, int direction)
{
    if (R <= 0.0f || theta <= 0.0f || v_target <= 0.0f ||
        (direction != 1 && direction != -1))
        return -1;

    g_single.type      = SEG_ARC;
    g_single.R         = R;
    g_single.length    = theta;
    g_single.v         = v_target;
    g_single.direction = direction;
    return trajectory_run_path(&g_single, 1, 0);
}

int trajectory_circle(float R, float v_target, int direction)
{
    if (R <= 0.0f || v_target <= 0.0f ||
        (direction != 1 && direction != -1))
        return -1;

    g_single.type      = SEG_ARC;
    g_single.R         = R;
    g_single.length    = 2.0f * PI_F;
    g_single.v         = v_target;
    g_single.direction = direction;
    return trajectory_run_path(&g_single, 1, 1);   /* loop = 持续圆周 */
}

int trajectory_straight(float distance, float v_target)
{
    if (distance <= 0.0f || v_target <= 0.0f) return -1;

    g_single.type      = SEG_STRAIGHT;
    g_single.R         = 0.0f;
    g_single.length    = distance;
    g_single.v         = v_target;
    g_single.direction = 1;
    return trajectory_run_path(&g_single, 1, 0);
}

void trajectory_stop(void)
{
    g_traj.status          = TRAJ_IDLE;
    g_traj.remaining_ticks = 0;
    g_traj.ff_v_L          = 0.0f;
    g_traj.ff_v_R          = 0.0f;
    motor_control_set_speed(MOTOR_L_ID, 0);
    motor_control_set_speed(MOTOR_R_ID, 0);
}

/* ==================== ISR 状态机调度 (10ms) ==================== */

void trajectory_update(void)
{
    if (g_traj.status != TRAJ_RUNNING) return;

    apply_speed();      /* 每 tick 下发 (便于闭环叠加修正) */

    if (g_traj.remaining_ticks > 0)
        g_traj.remaining_ticks--;

    if (g_traj.remaining_ticks == 0) {
        uint8_t next = (uint8_t)(g_traj.seg_index + 1);
        if (next >= g_traj.num_segs) {
            if (g_traj.loop) {
                next = 0;                       /* 闭合曲线: 回到起点 */
            } else {
                motor_control_set_speed(MOTOR_L_ID, 0);
                motor_control_set_speed(MOTOR_R_ID, 0);
                g_traj.status = TRAJ_DONE;
                return;
            }
        }
        load_segment(next);
    }
}

/* ==================== 闭环控制预留接口 ==================== */

void trajectory_set_feedback(traj_feedback_fn fn) { g_traj.feedback = fn; }

void trajectory_enable_closed_loop(uint8_t enable)
{
    g_traj.closed_loop = enable ? 1 : 0;
}

/* ==================== 状态查询 ==================== */

traj_status_t trajectory_get_status(void)        { return g_traj.status; }
uint8_t       trajectory_get_segment_index(void) { return g_traj.seg_index; }

uint32_t trajectory_get_remaining_ms(void)
{
    return g_traj.remaining_ticks * (uint32_t)(CTRL_DT * 1000.0f);
}
