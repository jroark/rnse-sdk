/* Bare-metal support for programs that run on an RNS-E in the place of
 * the update loader: loaded into RAM from a CD by the unit's boot ROM,
 * with nothing of the unit's own software underneath.
 *
 * The same source builds for both generations of the unit; what differs
 * is in src/hw192.c and src/hw193.c.  Everything a program may assume
 * about the hardware is what the stock loader has been seen to do, in
 * the order it does it; see docs/custom-firmware.md. */
#ifndef RNSE_H
#define RNSE_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed int s32;
typedef signed short s16;

#define REG8(a) (*(volatile u8 *)(a))
#define REG16(a) (*(volatile u16 *)(a))
#define REG32(a) (*(volatile u32 *)(a))

/* Board bring-up: interrupt controller, timers, debug serial port,
 * display, and a 10 ms tick that also serves the watchdog.  Called once,
 * with interrupts masked; returns with them enabled. */
void board_init(void);
/* Called by board_init between its parts when set (1 interrupts and
 * pins, 2 timers, 3 the unknown block at 0xfe008000, 4 the debug port,
 * 5 the graphics engine, 6 ports and watchdog, 7 the display unit, 8
 * the block at 0xfe800100, 9 the picture: 91 after its first word, 92 the whole picture
 * cleared, 93 after the first palette entry; 10 the tick), for a program
 * that has to find out where a unit stops.  Before the tick runs, so
 * the hook must not wait on millis or micros.  193 only. */
extern void (*board_init_step)(int step);
/* Called first when an exception nobody expects is taken (with SR.BL
 * set: interrupts blocked, a second exception resets the processor),
 * for a program that has to report where a unit stopped. */
extern void (*unexpected_hook)(u32 expevt, u32 intevt, u32 spc);

/* Milliseconds since board_init, in steps of 10. */
u32 millis(void);
void delay_ms(u32 ms);
/* Microseconds since board_init, from the tick and its timer's count;
 * wraps after 71 minutes, so use differences. */
u32 micros(void);
/* Set, the tick leaves the watchdog's strobe alone: the unit is reset
 * when the watchdog has waited long enough.  For measuring how long
 * that is, and for nothing else. */
extern volatile int watchdog_hold;
/* Stop the CPU until the next interrupt (the tick, at the latest). */
void cpu_idle(void);

/* Debug serial port (a header inside the unit). */
void console_putc(char c);
void console_puts(const char *s);
void console_hex(u32 v);
void console_dec(u32 v);

/* The picture: 8 bits per pixel through a palette of 256 colours. */
extern const int screen_width, screen_height;
void screen_palette(int index, u8 r, u8 g, u8 b);
void screen_pixel(int x, int y, u8 colour);
void screen_fill(int x, int y, int w, int h, u8 colour);
/* A picture of w x h pixels, one byte each, a row every `pitch` bytes,
 * at (x, y).  On the 192, x, w, pitch and the address multiples of 4
 * go a word at a time. */
/* Per unit (hw192.c, hw193.c): whether panel_service follows the
 * display key (not yet on the 193, whose motor lines are assumed; a
 * program that tests the motor, examples/display, sets it), and
 * whether the drive's port is the stock software's when a program
 * starts (the 193: cd_eject then uses it as it is). */
extern int display_key_followed;
/* The 193: whether board_init sets the display unit up (1, the
 * default), or leaves it as the software before left it (0) for
 * screen_init to do later.  Nothing on the 192. */
extern int board_init_display;
void screen_init(void);
extern const int cd_port_set_up_at_start;

void screen_blit(int x, int y, int w, int h, const u8 *pixels, int pitch);
/* Text in a 5x7 font, each dot `scale` pixels square.  Upper case,
 * digits and a little punctuation. */
void screen_text(int x, int y, const char *s, u8 colour, int scale);
/* The font itself: seven rows of a character, bit 4 the left column;
 * 0 for a character it does not have.  Lower case is upper case. */
const u8 *font_glyph(char c);

