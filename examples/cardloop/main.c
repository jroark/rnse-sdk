/* Reads its card without a pause, as a game that takes its data from
 * the card does (Quake), until RETURN is held: started by the SD loader,
 * it goes back to it then.  RETURN held is noticed in the card driver's
 * waits (sd_idle), that is in the middle of a read, and the driver puts
 * the return off until the read is over (sd_busy): the loader finds the
 * card processor between two messages and reads on with it as it is.
 * tests/emulated.rs, sd_loader_reopens_its_card_after_a_return_or_a_swap. */
#include "rnse.h"

int main(void)
{
    static u8 sector[512];
    char text[24];
    u32 reads = 0, i, n;
    int slot, r = -1;

    board_init();
    console_puts("rnse sdk: cardloop\n");
    screen_palette(0, 0, 0, 0);
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 96, 0);
    screen_fill(0, 0, screen_width, screen_height, 0);
    screen_text(8, 8, "READING THE CARD WITHOUT A PAUSE", 2, 2);
    screen_text(8, 40, "HOLD RETURN: BACK TO THE LOADER", 1, 2);
    /* The faceplate as the loader left it, and answered only from the
     * card driver's waits. */
    panel_attach();
    sd_idle = panel_service;
    display_idle = panel_service;
    for (slot = 0; slot < 2 && r; slot++)
        if (sd_present(slot))
            r = sd_attach(slot);
    if (r) {
        screen_text(8, 80, "NO CARD TO READ", 2, 2);
        return 1;
    }
    for (;;) {
        if (sd_read(reads & 63, sector)) {
            screen_text(8, 80, "A READ FAILED", 2, 2);
            return 2;
        }
        if (!(++reads & 255)) {
            /* "READS: n" */
            text[0] = 'R', text[1] = 'E', text[2] = 'A', text[3] = 'D', text[4] = 'S', text[5] = ' ';
            n = 6;
            for (i = 1000000000; i > 1 && reads < i; i = udiv(i, 10))
                ;
            for (; i; i = udiv(i, 10))
                text[n++] = (char)('0' + umod(udiv(reads, i), 10));
            text[n] = 0;
            screen_fill(8, 80, screen_width - 8, 16, 0);
            screen_text(8, 80, text, 1, 2);
            console_puts(text);
            console_puts("\n");
        }
    }
}
