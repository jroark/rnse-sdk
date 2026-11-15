#include "rnse.h"
int main(void)
{
    board_init();
    panel_init();
    console_puts("rnse sdk: returntest\n");
    screen_palette(1, 255, 255, 255);
    screen_text(8, 60, "RETURN TEST", 1, 2);
    delay_ms(200);
#ifdef EXPLICIT_RETURN
    loader_return(23);
#endif
    return 17;
}