/* The faceplate: keys and the two knobs sit behind a controller of
 * their own on a serial line.  panel_init releases the controller from
 * reset; panel_poll, to be called every 10 ms or so, answers it, sends
 * it the settings the stock software sends (among them six levels that
 * look like the lighting's) and keeps the state below up to date. */
void panel_init(void);
/* Instead of panel_init: talk to the controller as it is, started and
 * with its settings, without a reset (a program started by a loader that
 * already did panel_init; the screen stays lit). */
void panel_attach(void);
void panel_poll(void);
/* The controller has started and taken its settings. */
int panel_online(void);
/* Is this key down?  KEY_* below. */
int panel_key(int key);
/* Has this key gone down since the last time this said so?  A press
 * and its release can both arrive between two looks at panel_key in a
 * program that is busy for a while (Doom loading a level): this one
 * still sees it. */
int panel_pressed(int key);
/* Detents turned since the last call: positive is clockwise for the
 * select knob and "louder" for the volume knob. */
int panel_knob(int knob);
/* Keys and detents from elsewhere (the steering wheel: car.c), seen by
 * panel_key, panel_pressed and panel_knob as if from the faceplate.  A
 * key put down stays down until it is put up again, whatever the
 * faceplate reports meanwhile. */
void panel_put_key(int key, int down);
void panel_put_knob(int knob, int detents);

enum {
    KEY_INFO = 1, KEY_NAV = 2, KEY_TEL = 3, KEY_NAME = 5, KEY_MEDIA = 6, KEY_RADIO = 7,
    KEY_SOFT_TL = 9, KEY_SOFT_TR = 10, KEY_RETURN = 11, KEY_DISPLAY = 13, KEY_SETUP = 14, KEY_CAR = 15,
    KEY_NEXT = 19, KEY_PREV = 21, KEY_SOFT_BR = 22, KEY_SOFT_BL = 23,
    KEY_CD_EJECT = 31, KEY_SELECT = 44
};
enum { KNOB_VOLUME = 0, KNOB_SELECT = 1 };

/* What the hardware layer provides to the faceplate code: the serial
 * port (panel_getc returns -1 when nothing has arrived) and the frames
 * to send after the controller has started, each as command, length of
 * the data, data; a zero length ends the list. */
void panel_port_init(void);
int panel_getc(void);
void panel_putc(u8 c);
extern const u8 panel_settings[];

/* The fold-down display: the screen tilts down and out on a motor to
 * uncover the CD drive and the two SD card slots, which is what the
 * key next to the power button (KEY_DISPLAY) does on the stock
 * software.  display_open and display_close start the motor and return
 * at once; the 10 ms tick watches the position and stops the motor
 * when it no longer changes (at an end, or against a hand), as the
 * stock software does, and after 8 s in any case.  A hand-over to
 * another program (stock_update, restart, run_image, loader_run,
 * loader_return) stops it too.  On the 192 this is the stock software's
 * own sequence; on the 193 which of the two motor lines closes is
 * assumed (src/hw193.c).  The position is the A/D converter's reading,
 * 0 (fully open, 94 at most) to 1023 (closed, 894 at least); -1 when
 * the converter did not answer. */
enum { DISPLAY_CLOSED, DISPLAY_OPEN, DISPLAY_BETWEEN, DISPLAY_OPENING, DISPLAY_CLOSING, DISPLAY_STUCK };
int display_position(void);
/* DISPLAY_STUCK: the last run stopped in between. */
int display_state(void);
void display_open(void);
void display_close(void);
/* Closes it when it is open or opening, opens it otherwise. */
void display_toggle(void);
void display_stop(void);
/* Until the motor has stopped, for at most ms; the state then.  Calls
 * display_idle meanwhile, when set (to panel_poll, say). */
int display_wait(u32 ms);
extern void (*display_idle)(void);
/* How long the last run took the motor, in ms. */
u32 display_run_ms(void);
/* The stock software's key: to be called in the loop after panel_poll;
 * a press of KEY_DISPLAY opens or closes the display.  1 when it did. */
int display_follow_key(void);
/* One conversion of an input of the A/D converter (0..6 on the 192,
 * 0..7 on the 193), ten bits; -1 when it did not finish. */
int adc_read(int input);

