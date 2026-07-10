#include "motor.h"
#include "motor_control.h"
#include "ti_msp_dl_config.h"

/* ==================== 底层驱动 ==================== */

static uint8_t g_dir[2] = {0, 0};

void motor_init(uint8_t motorID)
{
    DL_GPIO_setPins(DC_Motor_STBY_PORT, DC_Motor_STBY_PIN);
    if (motorID == 1) {
        DL_Timer_startCounter(PWMA_INST);
        DL_GPIO_setPins(DC_Motor_AIN1_PORT, DC_Motor_AIN1_PIN);
        DL_GPIO_setPins(DC_Motor_AIN2_PORT, DC_Motor_AIN2_PIN);
        DL_Timer_setCaptureCompareValue(PWMA_INST, 0, GPIO_PWMA_C0_IDX);
    } else if (motorID == 2) {
        DL_GPIO_setPins(DC_Motor_BIN1_PORT, DC_Motor_BIN1_PIN);
        DL_GPIO_setPins(DC_Motor_BIN2_PORT, DC_Motor_BIN2_PIN);
        DL_Timer_setCaptureCompareValue(PWMA_INST, 0, GPIO_PWMA_C1_IDX);
    }
}

void motor_set_direction(uint8_t motorID, uint8_t dir)
{
    if (motorID >= 1 && motorID <= 2)
        g_dir[motorID - 1] = (dir != 0) ? 1 : 0;
}

void motor_set_duty(uint8_t motorID, uint32_t duty)
{
    if (duty > 4000) duty = 4000;

    if (motorID == 1) {
        if (g_dir[0] == 0) {
            DL_GPIO_clearPins(DC_Motor_AIN1_PORT, DC_Motor_AIN1_PIN);
            DL_GPIO_setPins(DC_Motor_AIN2_PORT, DC_Motor_AIN2_PIN);
        } else {
            DL_GPIO_setPins(DC_Motor_AIN1_PORT, DC_Motor_AIN1_PIN);
            DL_GPIO_clearPins(DC_Motor_AIN2_PORT, DC_Motor_AIN2_PIN);
        }
        DL_Timer_setCaptureCompareValue(PWMA_INST, duty, GPIO_PWMA_C0_IDX);
    } else if (motorID == 2) {
        if (g_dir[1] == 0) {
            DL_GPIO_clearPins(DC_Motor_BIN1_PORT, DC_Motor_BIN1_PIN);
            DL_GPIO_setPins(DC_Motor_BIN2_PORT, DC_Motor_BIN2_PIN);
        } else {
            DL_GPIO_setPins(DC_Motor_BIN1_PORT, DC_Motor_BIN1_PIN);
            DL_GPIO_clearPins(DC_Motor_BIN2_PORT, DC_Motor_BIN2_PIN);
        }
        DL_Timer_setCaptureCompareValue(PWMA_INST, duty, GPIO_PWMA_C1_IDX);
    }
}

/* ==================== 编码器测速 ==================== */

#define ENCODER_PPR    500
#define FREQ_TO_RPM(f) ((f) * 60 / ENCODER_PPR)
#define FREQ_SCALE     100     /* 1/0.01s */

static uint32_t measure_freq(int idx)
{
    uint32_t c0;
    if (idx == 0)
        c0 = DL_TimerA_getTimerCount(COMPARE_0_INST);   /* motor1: TIMA1, PA17 */
    else
        c0 = DL_TimerA_getTimerCount(COMPARE_2_INST);   /* motor2: TIMA0, PA21 */

    static uint32_t prev[2] = {0, 0};
    static uint32_t ovf[2]  = {0, 0};
    static uint32_t last[2] = {0, 0};
    static int      first[2]= {1, 1};

    if (c0 < prev[idx]) ovf[idx]++;
    prev[idx] = c0;

    uint32_t total = ovf[idx] * 65535U + c0;
    uint32_t freq = 0;

    if (first[idx]) {
        last[idx] = total;
        first[idx] = 0;
    } else {
        freq = (total - last[idx]) * FREQ_SCALE;
        last[idx] = total;
    }
    return freq;
}

/* ==================== PID ==================== */

#define PWM_MAX          4000
#define PWM_MIN          0
#define PID_INTEGRAL_MAX 5000.0f

typedef struct {
    float Kp, Ki, Kd, dt;
    float integral, prev_error;
    float out_max, out_min;
} PID_t;

static void pid_init(PID_t *p, float kp, float ki, float kd, float dt,
                     float omin, float omax)
{
    p->Kp=kp; p->Ki=ki; p->Kd=kd; p->dt=dt;
    p->integral=0; p->prev_error=0;
    p->out_min=omin; p->out_max=omax;
}

