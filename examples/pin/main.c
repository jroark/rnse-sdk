/* RNS-E 192 & 193 anti-theft PIN reader / decoder (read-only).
 *
 * - 192 (V220/V222, 400x240): reads the 14-byte serial at flash
 *   0x00ffe01f and the 4-byte plaintext ASCII PIN at flash 0x00ffe031.
 * - 193 (V574/V575/VB48, 800x480): reads the 14-byte serial at flash
 *   0x03ff401f and the 16-byte AES-128 ciphertext at flash 0x03ff403a,
 *   queries the DVD drive via SCSI INQUIRY for the 16-byte key,
 *   decrypts [PIN_u16_be][serial_14], and shows the PIN only when the
 *   decrypted serial matches.
 *
 * Read-only: never writes or erases flash.  Physical display open/close
 * (KEY_DISPLAY), disc eject (KEY_CD_EJECT), and 2 s hold-RETURN stay
 * active via panel_service(). */
#include "rnse.h"

/* ---- standard AES-128 decrypt (FIPS-197, used on 193) --------------- */

static const u8 sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
};

static const u8 rcon[10] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36 };

static u8 gf_mul(u8 a, u8 b)
{
    u8 r = 0;
    while (b) {
        if (b & 1)
            r ^= a;
        a = (u8)((a << 1) ^ ((a & 0x80) ? 0x1b : 0));
        b >>= 1;
    }
    return r;
}

static void aes128_decrypt(const u8 ct[16], const u8 key[16], u8 out[16])
{
    u8 isbox[256];
    u8 rk[11][16];
    u8 s[16], t[16];
    int i, r, c;

    for (i = 0; i < 256; i++)
        isbox[sbox[i]] = (u8)i;

    for (i = 0; i < 16; i++)
        rk[0][i] = key[i];
    for (r = 1; r <= 10; r++) {
        u8 w[4];
        w[0] = (u8)(sbox[rk[r - 1][13]] ^ rcon[r - 1]);
        w[1] = sbox[rk[r - 1][14]];
        w[2] = sbox[rk[r - 1][15]];
        w[3] = sbox[rk[r - 1][12]];
        for (c = 0; c < 4; c++) {
            for (i = 0; i < 4; i++) {
                w[i] ^= rk[r - 1][c * 4 + i];
                rk[r][c * 4 + i] = w[i];
            }
        }
    }

    for (i = 0; i < 16; i++)
        s[i] = (u8)(ct[i] ^ rk[10][i]);

    for (r = 9; r >= 0; r--) {
        for (i = 0; i < 4; i++)
            for (c = 0; c < 4; c++)
                t[i + 4 * c] = s[i + 4 * ((c + 4 - i) & 3)];
        for (i = 0; i < 16; i++)
            s[i] = (u8)(isbox[t[i]] ^ rk[r][i]);
        if (r > 0) {
            for (c = 0; c < 4; c++) {
                u8 a0 = s[4 * c + 0], a1 = s[4 * c + 1];
                u8 a2 = s[4 * c + 2], a3 = s[4 * c + 3];
                s[4 * c + 0] = (u8)(gf_mul(14, a0) ^ gf_mul(11, a1) ^ gf_mul(13, a2) ^ gf_mul(9, a3));
                s[4 * c + 1] = (u8)(gf_mul(9, a0) ^ gf_mul(14, a1) ^ gf_mul(11, a2) ^ gf_mul(13, a3));
                s[4 * c + 2] = (u8)(gf_mul(13, a0) ^ gf_mul(9, a1) ^ gf_mul(14, a2) ^ gf_mul(11, a3));
                s[4 * c + 3] = (u8)(gf_mul(11, a0) ^ gf_mul(13, a1) ^ gf_mul(9, a2) ^ gf_mul(14, a3));
            }
        }
    }

    for (i = 0; i < 16; i++)
        out[i] = s[i];
}

/* ---- formatting helpers --------------------------------------------- */

static void to_hex(const u8 *src, int n, char *dst)
{
    static const char hd[] = "0123456789ABCDEF";
    int i;
    for (i = 0; i < n; i++) {
        dst[2 * i] = hd[(src[i] >> 4) & 0xf];
        dst[2 * i + 1] = hd[src[i] & 0xf];
    }
    dst[2 * n] = 0;
}