/* What the hardware layer provides for it: the motor lines made
 * outputs, the motor run (-1 towards open, 1 towards closed, 0 off), the
 * end switch (1: fully open), a conversion with no regard for
 * interrupts, and the converter's input with the position.
 * display_tick is called by the tick, display_halt by the hand-overs. */
void display_port_init(void);
void display_motor(int dir);
int display_open_switch(void);
int adc_convert(int input);
extern const int display_input;
void display_tick(void);
void display_halt(void);

/* Interrupts masked, and SR as it was; SR back. */
u32 irq_save(void);
void irq_restore(u32 sr);

/* The CD/DVD drive: an ATAPI device, used here by programmed I/O and
 * without interrupts.  Results: 0 = done; -1 = the drive did not react
 * in time; above 0 = the drive refused, and the value is 0x100 plus its
 * error register (the sense key in bits 7..4: 2 not ready, 6 the disc
 * has changed, ...). */
int cd_init(void);
/* An ATAPI command of twelve bytes whose answer, if any, goes to buf
 * (at most len bytes are kept). */
int cd_command(const u8 *command, void *buf, u32 len);
/* Waits until a disc can be read, for at most the given time. */
int cd_wait_ready(u32 ms);
/* The disc out (the drive allowed to release it, then ejected); and
 * the CD EJECT key, to be called in the loop after panel_poll: a press
 * ejects.  cd_follow_key returns 1 when it did. */
int cd_eject(void);
int cd_follow_key(void);
/* What every program owes the unit's keys, wherever it waits: the
 * faceplate answered (panel_poll), the display key followed
 * (display_follow_key) and CD EJECT obeyed (cd_follow_key), so that the
 * disc and the cards behind the display can always be got at.  Call it
 * in every loop, and set sd_idle and display_idle to it. */
void panel_service(void);
/* RETURN, as panel_service follows it.  Held for two seconds, it ends
 * a program started by the SD loader: loader_leave first, when set (to
 * stop sound and the like), the display brought to rest, then
 * loader_return(LOADER_RETURN_HELD).  Outside a loader session (or with
 * panel_return_to_loader cleared, as the loader itself has it) nothing
 * happens but that panel_return_held says so, once.  A press let go
 * within the two seconds is a short one: panel_return_short says so,
 * once, on its release, so that a long hold never counts as a short
 * press as well.  A RETURN already down when the program starts counts
 * only after it has been let go. */
#define LOADER_RETURN_HELD 99
extern void (*loader_leave)(void);
extern int panel_return_to_loader;
int panel_return_short(void);
int panel_return_held(void);
/* Called at the end of panel_service when set: car_init sets it to
 * car_poll, so that a program that serves its keys serves the car too. */
extern void (*panel_service_hook)(void);
/* Sectors of 2048 bytes. */
int cd_read(u32 sector, u32 count, void *buf);
/* A file in the root directory of an ISO 9660 disc, by its name in
 * upper case without the version (";1").  0 = found. */
int cd_find(const char *name, u32 *sector, u32 *size);

/* SD cards (192, with the North American software 0371 installed: the
 * card processors run that software's program, taken from the unit's
 * flash; see src/sd.c).  sd_init starts the processor of a slot (0 or
 * 1) and the card in it; 0, or what went wrong: -1 no such thing on
 * this unit, -2 other software in the flash, -3 the processor did not
 * take its program, -4 and below the card's start.  sd_read reads a
 * sector of 512 bytes.  sd_load reads a file of the root directory of
 * a FAT16 or FAT32 card, named as directories name it (eleven
 * characters, "RNSE    BIN"), to `to`, which has room for `max` bytes
 * and must have it for whole sectors: 0, -1 a read failed, -2 no such
 * file, -3 too long. */
int sd_init(int slot);
/* The 193's own SD host, under sd_init and sd_read there (one data
 * line, polled; see src/hw193.c).  sdhi_trace, when set, is told of
 * every command: its word, argument, response and result. */
