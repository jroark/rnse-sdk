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

#define REG8(a) (*(volatile u8 *)(a))
#define REG16(a) (*(volatile u16 *)(a))
#define REG32(a) (*(volatile u32 *)(a))

/* Board bring-up: interrupt controller, timers, debug serial port,
 * display, and a 10 ms tick that also serves the watchdog.  Called once,
 * with interrupts masked; returns with them enabled. */
void board_init(void);

/* Milliseconds since board_init, in steps of 10. */
u32 millis(void);
void delay_ms(u32 ms);
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
/* Text in a 5x7 font, each dot `scale` pixels square.  Upper case,
 * digits and a little punctuation. */
void screen_text(int x, int y, const char *s, u8 colour, int scale);

/* The faceplate: keys and the two knobs sit behind a controller of
 * their own on a serial line.  panel_init releases the controller from
 * reset; panel_poll, to be called every 10 ms or so, answers it, sends
 * it the settings the stock software sends (among them six levels that
 * look like the lighting's) and keeps the state below up to date. */
void panel_init(void);
void panel_poll(void);
/* The controller has started and taken its settings. */
int panel_online(void);
/* Is this key down?  KEY_* below. */
int panel_key(int key);
/* Detents turned since the last call: positive is clockwise for the
 * select knob and "louder" for the volume knob. */
int panel_knob(int knob);

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
/* Sectors of 2048 bytes. */
int cd_read(u32 sector, u32 count, void *buf);
/* A file in the root directory of an ISO 9660 disc, by its name in
 * upper case without the version (";1").  0 = found. */
int cd_find(const char *name, u32 *sector, u32 *size);

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
/* The length of the oldest frame waiting (0..8), its identifier and
 * data stored; -1 when there is none. */
int can_receive(u16 *id, u8 *data);
u32 can_lost(void);

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
/* What the program was started with: the screen language (0..8) and
 * two values the stock loader gets as well. */
extern u32 start_args[3];
/* And what its caller had at that moment: SR, VBR, the return address
 * and the stack pointer. */
extern u32 start_state[4];
/* From the hardware layer: 4 MB of RAM (and 64 KB below it) that
 * neither the program nor the stock loader has in use. */
extern const u32 way_back_scratch;

/* Called from the vector code for anything but the tick. */
void unexpected(u32 expevt, u32 intevt, u32 spc);

/* Division without the compiler's library. */
u32 udiv(u32 a, u32 b);
u32 umod(u32 a, u32 b);

#endif