static void to_ascii(const u8 *src, int n, char *dst)
{
    int i;
    for (i = 0; i < n; i++) {
        u8 c = src[i];
        dst[i] = (c >= 0x20 && c <= 0x7e) ? (char)c : '.';
    }
    dst[n] = 0;
}

/* ---- main ----------------------------------------------------------- */

enum {
    COL_BG = 0,
    COL_WHITE = 1,
    COL_GREEN = 2,
    COL_AMBER = 3,
    COL_RED = 4,
    COL_DIM = 5,
    COL_BAR = 6,
};

int main(void)
{
    int is_193;
    int s; /* 1 on 192 (400x240), 2 on 193 (800x480) */
    u8 serial[14];
    char serial_s[15], pin_s[5];
    int i, valid = 0;

    board_init();
    is_193 = (screen_width >= 800);
    s = is_193 ? 2 : 1;
    console_puts(is_193 ? "\nrnse sdk: pin (193)\n" : "\nrnse sdk: pin (192)\n");
    panel_init();

    /* Give the faceplate controller ~600 ms of service after panel_init
     * before any drive I/O so its start-up handshake completes and the
     * backlight stays lit on real 193 hardware. */
    {
        u32 ready = millis() + 600;
        while ((s32)(millis() - ready) < 0) {
            panel_service();
            cpu_idle();
        }
    }

    screen_palette(COL_BG, 10, 12, 18);
    screen_palette(COL_WHITE, 245, 245, 245);
    screen_palette(COL_GREEN, 80, 235, 120);
    screen_palette(COL_AMBER, 255, 190, 60);
    screen_palette(COL_RED, 255, 90, 90);
    screen_palette(COL_DIM, 150, 160, 175);
    screen_palette(COL_BAR, 35, 45, 65);

    screen_fill(0, 0, screen_width, screen_height, COL_BG);
    screen_fill(0, 0, screen_width, 26 * s, COL_BAR);
    screen_text(12 * s, 7 * s, is_193 ? "RNS-E 193 PIN DECODE" : "RNS-E 192 PIN DECODE",
                COL_WHITE, is_193 ? 3 : 2);

    if (!is_193) {
        /* 192: 14-byte serial at 0x00ffe01f, 4-byte plaintext ASCII PIN at 0x00ffe031. */
        u8 raw_pin[4];
        char raw_hex[9];
        for (i = 0; i < 14; i++)
            serial[i] = REG8(0xa0ffe01fu + (u32)i);
        valid = 1;
        for (i = 0; i < 4; i++) {
            raw_pin[i] = REG8(0xa0ffe031u + (u32)i);
            if (raw_pin[i] < '0' || raw_pin[i] > '9')
                valid = 0;
            pin_s[i] = (char)raw_pin[i];
        }
        pin_s[4] = 0;
        to_ascii(serial, 14, serial_s);
        to_hex(raw_pin, 4, raw_hex);

        if (valid) {
            screen_text(12, 42, "PIN:", COL_DIM, 2);
            screen_text(86, 36, pin_s, COL_GREEN, 5);
            screen_text(12, 86, "STATUS: VALID (FLASH 00FFE031)", COL_GREEN, 1);
            console_puts("PIN = ");
            console_puts(pin_s);
            console_puts("\n");
        } else {
            screen_text(12, 42, "PIN: NOT SET", COL_RED, 2);
            screen_text(12, 70, "STATUS: NON-DIGIT AT 00FFE031", COL_AMBER, 1);
            console_puts("PIN NOT SET\n");
        }
        screen_text(12, 112, "SERIAL:", COL_DIM, 1);
        screen_text(68, 112, serial_s, COL_WHITE, 1);
        screen_text(12, 130, "RAW:", COL_DIM, 1);
        screen_text(68, 130, raw_hex, COL_DIM, 1);

        console_puts("SERIAL: ");
        console_puts(serial_s);
        console_puts("\nRAW:    ");
        console_puts(raw_hex);
        console_puts("\n");
    } else {
        /* 193: 14-byte serial at 0x03ff401f, 16-byte AES-128 ciphertext at 0x03ff403a,
         * key = bytes 16..31 of DVD drive SCSI INQUIRY. */
        static const u8 inquiry_cmd[12] = { 0x12, 0x00, 0x00, 0x00, 0x60, 0x00 };
        u8 inq[96], block0[16], key[16], decoded[16];
        char key_s[17], ct_hex[33], dec_hex[33];
        int inq_ok;
        u32 pin_val;

        for (i = 0; i < 14; i++)
            serial[i] = REG8(0xa3ff401fu + (u32)i);
        for (i = 0; i < 16; i++)
            block0[i] = REG8(0xa3ff403au + (u32)i);
        for (i = 0; i < (int)sizeof inq; i++)
            inq[i] = 0;

        inq_ok = (cd_command(inquiry_cmd, inq, sizeof inq) == 0);
        for (i = 0; i < 16; i++)
            key[i] = inq_ok ? inq[16 + i] : 0;

        aes128_decrypt(block0, key, decoded);

        if (inq_ok) {
            int match = 1;
            for (i = 0; i < 14; i++) {
                if (decoded[2 + i] != serial[i]) {
                    match = 0;
                    break;
                }
            }
            pin_val = ((u32)decoded[0] << 8) | (u32)decoded[1];
            if (match && pin_val < 10000) {
                valid = 1;
                pin_s[0] = (char)('0' + udiv(pin_val, 1000));
                pin_s[1] = (char)('0' + umod(udiv(pin_val, 100), 10));
                pin_s[2] = (char)('0' + umod(udiv(pin_val, 10), 10));
                pin_s[3] = (char)('0' + umod(pin_val, 10));
                pin_s[4] = 0;
            }
        }

        to_ascii(serial, 14, serial_s);
        to_ascii(key, 16, key_s);
        to_hex(block0, 16, ct_hex);
        to_hex(decoded, 16, dec_hex);

        if (valid) {
            screen_text(24, 80, "PIN:", COL_DIM, 4);
            screen_text(170, 68, pin_s, COL_GREEN, 10);
            screen_text(24, 168, "STATUS: VALIDATED (SERIAL MATCH)", COL_GREEN, 2);
            console_puts("PIN = ");
            console_puts(pin_s);
            console_puts(" (VALIDATED)\n");
        } else {
            screen_text(24, 80, "PIN: NOT VALIDATED", COL_RED, 4);
            screen_text(24, 136,
                        inq_ok ? "STATUS: SERIAL MISMATCH (CHECK DRIVE KEY / FLASH)"
                               : "STATUS: ATAPI INQUIRY FAILED",
                        COL_AMBER, 2);
            console_puts("PIN NOT VALIDATED\n");
        }

        screen_text(24, 214, "SERIAL:", COL_DIM, 2);
        screen_text(130, 214, serial_s, COL_WHITE, 2);
        screen_text(24, 246, "DRIVE:", COL_DIM, 2);
        screen_text(130, 246, inq_ok ? key_s : "(INQUIRY ERROR)", COL_WHITE, 2);
        screen_text(24, 286, "FLASH:", COL_DIM, 2);
        screen_text(130, 286, ct_hex, COL_DIM, 2);
        screen_text(24, 314, "PLAIN:", COL_DIM, 2);
        screen_text(130, 314, dec_hex, COL_DIM, 2);

        console_puts("SERIAL: ");
        console_puts(serial_s);
        console_puts("\nDRIVE:  ");
        console_puts(key_s);
        console_puts("\nFLASH:  ");
        console_puts(ct_hex);
        console_puts("\nPLAIN:  ");
        console_puts(dec_hex);
        console_puts("\n");
    }

    screen_fill(0, screen_height - 22 * s, screen_width, 22 * s, COL_BAR);
    screen_text(10 * s, screen_height - 15 * s,
                "DISPLAY: OPEN/CLOSE   CD EJECT: EJECT   HOLD RETURN: LOADER",
                COL_DIM, s);

    /* Unblockable 60 s bench loop: keep panel_service() responsive for
     * display open/close, disc eject, and 2 s hold-RETURN, then restart()
     * so the disc can never trap the unit and the index stays unchanged. */
    {
        u32 end = millis() + 60000;
        for (;;) {
            panel_service();
            if (panel_return_held()) {
                screen_fill(12 * s, 184 * s, screen_width - 24 * s, 14 * s, COL_BG);
                screen_text(12 * s, 185 * s, "RETURN TO LOADER UNAVAILABLE (DISC BOOT)", COL_AMBER, s);
            }
            if ((s32)(millis() - end) >= 0) {
                console_puts("pin: restarting\n");
                restart();
            }
            cpu_idle();
        }
    }
}