int sdhi_start(int slot);
int sdhi_read(u32 sector, void *buf);
/* count sectors with one CMD18 (the host sends CMD12 by itself). */
int sdhi_read_multi(u32 sector, u32 count, void *buf);
/* The clock divider after the start (0, the default, the stock
 * software's and the fastest; 1 half that); for the next sdhi_start. */
extern u16 sdhi_fast_clock;
extern void (*sdhi_trace)(int cmd, u32 arg, u32 response, int result);
/* Called over and over while sd_init waits (it takes seconds): set it
 * to panel_poll in a program that talks to the faceplate. */
extern void (*sd_idle)(void);
/* The card as the installed software has started it, with nothing
 * reset: 0, or negative when no sector can be read that way. */
int sd_attach(int slot);
/* Not 0 while the driver is in the middle of an exchange with a card
 * processor (it calls sd_idle meanwhile): RETURN held waits for the end
 * of it before it ends the program, so that the processor is never
 * left with half a message for the loader to stumble over. */
int sd_busy(void);
/* A card put in after its slot's processor was started (by the stock
 * software or sd_init): started as the stock driver starts it then, on
 * the processor's program as it runs, without sd_init's reset.  0, or
 * as sd_init (-4 and below), -14 the processor is held in reset (a
 * program played sound on it: sd_init then).  sd_card_out tells the
 * processor of a slot whose card has gone, as the stock driver does. */
int sd_restart(int slot);
/* How a card's start is done, for trying other ways on a card that
 * will not start (examples/cardstart): the time between steps in
 * ms (150), the voltage window of ACMD41 (0x00ff0000), and, set, no
 * second ACMD41 without CMD55 while a card is busy. */
extern u32 sd_pace, sd_window;
extern int sd_no_second_acmd41;
/* (For a test.)  1: before each CMD55 and ACMD41 of a card's start, and
 * after the one that failed, the interface's control register SDCTL
 * (0x3814) read through the processor's program and kept in
 * sd_ctl_seen; 2: as well, its bit 2 taken down by reading the response
 * register in threes. */
extern int sd_unstick;
/* (For a test.)  CMD55 in the rounds of ACMD41 asked for as a command
 * with a long answer (0x40 in the register value, as CMD2's), which the
 * processor's program reads as eight words: the eighth takes the
 * interface's bit 2 down.  The card answers it short; what the
 * interface makes of that is not known. */
extern int sd_long_cmd55;
extern u16 sd_ctl_seen[24];
extern int sd_ctl_seen_n;
void sd_card_out(int slot);
/* After a failure: the last card command (the interface's command
 * register value), the header of the processor's answer and two words
 * of it. */
extern u16 sd_last[4];
/* The files of the card's top directory by number, from 0: the name as
 * the directory has it (eight characters and three, padded, and a
 * zero) and the size.  Negative past the last. */
int sd_list(int index, char *name, u32 *size);
/* An MP3 file of the card's top directory (name as for sd_load), played
 * by the card processor through its converter.  sd_play starts it;
 * sd_play_poll, called every few milliseconds, keeps it fed and says 1
 * while it plays, 0 at its end and negative for an error;
 * sd_play_stop ends it.  To hear it the amplifier wants the converter
 * as its source: radio_source(RADIO_DECODER, volume). */
int sd_play(const char *name);
/* The last 48 words between the host and the card processor, in a
 * ring (sd_heard_n counts them all; sd_heard_host is 1 for the
 * host's): what to send out when sound does not do as it should. */
extern u16 sd_heard[48];
extern u8 sd_heard_host[48];
extern u32 sd_heard_n;
/* With sd_heard_first set (and sd_heard_n cleared) the first 48 words
 * are kept instead of the last; sd_heard_ms has the time of each, in
 * milliseconds from sd_heard_from (a millis() value). */
extern int sd_heard_first;
extern u16 sd_heard_ms[48];
extern u32 sd_heard_from;
int sd_play_poll(void);
void sd_play_stop(void);

/* For a program installed in flash (`rnse firmware`, 192): take it out
 * again.  Erases the directory of the installed software, after which
 * the boot ROM takes the unit for empty and installs from the update
 * disc in the drive at the next start.  0 when the sector is erased.
 * Call restart() after it, with a stock update disc in the drive. */