static float pid_update(PID_t *p, float sp, float mv)
{
    float e = sp - mv;
    float d = (e - p->prev_error) / p->dt;
    p->integral += e * p->dt;
    if (p->integral > PID_INTEGRAL_MAX)      p->integral = PID_INTEGRAL_MAX;
    else if (p->integral < -PID_INTEGRAL_MAX) p->integral = -PID_INTEGRAL_MAX;
    p->prev_error = e;
    float o = p->Kp*e + p->Ki*p->integral + p->Kd*d;
    if (o > p->out_max) o = p->out_max;
    if (o < p->out_min) o = p->out_min;
    return o;
}

/* ==================== 双电机控制状态 ==================== */

#define MOTOR_MAX 2

static struct {
    uint8_t  init;
    int32_t  target_rpm;     /* 正=正向, 负=反向 */
    int32_t  actual_rpm;
    uint32_t duty, freq;
    int      manual_duty;   /* >=0 = 手动 */
    PID_t    pid;
} g_mc[MOTOR_MAX];

/* ==================== 公开 API ==================== */

void motor_control_init(uint8_t motorID, float dt_sec, float kp, float ki, float kd)
{
    int idx = motorID - 1;
    if (idx >= MOTOR_MAX) return;

    motor_init(motorID);

    /* 启动编码器计数器 */
    if (motorID == 1)
        DL_TimerA_startCounter(COMPARE_0_INST);   /* TIMA1, PA17 */
    else
        DL_TimerA_startCounter(COMPARE_2_INST);   /* TIMA0, PA21 */

    pid_init(&g_mc[idx].pid, kp, ki, kd, dt_sec, PWM_MIN, PWM_MAX);
    g_mc[idx].init = 1;
    g_mc[idx].manual_duty = -1;
}

void motor_control_set_speed(uint8_t motorID, int32_t rpm)
{
    int idx = motorID - 1;
    if (idx >= MOTOR_MAX) return;

    g_mc[idx].manual_duty = -1;

    if (rpm > 0) {
        motor_set_direction(motorID, 0);
        g_mc[idx].target_rpm = rpm;
    } else if (rpm < 0) {
        motor_set_direction(motorID, 1);
        g_mc[idx].target_rpm = -rpm;
    } else {
        g_mc[idx].target_rpm = 0;
        motor_set_duty(motorID, 0);
    }
}

void motor_control_stop(uint8_t motorID)
{
    int idx = motorID - 1;
    if (idx >= MOTOR_MAX) return;
    g_mc[idx].target_rpm = 0;
    g_mc[idx].manual_duty = 0;
}

void motor_control_set_duty(uint8_t motorID, uint32_t duty)
{
    int idx = motorID - 1;
    if (idx >= MOTOR_MAX) return;
    g_mc[idx].manual_duty = (int)duty;
    if (g_mc[idx].manual_duty < 0) g_mc[idx].manual_duty = 0;
    if (g_mc[idx].manual_duty > 4000) g_mc[idx].manual_duty = 4000;
}

void motor_control_update(void)
{
    for (int i = 0; i < MOTOR_MAX; i++) {
        if (!g_mc[i].init) continue;

        uint8_t motorID = (uint8_t)(i + 1);
        g_mc[i].freq = measure_freq(i);
        g_mc[i].actual_rpm = (int32_t)FREQ_TO_RPM(g_mc[i].freq);

        uint32_t duty;
        if (g_mc[i].manual_duty >= 0) {
            duty = (uint32_t)g_mc[i].manual_duty;
            g_mc[i].pid.integral = 0;
            g_mc[i].pid.prev_error = 0;
        } else if (g_mc[i].target_rpm > 0) {
            float df = pid_update(&g_mc[i].pid, (float)g_mc[i].target_rpm,
                                  (float)g_mc[i].actual_rpm);
            if (df < 0) df = 0;
            duty = (uint32_t)df;
        } else {
            duty = 0;
            g_mc[i].pid.integral = 0;
            g_mc[i].pid.prev_error = 0;
        }

        motor_set_duty(motorID, duty);
        g_mc[i].duty = duty;
    }
}

/* getters */
int32_t  motor_control_get_target_rpm(uint8_t id) { int i=id-1; return (i<MOTOR_MAX)?g_mc[i].target_rpm:0; }
int32_t  motor_control_get_actual_rpm(uint8_t id) { int i=id-1; return (i<MOTOR_MAX)?g_mc[i].actual_rpm:0; }
uint32_t motor_control_get_duty(uint8_t id)       { int i=id-1; return (i<MOTOR_MAX)?g_mc[i].duty:0; }
uint32_t motor_control_get_freq(uint8_t id)       { int i=id-1; return (i<MOTOR_MAX)?g_mc[i].freq:0; }
void motor_control_set_kp(uint8_t id, float v)    { int i=id-1; if (i<MOTOR_MAX) g_mc[i].pid.Kp=v; }
void motor_control_set_ki(uint8_t id, float v)    { int i=id-1; if (i<MOTOR_MAX) g_mc[i].pid.Ki=v; }
void motor_control_set_kd(uint8_t id, float v)    { int i=id-1; if (i<MOTOR_MAX) g_mc[i].pid.Kd=v; }
