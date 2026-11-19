/* The fold-down display: the screen tilts down and out on a motor to
 * uncover the CD drive and the two SD card slots.
 *
 * The motor is run as the stock software runs it (V2220371 traced in
 * the emulator, docs/hardware.md): one of the two motor lines on, and
 * off again once the position has stopped changing, wherever that is.
 * There is no end switch that cuts the motor, so the stop is ours to
 * make.  It is made from the 10 ms tick (display_tick, called by the
 * hardware layer's interrupt handler) rather than from the program's
 * loop, so that a program that stops calling anything still stops the
 * motor; and every hand-over to another program stops it at once
 * (display_halt), since the next one knows nothing of it.
 *
 * The numbers: the stock software stopped some 180 ms (open) and
 * 350 ms (closed) after the reading had settled; a reading within
 * STILL of where it was STALL_MS ago counts as settled.  LIMIT_MS is a
 * last resort, well beyond the sweep (1.5 s in the emulator; not yet
 * measured on a unit: examples/suite, test 19, does). */
#include "rnse.h"

#define OPEN_MAX 94    /* the stock software: at most 94 is fully open */
#define CLOSED_MIN 894 /* and at least 894 closed */
#define STILL 8
#define STALL_MS 300
#define LIMIT_MS 8000

void (*display_idle)(void);

static int ready;          /* the port's lines are set up */
static volatile int dir;   /* -1 opening, 1 closing, 0 still */
static volatile int stuck; /* the last run ended in between */
static volatile int last;  /* the last reading of the tick */
static int ref;
static u32 ref_time, start_time;
static volatile u32 last_run_ms;
static int key_down;

/* An average of four conversions, -1 when one did not finish. */
static int position_now(void)
{
    int i, v, sum = 0;
    for (i = 0; i < 4; i++) {
        v = adc_convert(display_input);
        if (v < 0)
            return -1;
        sum += v;
    }
    return sum >> 2;
}

static void setup(void)
{
    if (ready)
        return;
    display_port_init();
    display_motor(0);
    ready = 1;
}

int adc_read(int input)
{
    u32 sr = irq_save();
    int v = adc_convert(input);
    irq_restore(sr);
    return v;
}

int display_position(void)
{
    u32 sr = irq_save();
    int v = position_now();
    irq_restore(sr);
    return v;
}

static int is_open(int pos)
{
    return (pos >= 0 && pos <= OPEN_MAX) || display_open_switch();
}

static void stop_now(void)
{
    display_motor(0);
    dir = 0;
    last_run_ms = millis() - start_time;
    stuck = !is_open(last) && last < CLOSED_MIN;
}

void display_tick(void)
{
    int pos;
    u32 now;
    if (!dir)
        return;
    now = millis();
    pos = position_now();
    if (pos >= 0) {
        last = pos;
        if (pos > ref + STILL || pos < ref - STILL) {
            ref = pos;
            ref_time = now;
        }
    }
    if (now - ref_time >= STALL_MS || now - start_time >= LIMIT_MS)
        stop_now();
}

static void start(int towards)
{
    u32 sr;
    setup();
    sr = irq_save();
    last = position_now();
    ref = last;
    start_time = ref_time = millis();
    stuck = 0;
    dir = towards;
    display_motor(towards);
    irq_restore(sr);
}

int display_state(void)
{
    int pos;
    if (dir)
        return dir < 0 ? DISPLAY_OPENING : DISPLAY_CLOSING;
    pos = display_position();
    if (is_open(pos))
        return DISPLAY_OPEN;
    if (pos >= CLOSED_MIN)
        return DISPLAY_CLOSED;
    return stuck ? DISPLAY_STUCK : DISPLAY_BETWEEN;
}

void display_open(void)
{
    if (dir < 0 || (!dir && display_state() == DISPLAY_OPEN))
        return;
    start(-1);
}

void display_close(void)
{
    if (dir > 0 || (!dir && display_state() == DISPLAY_CLOSED))
        return;
    start(1);
}

void display_toggle(void)
{
    int state = display_state();
    if (state == DISPLAY_OPEN || state == DISPLAY_OPENING)
        display_close();
    else
        display_open();
}

void display_stop(void)
{
    u32 sr = irq_save();
    if (dir)
        stop_now();
    stuck = 0; /* stopped on purpose */
    irq_restore(sr);
}

/* For the hand-overs (wayback.c, restart): interrupts are masked by
 * then or about to be; nothing is touched unless this program has used
 * the display. */
void display_halt(void)
{
    if (ready) {
        display_motor(0);
        dir = 0;
    }
}

int display_wait(u32 ms)
{
    u32 t = millis();
    while (dir && millis() - t < ms) {
        if (display_idle)
            display_idle();
        else
            cpu_idle();
    }
    return display_state();
}

u32 display_run_ms(void)
{
    return last_run_ms;
}

int display_follow_key(void)
{
    int down = panel_key(KEY_DISPLAY), pressed = down && !key_down;
    key_down = down;
    if (pressed)
        display_toggle();
    return pressed;
}