int firmware_remove(void);
/* Was the program started from flash by the boot ROM?  And: it runs.
 * A program in flash counts its starts (flash.c, firmware_guard) and
 * takes itself out at the fifth in a row that did not get as far as
 * firmware_healthy(): call it once the program has shown that it
 * works, ten seconds in, say.  Until then four quick switchings off
 * and on are the way out of a program that cannot be operated. */
int firmware_installed(void);
void firmware_healthy(void);
void firmware_guard(u32 arg);

/* The radio processor (192): tuner, amplifier and power.  A command
 * with its data; the answer's data goes to `answer` and its length is
 * returned, negative when no answer came in three tries. */
int radio_command(u8 cmd, const u8 *data, int n, u8 *answer, int max);
/* The radio processor's keep-alive, 81 [06], as the stock software sends
 * it once a minute: without it a unit restarts into the stock software
 * after about seven minutes.  Call it often (panel_service does): it
 * sends at the first call and then when a minute has passed, and takes
 * some 6 ms when it does.  192 only. */
void radio_keepalive(void);
#define RADIO_CD 1
#define RADIO_TUNER 3
#define RADIO_DECODER 4
/* The amplifier's source and its volume, 0 to 30; the volume alone. */
int radio_source(int source, int volume);
int radio_volume(int volume);
/* The answer's command number after radio_command (0x20: acknowledged). */
extern int radio_answered;
/* The tuner to a frequency, in steps of 50 kHz on FM (2042: 102.1 MHz);
 * the radio processor's status block (57 bytes: 4 and 5 the frequency,
 * 33 the source, 34 the volume). */
/* For a program started by the boot ROM (from flash): the line to the
 * radio processor set up and the frames the stock software sends it
 * at every start, the tuner as the source at the end.  The number of
 * frames that went unanswered; 99 when the first two did and the rest
 * was not tried. */
extern int radio_refusal, radio_refusals;
extern u16 radio_start_log[19];
int radio_start(void);
int radio_tune(int frequency);
int radio_status(u8 *status, int max);
int sd_read(u32 sector, void *buf);
/* When set, sd_read (and with it the file functions below) reads its
 * sectors through this instead of the installed software's program: a
 * program with a card processor program of its own (examples/linkbench)
 * keeps the FAT code. */
extern int (*sd_reader)(u32 sector, void *buf);
/* A word of the installed software's card processor program's memory,
 * asked of it as its driver asks: space 0 the processor's registers
 * (0x3800-0x383f), 1 its data memory.  0, or -1 without an answer.
 * After sd_init or sd_attach. */
int sd_peek(int space, u16 addr, u16 *value);
/* A command to the card through the installed software's program, as
 * sd_init sends them: the card interface's command register value (as
 * 0x4c10 for CMD16: index, 0x400 a response, 0x40 a long one) and the
 * argument; the processor's answer in r (nine words).  0, or -1 when
 * none came.  After sd_init or sd_attach. */
int sd_command(u16 reg, u32 arg, u16 *r);
int sd_load(const char *name, void *to, u32 max, u32 *size);
/* Part of a file of the top directory (named as for sd_load), for data
 * read as it is needed.  sd_open finds the file and walks its chain of
 * clusters into `chain`, which has room for `max` of them; with chain
 * 0 it only fills in size and clusters (how many the chain wants).  0,
 * or as sd_load, -3 the chain longer than max, -4 the chain broken.
 * sd_pread reads n bytes from offset (any, within the file) to buf: 0,
 * -1 a read failed, -3 past the end.  Both call sd_idle between
 * sectors.  The card must stay the one opened. */
struct sd_file {
    u32 size, clusters, per_cluster, data_start;
    u32 *chain;
};
int sd_open(const char *name, struct sd_file *f, u32 *chain, u32 max);
int sd_pread(const struct sd_file *f, u32 offset, void *buf, u32 n);

/* What the hardware layer provides to the drive code: the task file
 * (register 0..7 as in ATA: 1 error/features, 2 interrupt reason, 4 and
 * 5 byte count, 6 drive, 7 status/command), the alternate status, and
 * the data register as words with the first byte of each pair in the
 * upper half. */
