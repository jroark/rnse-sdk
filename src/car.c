/* The car on the infotainment CAN bus: the ignition lock, the steering
 * wheel's keys and the radio's lines in the instrument cluster (the API
 * and what is established about each frame: include/rnse.h and
 * docs/car-bus.md). */
#include "rnse.h"

#define WHEEL_ID 0x5c3
#define WHEEL_KIND 0x39 /* the only first byte the stock software takes */
#define WHEEL_LOST 500  /* ms without a frame before a held key is let go */
#define REPEAT_FIRST 500
#define REPEAT_NEXT 200

void (*car_other)(u16 id, const u8 *data, int len);
int car_wheel_panel = 1;
u16 car_cluster_id = 0x363;

static int ignition = -1;
static u32 ignition_seen;
static u8 wheel;
static u32 wheel_seen, repeat_at;
static u8 queue[8];
static u32 q_head, q_tail;
static int cluster_on;
static u8 cluster[2][8];
static u32 cluster_due;

static void to_panel(u8 code, int down)
{
    if (!car_wheel_panel)
        return;
    switch (code) {
    case WHEEL_SEEK_UP:
        panel_put_key(KEY_NEXT, down);
        break;
    case WHEEL_SEEK_DOWN:
        panel_put_key(KEY_PREV, down);
        break;
    case WHEEL_VOLUME_UP:
        if (down)
            panel_put_knob(KNOB_VOLUME, 1);
        break;
    case WHEEL_VOLUME_DOWN:
        if (down)
            panel_put_knob(KNOB_VOLUME, -1);
        break;
    default:
        break;
    }
}

static void wheel_set(u8 code)
{
    if (code == wheel)
        return;
    if (wheel)
        to_panel(wheel, 0);
    wheel = code;
    if (!code)
        return;
    if (q_head - q_tail < 8) {
        queue[q_head & 7] = code;
        q_head++;
    }
    repeat_at = millis() + REPEAT_FIRST;
    to_panel(code, 1);
}

int car_frame(u16 id, const u8 *data, int len)
{
    if ((id == 0x271 || id == 0x2c3) && len >= 1) {
        ignition = data[0] & (CAR_KEY_IN | CAR_IGNITION);
        ignition_seen = millis();
        return 1;
    }
    if (id == WHEEL_ID && len >= 2) {
        wheel_seen = millis();
        wheel_set(data[0] == WHEEL_KIND ? data[1] : 0);
        return 1;
    }
    return 0;
}

static void send_cluster(void)
{
    /* Short waits: a cluster that is not there must not hold the
     * program up. */
    can_send_for(car_cluster_id, cluster[0], 8, 5000);
    can_send_for((u16)(car_cluster_id + 2), cluster[1], 8, 5000);
}

void car_poll(void)
{
    u16 id;
    u8 data[8];
    int len;
    u32 now;
    while ((len = can_receive(&id, data)) >= 0)
        if (!car_frame(id, data, len) && car_other)
            car_other(id, data, len);
    now = millis();
    if (wheel && now - wheel_seen > WHEEL_LOST)
        wheel_set(0);
    if (car_wheel_panel && (wheel == WHEEL_VOLUME_UP || wheel == WHEEL_VOLUME_DOWN) && (s32)(now - repeat_at) >= 0) {
        repeat_at = now + REPEAT_NEXT;
        panel_put_knob(KNOB_VOLUME, wheel == WHEEL_VOLUME_UP ? 1 : -1);
    }
    if (cluster_on && car_ignition() >= 0 && (s32)(now - cluster_due) >= 0) {
        cluster_due = now + 1000;
        send_cluster();
    }
}

void car_init(void)
{
    /* (can_attach: on the 193 can_init left a unit's bus silent.) */
    can_attach();
    ignition = -1;
    wheel = 0;
    q_head = q_tail = 0;
    panel_service_hook = car_poll;
}

int car_wheel(void)
{
    return wheel;
}

int car_wheel_next(void)
{
    u8 code;
    if (q_tail == q_head)
        return 0;
    code = queue[q_tail & 7];
    q_tail++;
    return code;
}

int car_ignition(void)
{
    if (ignition < 0 || millis() - ignition_seen > 1000)
        return -1;
    return ignition;
}

void car_cluster_text(const char *line1, const char *line2)
{
    const char *line[2];
    int i, j;
    if (!line1 && !line2) {
        cluster_on = 0;
        return;
    }
    line[0] = line1 ? line1 : "";
    line[1] = line2 ? line2 : "";
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 8 && line[i][j]; j++)
            cluster[i][j] = (u8)line[i][j];
        for (; j < 8; j++)
            cluster[i][j] = ' ';
    }
    cluster_on = 1;
    cluster_due = millis(); /* at once */
}
