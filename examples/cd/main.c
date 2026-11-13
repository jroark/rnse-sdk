/* The CD drive: waits for a disc, shows the volume's name, finds a file
 * in the root directory and adds its bytes up. */
#include "rnse.h"

static u8 buf[16 * 2048];

static void line(int row, const char *text, u8 colour)
{
    int s = screen_width / 400;
    screen_fill(8 * s, (8 + 20 * row) * s, screen_width - 16 * s, 14 * s, 0);
    screen_text(8 * s, (8 + 20 * row) * s, text, colour, 2 * s);
}

static char *put_hex(char *p, u32 v, int digits)
{
    while (digits--)
        *p++ = "0123456789ABCDEF"[v >> (4 * digits) & 15];
    return p;
}

int main(void)
{
    static const char file[] = "LOADING.KWI";
    char text[40], *p;
    u32 sector, size, sum = 0, done = 0;
    int r, i;

    board_init();
    console_puts("\nrnse sdk: cd\n");
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 96, 0);
    line(0, "CD DRIVE", 1);

    r = cd_init();
    if (!r)
        r = cd_wait_ready(20000);
    if (r) {
        line(1, r < 0 ? "NO ANSWER" : "NO DISC", 2);
        console_puts("no disc: ");
        console_hex((u32)r);
        console_puts("\n");
        for (;;)
            cpu_idle();
    }

    /* The volume's name: 32 characters at 40 in the descriptor. */
    r = cd_read(16, 1, buf);
    if (r || buf[1] != 'C' || buf[2] != 'D') {
        line(1, "NOT AN ISO 9660 DISC", 2);
        console_puts("not ISO 9660\n");
        for (;;)
            cpu_idle();
    }
    for (i = 0; i < 32 && buf[40 + i] != ' '; i++)
        text[i] = (char)buf[40 + i];
    text[i] = 0;
    line(1, text, 1);
    console_puts("volume ");
    console_puts(text);
    console_puts("\n");

    r = cd_find(file, &sector, &size);
    if (r) {
        line(2, "LOADING.KWI: NOT THERE", 2);
        console_puts("file not found\n");
        for (;;)
            cpu_idle();
    }
    console_puts("file at sector ");
    console_dec(sector);
    console_puts(", ");
    console_dec(size);
    console_puts(" bytes\n");

    /* The first megabyte at most, sixteen sectors at a time. */
    if (size > 0x100000)
        size = 0x100000;
    while (done < size) {
        u32 n = size - done, k;
        if (n > sizeof buf)
            n = sizeof buf;
        r = cd_read(sector + done / 2048, (n + 2047) / 2048, buf);
        if (r) {
            line(3, "READ ERROR", 2);
            console_puts("read error ");
            console_hex((u32)r);
            console_puts("\n");
            for (;;)
                cpu_idle();
        }
        for (k = 0; k < n; k++)
            sum += buf[k];
        done += n;
        p = put_hex(text, done, 6);
        *p = 0;
        line(2, text, 1);
    }
    p = text;
    *p++ = 'S';
    *p++ = 'U';
    *p++ = 'M';
    *p++ = ' ';
    p = put_hex(p, sum, 8);
    *p = 0;
    line(3, text, 1);
    console_puts("sum ");
    console_hex(sum);
    console_puts("\n");
    for (;;)
        cpu_idle();
}