void cd_port_init(void);
u8 cd_reg_read(int reg);
void cd_reg_write(int reg, u8 v);
u8 cd_alt_status(void);
u16 cd_data_read(void);
void cd_data_write(u16 w);

/* CAN: the car's infotainment bus, 100 kbit/s, identifiers of 11 bits.
 * can_init sets the controller up as the stock loader does, with one
 * mailbox that receives everything and one to send from.  Frames are
 * taken out of the controller by interrupt and kept, up to 31 of them,
 * until can_receive asks; can_lost counts those there was no room for
 * or that the controller itself had to drop. */
void can_init(void);
/* 0 = sent and acknowledged by another node; -1 = not within 100 ms
 * (nobody else on the bus, or the bus is down), and given up. */
int can_send(u16 id, const u8 *data, int len);
/* The same with the time to wait for the acknowledgement, in
 * microseconds.  A frame of eight bytes takes some 1.3 ms on the bus
 * and is on it at least once after 3 ms, acknowledged or not: that is
 * how to send to a logic analyser, which acknowledges nothing. */
int can_send_for(u16 id, const u8 *data, int len, u32 us);
/* The length of the oldest frame waiting (0..8), its identifier and
 * data stored; -1 when there is none. */
int can_receive(u16 *id, u8 *data);
u32 can_lost(void);
/* The controller's state: its general status register (GSR) in the
 * upper half, the transmit error counter in bits 15..8 and the receive
 * error counter in bits 7..0.  In GSR, as Renesas' HCAN2 has it, bit 0
 * is bus off, bit 1 an error counter at 96 or more and bit 5 error-
 * passive (a counter at 128 or more); a 192 alone on a bench, sending
 * with nobody to acknowledge, reads 0x0026 with 0x88 transmit errors. */
u32 can_status(void);

/* The car, as the infotainment CAN bus tells it (src/car.c; what the
 * frames mean is in docs/car-bus.md, with what was seen on a unit and
 * what is only the emulator's).  car_init starts CAN and has
 * panel_service call car_poll, which takes every frame from
 * can_receive: those it knows it uses, the rest go to car_other when
 * set.  A program that reads frames itself can hand them to car_frame
 * instead (1: it was the car's).
 *
 * The steering wheel's keys (0x5C3, first byte 0x39) reach programs as
 * the faceplate's: volume up and down as detents of KNOB_VOLUME (one a
 * press, then five a second while held), seek up and down as KEY_NEXT
 * and KEY_PREV, unless car_wheel_panel is cleared.  car_wheel says
 * which code is held (0: none), car_wheel_next the codes pressed since
 * the last call, one a call, for the keys that have no faceplate key
 * yet.  A key whose frames stop is let go after half a second. */
enum {
    WHEEL_SEEK_UP = 0x04, WHEEL_SEEK_DOWN = 0x05,
    WHEEL_VOLUME_UP = 0x06, WHEEL_VOLUME_DOWN = 0x07
};
void car_init(void);
void car_poll(void);
int car_frame(u16 id, const u8 *data, int len);
extern void (*car_other)(u16 id, const u8 *data, int len);
extern int car_wheel_panel;
int car_wheel(void);
int car_wheel_next(void);
/* The ignition lock (0x271, or 0x2C3 in other cars): bit 0 the key in
 * the lock, bit 1 the ignition on; -1 when neither has come for a
 * second (the car sends it every 100 ms). */
#define CAR_KEY_IN 1
#define CAR_IGNITION 2
int car_ignition(void);
/* The radio's two lines of eight characters in the instrument
 * cluster, sent once a second while the ignition message comes (so
 * that a unit alone on a bench does not wait on frames nobody
 * acknowledges); 0 to stop.  car_cluster_id is the first line's
 * identifier, the second's is two higher: 0x363 as a unit coded for an
 * A3 (8P) sends, 0x261 as the uncoded emulated unit does. */
void car_cluster_text(const char *line1, const char *line2);
extern u16 car_cluster_id;

