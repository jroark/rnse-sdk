/* The faceplate controller's protocol, from the unit's side.
 *
 * Frames in both directions are  cmd L data sum : L is the index of the
 * last byte and sum the 8-bit sum of what precedes it.  A frame is
 * answered with the single byte 0x15.  The controller announces itself
 * with command 0x0f after its reset line is released, reports the keys
 * with 0x0c (six bytes, a bit a key) and the knobs with 0x0d (four
 * bytes: volume, -, select, -; bits 6..0 a count, bit 7 the direction:
 * clockwise for the select knob, "quieter" for the volume knob).
 * src/dev/panel.rs in the emulator has the rest of what is known. */
#include "rnse.h"

#define ACK 0x15
#define ACK_AND_FRAME 0x16
#define ERROR 0x18

static u8 rx[24];
static int rx_len;
static u32 rx_time;
static u8 keys[6];
static u8 pressed[6]; /* went down since panel_pressed last said so */
static u8 extra[6];   /* held by another source: the steering wheel (car.c) */
static int knobs[2];
static int started;       /* the controller has announced itself */
static const u8 *setting; /* next frame of panel_settings to send */
static int waiting;       /* a frame is out, not yet acknowledged */
static int attempts;
static u32 sent_time;
static u32 busy_time; /* when the line was last in use */

static void send_frame(u8 cmd, const u8 *data, int n)
{
    u8 sum = (u8)(cmd + n + 2);
    int i;
    panel_putc(cmd);
    panel_putc((u8)(n + 2));
    for (i = 0; i < n; i++) {
        panel_putc(data[i]);
        sum = (u8)(sum + data[i]);
    }
    panel_putc(sum);
    busy_time = millis();
}

static void send_setting(void)
{
    send_frame(setting[0], setting + 2, setting[1]);
    waiting = 1;
    sent_time = millis();
}

static void acknowledged(void)
{
    if (!waiting)
        return;
    waiting = 0;
    attempts = 0;
    setting += 2 + setting[1];
}

/* Every message from the controller, before it is looked at: its type,
 * its data and their length (examples/keys193). */
void (*panel_frame_hook)(int type, const u8 *data, int n);

static void frame(void)
{
    int n = rx[1] - 2, i;
    const u8 *d = rx + 2;
    if (panel_frame_hook)
        panel_frame_hook(rx[0], d, n);
    switch (rx[0]) {
    case 0x0f: /* started: it wants its settings (again) */
        started = 1;
        setting = panel_settings;
        waiting = 0;
        attempts = 0;
        for (i = 0; i < 6; i++)
            keys[i] = 0;
        break;
    case 0x0c:
        for (i = 0; i < 6 && i < n; i++) {
            pressed[i] |= (u8)(d[i] & ~keys[i]);
            keys[i] = d[i];
        }
        break;
    case 0x0d:
        if (n >= 3) {
            knobs[KNOB_VOLUME] += d[0] & 0x80 ? -(d[0] & 0x7f) : (d[0] & 0x7f);
            knobs[KNOB_SELECT] += d[2] & 0x80 ? (d[2] & 0x7f) : -(d[2] & 0x7f);
        }
        break;
    case ERROR: /* it did not like our frame: let the retry handle it */
        break;
    default:
        break;
    }
    panel_putc(ACK);
    busy_time = millis();
}

void panel_init(void)
{
    rx_len = 0;
    started = 0;
    waiting = 0;
    setting = panel_settings;
    panel_port_init();
}

/* The controller as whoever ran before left it: started and given its
 * settings (by the boot ROM, the stock software, or a loader that ran
 * panel_init and panel_poll), so that it keeps the screen lit.  Nothing
 * is reset; whatever half a frame the port holds is dropped.  Should the
 * controller announce itself again, it gets its settings as after
 * panel_init. */
void panel_attach(void)
{
    const u8 *p = panel_settings;
    while (p[1])
        p += 2 + p[1];
    rx_len = 0;
    started = 1;
    waiting = 0;
    attempts = 0;
    setting = p;
    busy_time = millis();
}

void panel_poll(void)
{
    int c;
    u32 now = millis();

    /* Half a frame and then silence: forget it. */
    if (rx_len && now - rx_time > 100)
        rx_len = 0;
    while ((c = panel_getc()) >= 0) {
        rx_time = now;
        busy_time = now;
        if (rx_len == 0 && (c == ACK || c == ACK_AND_FRAME)) {
            acknowledged();
            continue;
        }
        rx[rx_len++] = (u8)c;
        if (rx_len >= 2) {
            int last = rx[1] < 2 ? 2 : rx[1];
            if (last >= (int)sizeof rx) {
                rx_len = 0;
            } else if (rx_len > last) {
                u8 sum = 0;
                int i;
                for (i = 0; i < last; i++)
                    sum = (u8)(sum + rx[i]);
                rx_len = 0;
                if (sum == rx[last]) {
                    frame();
                } else {
                    static const u8 code = 0x80;
                    send_frame(ERROR, &code, 1);
                }
            }
        }
    }
    if (!started)
        return;
    if (waiting) {
        /* The stock software gives the controller a second and then
         * resets it; a repeat after 300 ms is this code's own choice. */
        if (now - sent_time >= 300) {
            if (++attempts > 5) {
                panel_init();
                return;
            }
            send_setting();
        }
    } else if (setting[1] && now - busy_time >= 20) {
        /* One frame at a time, with a pause after anything else on the
         * line: the stock software leaves 10 ms and more. */
        send_setting();
    }
}

int panel_online(void)
{
    return started && !setting[1];
}

int panel_key(int key)
{
    if (key < 0 || key >= 48)
        return 0;
    return (keys[key >> 3] | extra[key >> 3]) >> (key & 7) & 1;
}

void panel_put_key(int key, int down)
{
    u8 m;
    if (key < 0 || key >= 48)
        return;
    m = (u8)(1u << (key & 7));
    if (down) {
        if (!(extra[key >> 3] & m))
            pressed[key >> 3] |= m;
        extra[key >> 3] |= m;
    } else {
        extra[key >> 3] &= (u8)~m;
    }
}

void panel_put_knob(int knob, int detents)
{
    knobs[knob & 1] += detents;
}

int panel_pressed(int key)
{
    int r;
    if (key < 0 || key >= 48)
        return 0;
    r = pressed[key >> 3] >> (key & 7) & 1;
    pressed[key >> 3] &= (u8)~(1u << (key & 7));
    return r;
}

int panel_knob(int knob)
{
    int n = knobs[knob & 1];
    knobs[knob & 1] = 0;
    return n;
}
