#include "builtins.h"
#include "ui.h"
#define LIMIT 128
/* SRAM records deliberately exclude stock data and the boot guard. */
typedef struct {
    u32 magic, seq, volume, freq, preset[6], dim, sum;
} Settings;
static Settings settings;
static int volume, frequency, view, focus, slot, ready, playing, current,
    source, dim;
static int count, app_count, active, dirty = 1, healthy;
static char songs[LIMIT][12], apps[LIMIT][12];
static const char *note = "";
static RnseAppHeader *app = (RnseAppHeader *)APP_BASE;
static const RnseAppHost host = {APP_ABI, ui_app_fill, ui_app_text, millis};
enum { RADIO, MEDIA, APPS, SETUP, BUILTIN, RECOVERY, RUNNING };
static const u8 eject_cmd[12] = {0x1b, 1, 0, 0, 2};
static u32 hash(const u8 *p, u32 n) {
    u32 h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
static int valid(Settings *s) {
    int i;
    if (s->magic != 0x4d4d4931 || s->sum != hash((u8 *)s, sizeof(*s) - 4) ||
        s->volume > 30 || s->freq < 1750 || s->freq > 2160 || s->dim > 1)
        return 0;
    for (i = 0; i < 6; i++)
        if (s->preset[i] < 1750 || s->preset[i] > 2160)
            return 0;
    return 1;
}
static void load_settings(void) {
    Settings a, b;
    int i;
    volatile u32 *p = (volatile u32 *)0xb003f800;
    for (i = 0; i < (int)sizeof(a) / 4; i++) {
        ((u32 *)&a)[i] = p[i];
        ((u32 *)&b)[i] = p[128 + i];
    }
    if (valid(&a) && (!valid(&b) || (s32)(a.seq - b.seq) > 0))
        settings = a;
    else if (valid(&b))
        settings = b;
    else {
        settings.magic = 0x4d4d4931;
        settings.volume = 8;
        settings.freq = 2042;
        for (i = 0; i < 6; i++)
            settings.preset[i] = 1800 + i * 40;
    }
    volume = settings.volume;
    frequency = settings.freq;
    dim = settings.dim;
}
static void save(void) {
    volatile u32 *p =
        (volatile u32 *)(0xb003f800 + ((++settings.seq & 1) ? 512 : 0));
    int i;
    settings.volume = volume;
    settings.freq = frequency;
    settings.dim = dim;
    settings.sum = hash((u8 *)&settings, sizeof(settings) - 4);
    p[0] = 0;
    for (i = 1; i < (int)sizeof(settings) / 4; i++)
        p[i] = ((u32 *)&settings)[i];
    p[0] = settings.magic;
}
static void palette(void) {
    screen_palette(0, 18, 19, 21);
    screen_palette(1, dim ? 168 : 235, dim ? 168 : 235, dim ? 168 : 235);
    screen_palette(2, 140, 143, 148);
    screen_palette(3, 155, 24, 33);
    screen_palette(4, 35, 37, 40);
}
static void freq_text(char *b, int f) {
    int n;
    ui_number(b, f / 20);
    for (n = 0; b[n]; n++)
        ;
    b[n++] = '.';
    b[n++] = '0' + (f % 20) * 5 / 10;
    if (f & 1)
        b[n++] = '5';
    b[n++] = ' ';
    b[n++] = 'M';
    b[n++] = 'H';
    b[n++] = 'z';
    b[n] = 0;
}
static void filename(char *b, const char *s) {
    int i = 0;
    while (i < 8 && s[i] != ' ') {
        b[i] = s[i];
        i++;
    }
    b[i] = 0;
}
static int wrap(int x, int n) {
    int r;
    if (x < 0) {
        r = (int)umod(0u - (u32)x, (u32)n);
        return r ? n - r : 0;
    }
    return (int)umod((u32)x, (u32)n);
}
static void stop(void) {
    if (playing)
        sd_play_stop();
    playing = 0;
}
static void play(int index) {
    stop();
    if (!ready || !count)
        return;
    current = wrap(index, count);
    source = 1;
    if (radio_source(RADIO_DECODER, volume) || sd_play(songs[current])) {
        note = "Playback failed - reload card";
    } else {
        playing = 1;
        note = "Playing";
        console_puts("modern: playing\n");
    }
    dirty = 1;
}
static void scan(void) {
    int i, r;
    u32 size;
    char name[12];
    stop();
    ready = 0;
    count = app_count = 0;
    note = "Starting card...";
    dirty = 1;
    r = sd_attach(slot);
    if (r)
        r = sd_init(slot);
    if (r) {
        note = r == -2 ? "SD requires stock 0371 firmware"
                       : "No readable card - select Reload";
        return;
    }
    ready = 1;
    for (i = 0; i < 2048; i++) {
        panel_poll();
        if (sd_list(i, name, &size) < 0)
            break;
        if (name[8] == 'M' && name[9] == 'P' && name[10] == '3' &&
            count < LIMIT) {
            int j;
            for (j = 0; j < 12; j++)
                songs[count][j] = name[j];
            count++;
        }
        if (name[8] == 'R' && name[9] == 'N' && name[10] == 'A' &&
            app_count < LIMIT) {
            int j;
            for (j = 0; j < 12; j++)
                apps[app_count][j] = name[j];
            app_count++;
        }
    }
    focus = 0;
    note = count || app_count ? "Card ready" : "No MP3 files or apps";
    console_puts("modern: card ready\n");
}
static void end_app(void) {
    if (active) {
        ((void (*)(void))(APP_BASE + app->shutdown))();
        active = 0;
        console_puts("modern: app exited\n");
    }
}
static void launch(void) {
    u32 bytes = 0, i, h = 2166136261u;
    int resume = playing, r;
    stop();
    r = sd_load(apps[focus], (void *)APP_BASE, APP_CAPACITY, &bytes);
    if (r || bytes < sizeof(*app)) {
        note = "App could not be read";
        goto done;
    }
    if (app->magic != APP_MAGIC || app->abi != APP_ABI || app->model != 192 ||
        app->base != APP_BASE || app->bytes != bytes || app->bss_end < bytes ||
        app->bss_end > APP_CAPACITY || app->title[15]) {
        note = "Incompatible app package";
        goto done;
    }
    for (i = 0; i < 5; i++) {
        u32 offsets[5] = {app->init, app->input, app->update, app->draw,
                          app->shutdown};
        u32 off = offsets[i];
        if (off < sizeof(*app) || off >= bytes || (off & 1)) {
            note = "Invalid app entry point";
            goto done;
        }
    }
    for (i = 0; i < bytes; i++)
        h = (h ^ ((i >= 24 && i < 28) ? 0 : ((u8 *)app)[i])) * 16777619u;
    if (h != app->checksum) {
        note = "App checksum failed";
        goto done;
    }
    for (i = bytes; i < app->bss_end; i++)
        ((u8 *)app)[i] = 0;
    active = 1;
    ((void (*)(const RnseAppHost *))(APP_BASE + app->init))(&host);
    view = RUNNING;
    note = "";
    console_puts("modern: app started\n");
done:
    if (resume)
        play(current);
    dirty = 1;
}
static void render(void) {
    char b[64];
    int i, n, start;
    if (view == RADIO) {
        ui_frame("Radio", "FM presets", "Turn: choose", "Tune / Presets",
                 "Save preset", volume);
        freq_text(b, frequency);
        ui_text(15, 70, b, 1, 3);
        for (i = 0; i < 3; i++) {
            int idx = (focus / 3) * 3 + i;
            freq_text(b, settings.preset[idx]);
            ui_row(i + 2, b, idx == focus);
        }
    } else if (view == MEDIA || view == APPS) {
        ui_frame(view == MEDIA ? "Media" : "Applications",
                 slot ? "SD 2" : "SD 1", "Change slot", "Reload card",
                 view == MEDIA ? "Stop" : "Launch", volume);
        n = view == MEDIA ? count : app_count;
        start = focus / 5 * 5;
        for (i = 0; i < 5 && start + i < n; i++) {
            filename(b, view == MEDIA ? songs[start + i] : apps[start + i]);
            ui_row(i, b, start + i == focus);
        }
        if (!n)
            ui_text(17, 91,
                    ready ? "No compatible files" : "Select Reload card", 1, 2);
    } else if (view == SETUP) {
        ui_frame("Settings", "Preferences", "", "Back", "Select", volume);
        ui_row(0, dim ? "Display: subdued" : "Display: normal", focus == 0);
        ui_row(1, "Restore stock firmware", focus == 1);
        ui_row(2, "RNS-E 192 / App ABI 1", 0);
    } else if (view == RECOVERY) {
        ui_frame("Recovery", "Stock disc required", "", "Cancel",
                 "Confirm restore", volume);
        ui_text(16, 75, "Insert stock update disc.", 1, 2);
        ui_text(16, 106, "Hold SELECT for 2 seconds", 1, 2);
        ui_text(16, 133, "to remove this firmware.", 1, 2);
    } else if (view == RUNNING) {
        ui_frame(app->title, "App action 1", "App action 2", "App action 3",
                 "App action 4", volume);
        ((void (*)(void))(APP_BASE + app->draw))();
    } else {
        builtin_render(volume);
    }
    if (view != RUNNING && view != RECOVERY && view != BUILTIN)
        ui_text(14, 190, note, 2, 1);
    dirty = 0;
}
static void change_view(int v) {
    end_app();
    view = v;
    focus = 0;
    note = "";
    dirty = 1;
    console_puts("modern: view ");
    console_dec(v);
    console_puts("\n");
}
int main(void) {
    int key, turn, was[45] = {0}, tuning = 0;
    u32 held = 0, last = 0;
    board_init();
    if (screen_width != 400)
        return 1;
    load_settings();
    builtin_init();
    palette();
    panel_init();
    sd_idle = panel_poll;
    can_init();
    cd_init();
    {
        const u8 allow[12] = {0x1e};
        cd_command(allow, 0, 0);
    }
    radio_start();
    if (radio_source(RADIO_TUNER, volume))
        note = "Radio did not answer";
    if (radio_tune(frequency))
        note = "Tuner did not answer";
    console_puts("modern: ready\n");
    for (;;) {
        panel_poll();
        if (builtin_poll() && view == BUILTIN)
            dirty = 1;
        if (!healthy && millis() > 10000 && panel_online()) {
            firmware_healthy();
            healthy = 1;
        }
        turn = panel_knob(KNOB_VOLUME);
        if (turn) {
            volume += turn;
            if (volume < 0)
                volume = 0;
            if (volume > 30)
                volume = 30;
            radio_volume(volume);
            save();
            dirty = 1;
        }
        turn = panel_knob(KNOB_SELECT);
        if (turn) {
            if (view == BUILTIN)
                builtin_turn(turn);
            else if (view == RUNNING)
                ((void (*)(int, int))(APP_BASE + app->input))(APP_TURN, turn);
            else if (view == RADIO && tuning) {
                frequency += turn;
                while (frequency > 2160)
                    frequency -= 411;
                while (frequency < 1750)
                    frequency += 411;
                if (radio_tune(frequency))
                    note = "Tuner did not answer";
                save();
            } else {
                int n = view == RADIO   ? 6
                        : view == MEDIA ? count
                        : view == APPS  ? app_count
                        : view == SETUP ? 2
                                        : 1;
                if (n > 0) {
                    focus = wrap(focus + turn, n);
                    if (focus < 0)
                        focus += n;
                }
            }
            dirty = 1;
        }
        for (key = 1; key < 45; key++) {
            int down = panel_key(key), press = down && !was[key];
            was[key] = down;
            if (!press)
                continue;
            dirty = 1;
            if (key == KEY_CD_EJECT) {
                cd_command(eject_cmd, 0, 0);
                continue;
            }
            if (key == KEY_RETURN) {
                if (view == BUILTIN && builtin_key(key))
                    continue;
                change_view(view == RUNNING    ? APPS
                            : view == RECOVERY ? SETUP
                                               : RADIO);
                continue;
            }
            if (key == KEY_RADIO) {
                change_view(RADIO);
                stop();
                source = 0;
                if (radio_source(RADIO_TUNER, volume))
                    note = "Radio did not answer";
                continue;
            }
            if (key == KEY_MEDIA || key == KEY_INFO || key == KEY_SETUP) {
                change_view(key == KEY_MEDIA  ? MEDIA
                            : key == KEY_INFO ? APPS
                                              : SETUP);
                continue;
            }
            if (key == KEY_NAV || key == KEY_TEL || key == KEY_NAME ||
                key == KEY_CAR) {
                change_view(BUILTIN);
                builtin_open(key);
                continue;
            }
            if (view == BUILTIN) {
                builtin_key(key);
                continue;
            }
            if (view == RUNNING) {
                int ev = key == KEY_SELECT    ? APP_SELECT
                         : key == KEY_SOFT_TL ? APP_TL
                         : key == KEY_SOFT_TR ? APP_TR
                         : key == KEY_SOFT_BL ? APP_BL
                         : key == KEY_SOFT_BR ? APP_BR
                                              : 0;
                if (ev)
                    ((void (*)(int, int))(APP_BASE + app->input))(ev, 1);
                continue;
            }
            if ((key == KEY_NEXT || key == KEY_PREV) && count && source) {
                play(current + (key == KEY_NEXT ? 1 : -1));
                continue;
            }
            if (view == RADIO) {
                if (key == KEY_SOFT_BL) {
                    tuning = !tuning;
                    note =
                        tuning ? "Turn: tune frequency" : "Turn: choose preset";
                }
                if (key == KEY_SELECT) {
                    frequency = settings.preset[focus];
                    if (radio_tune(frequency))
                        note = "Tuner did not answer";
                    save();
                }
                if (key == KEY_SOFT_BR) {
                    settings.preset[focus] = frequency;
                    save();
                    note = "Preset saved";
                }
            } else if (view == MEDIA || view == APPS) {
                if (key == KEY_SOFT_TR) {
                    slot = 1 - slot;
                    scan();
                }
                if (key == KEY_SOFT_BL)
                    scan();
                if (view == MEDIA && key == KEY_SELECT && count)
                    play(focus);
                if (view == MEDIA && key == KEY_SOFT_BR) {
                    stop();
                    note = "Stopped";
                }
                if (view == APPS && (key == KEY_SELECT || key == KEY_SOFT_BR) &&
                    app_count)
                    launch();
            } else if (view == SETUP &&
                       (key == KEY_SELECT || key == KEY_SOFT_BR)) {
                if (!focus) {
                    dim = !dim;
                    palette();
                    save();
                } else
                    change_view(RECOVERY);
            }
        }
        if (view == RECOVERY && panel_key(KEY_SELECT)) {
            if (!held)
                held = millis() | 1;
            else if ((s32)(millis() - held) > 2000) {
                stop();
                if (firmware_installed() && !firmware_remove())
                    restart();
                held = 0;
                change_view(SETUP);
                note = "Restore failed or not installed";
            }
        } else
            held = 0;
        if (playing) {
            int r = sd_play_poll();
            if (r <= 0) {
                playing = 0;
                if (r == 0)
                    play(current + 1);
                else {
                    note = "Card removed or playback failed";
                    ready = 0;
                    dirty = 1;
                }
            }
        }
        if (active && millis() - last >= 50) {
            last = millis();
            ((void (*)(u32))(APP_BASE + app->update))(last);
            if (!dirty)
                ((void (*)(void))(APP_BASE + app->draw))();
        }
        if (dirty)
            render();
        cpu_idle();
    }
}