/* Between the hardware layer and the CAN code: can_take reads a frame
 * out of the receive mailbox (-1: none, -2: one or more were lost, call
 * again), can_interrupt is to be called for the controller's interrupt. */
int can_take(u16 *id, u8 *data);
void can_interrupt(void);

/* The way back to an ordinary unit.  The installed software starts a
 * program in the loader's place whenever the disc in the drive has a
 * higher software index than the unit, that is at every start until
 * the unit has been updated from that disc.  stock_update does that: it
 * starts the stock loader, which `rnse custom` has packed into the
 * image, exactly as the installed software would have, and the unit
 * updates itself from the disc and restarts as after any update.  It
 * returns only when it cannot do it: -1 the image carries no stock
 * loader, -2 a part of it is damaged in both of its copies. */
int stock_loader_carried(void);
int stock_update(void);
/* The other end: start the unit again and leave it as it is.  Its
 * software then finds the disc in the drive once more and, after its
 * half minute of starting and checking, runs the program again, for as
 * long as the disc is there: on a bench, where the disc can be taken
 * out in that half minute, a disc that can be used any number of
 * times, since the unit's software index never changes.  In a car,
 * where the disc is behind the display, this is the trap that
 * stock_update is there to avoid.  Does not return. */
void restart(void);
/* Another program of this kind, from memory, in this one's place: see
 * src/wayback.c.  With sd_load, programs come from an SD card. */
int run_image(void *image, u32 bytes);
/* A bigger image than the loader's place holds, loaded into RAM by a
 * program in that place (src/enter.c; the 192): everything stopped,
 * the operand cache written back over [start, end), the instruction
 * cache invalidated, then a jump to entry.  Returns -1 on the 193. */
int image_enter(u32 entry, u32 start, u32 end);
/* Cooperative SD loader session (192). Returns only outside a session.
 * main() completion calls this automatically. Reserve 0x8c7c0000..0x8c800000. */
void loader_return(int status);
/* Whether this program runs in a loader session (and so has a loader
 * to return to). */
int loader_active(void);
/* For such a bigger image: the loader_return of the program that
 * started it, which stays in the loader's place.  Set, loader_active
 * says 1 and loader_return goes through it. */
extern void (*loader_return_via)(int status);
/* Loader-side helpers: blocked is a two-bit mask of consumed insertions.
 * loader_run returns only on rejection; loader_returned consumes a completed
 * session and returns its slot, or -1 when this is a fresh launch. */
int loader_run(void *image, u32 bytes, int slot, u32 blocked);
int loader_returned(int *status, u32 *blocked);
int sd_present(int slot);
/* The program a loader session runs, by its directory name (11
 * characters), and flags of the loader's own, kept across the session
 * for the loader's next start; loader_noted returns 0 when there is
 * none (flags may be 0). */
void loader_note(const char *name, u32 flags);
int loader_noted(char *name, u32 *flags);
/* (way_back_scratch, below, is where there is room for such an image.) */
/* What the program was started with: the screen language (0..8) and
 * two values the stock loader gets as well. */
extern u32 start_args[3];
/* And what its caller had at that moment: SR, VBR, the return address
 * and the stack pointer. */
extern u32 start_state[4];
/* From the hardware layer: 4 MB of RAM (and 64 KB below it) that
 * neither the program nor the stock loader has in use. */
extern const u32 way_back_scratch;

/* The cache control register set to ccr (src/cache.S), whatever the
 * caches hold: the operand cache is written back and emptied first,
 * and both caches start empty under the new setting.  The stock
 * software leaves 0x80008181 on both units; on the 192 that is the
 * SH7750R's large caches (EMODE), both on, stores through P1 written
 * through and the rest copied back, and the index modes (IIX, OIX)
 * that give each half of the caches to one half of every 64 MB, so
 * that a program within one 32 MB has half of each. */
void cache_setup(u32 ccr);
#define CCR_OCE 0x00000001u  /* operand cache on */
#define CCR_WT 0x00000002u   /* P0, U0, P3: write-through */
#define CCR_CB 0x00000004u   /* P1: copy-back */
#define CCR_OIX 0x00000080u  /* operand cache indexed by address bit 25 */
#define CCR_ICE 0x00000100u  /* instruction cache on */
#define CCR_IIX 0x00008000u  /* instruction cache indexed by address bit 25 */
#define CCR_EMODE 0x80000000u /* SH7750R: the caches at double size */

