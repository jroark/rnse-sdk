/* A visibly distinct second payload for the persistent SD loader. */
#include "rnse.h"
int main(void)
{
    u32 start;
    board_init();
    panel_init();
    console_puts("rnse sdk: returntest2\n");
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 0, 200, 255);
    screen_fill(0, 0, screen_width, 40, 2);
    screen_text(12, 60, "SECOND SD TEST", 1, 3);
    screen_text(12, 110, "RETURNING IN FIVE SECONDS", 2, 2);
    screen_text(12, 140, "EXPECT RETURN STATUS 23", 1, 2);
    start = millis();
    while (millis() - start < 5000) {
        panel_service();
        cpu_idle();
    }
    loader_return(23);
    return 23;
}