/* Sound as samples, the 192 (src/pcm.c): a card processor (0 or 1)
 * gets a program of our own that plays 16-bit stereo samples, left
 * then right, at 44.1 kHz, out of its converter, which is what the
 * amplifier hears as RADIO_DECODER.  pcm_put hands the samples over
 * itself and returns when the processor has taken them all (it waits
 * as long as they take to play, less the last two); pcm_dma has the
 * DMA controller do it and returns at once, and pcm_dma_busy says
 * whether it is still at it.  The samples must stay where they are
 * until then.  pcm_stop puts the processor back into reset (a card in
 * its slot wants sd_init after that).  pcm_start returns 0, or below 0
 * when the processor did not take its program (or on the 193). */
int pcm_start(int dsp);
int pcm_put(const s16 *lr, u32 frames);
/* Called by pcm_put every 2048 samples, when set (to panel_service): a
 * gap in the sound if it takes longer than a sample or two.  Programs
 * that keep keys answered while playing feed by DMA. */
extern void (*pcm_idle)(void);
int pcm_dma(const s16 *lr, u32 frames);
/* (Samples through P2, uncached, need no writing back, and pcm_dma
 * does not spend time on it: the way to feed one long transfer that
 * is written ahead of as it plays.) */
int pcm_dma_busy(void);
/* The words a transfer still has to go (two a stereo sample): what a
 * program that streams in blocks looks at before it does anything long,
 * so that the next block is started in time. */
u32 pcm_dma_left(void);
/* Sound without gaps: a ring of `blocks` (2 to PCM_STREAM_MAX) blocks
 * of `frames` stereo samples each (a multiple of 8) at buf (aligned to
 * 32 bytes), played by DMA one after the other, the next started from
 * the last one's end-of-transfer interrupt.  The program fills them in
 * order: pcm_stream_block gives the next one to fill, or 0 while it is
 * still queued or playing; pcm_stream_ready says it is filled.  A block
 * not ready in time is heard as a short silence.  pcm_stream_stop ends
 * it (pcm_stop does as well). */
#define PCM_STREAM_MAX 16
int pcm_stream(s16 *buf, u32 frames, u32 blocks);
s16 *pcm_stream_block(void);
void pcm_stream_ready(void);
void pcm_stream_stop(void);
/* A sound card's way: the ring played round and round by the DMA
 * controller with nothing to say a block is ready, and pcm_loop_pos
 * the frame (0 .. frames * blocks - 1) it plays now, so that a program
 * mixes ahead of it (Quake's snd_dma.c does).  The controller reads
 * memory and not the cache: pcm_loop purges the ring from the cache,
 * and the program writes it through P2 (address | 0x20000000) or
 * writes its cache back itself.  pcm_stream_stop ends it. */
int pcm_loop(s16 *buf, u32 frames, u32 blocks);
u32 pcm_loop_pos(void);
/* From the interrupt handler: the end of a streaming transfer. */
void pcm_interrupt(void);
void pcm_stop(void);

/* Called from the vector code for anything but the tick. */
void unexpected(u32 expevt, u32 intevt, u32 spc);

/* Division without the compiler's library.  (`/` and `%` work too
 * now, through src/libgcc.S; these are kept for the code that has them.) */
u32 udiv(u32 a, u32 b);
u32 umod(u32 a, u32 b);

/* Memory to allocate from (src/heap.c): malloc() hands out pieces of
 * the block given to heap_init, aligned to 32 bytes, and free() does
 * nothing.  The kit itself allocates nothing; this is for the code in
 * third_party.  memset() and memcpy() are there as well. */
void heap_init(void *base, u32 size);
u32 heap_left(void);
void *malloc(unsigned long n);
void free(void *p);
void *memset(void *d, int c, unsigned long n);
void *memcpy(void *d, const void *s, unsigned long n);

#endif
