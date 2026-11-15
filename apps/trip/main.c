#include "rnse_app.h"
static const RnseAppHost *host;
static int km, rate;
static void number(char *b, unsigned n) {
    char t[12];
    int i = 0, j;
    do {
        t[i++] = '0' + n % 10;
        n /= 10;
    } while (n);
    for (j = 0; j < i; j++)
        b[j] = t[i - j - 1];
    b[i] = 0;
}
void app_init(const RnseAppHost *h) {
    host = h;
    km = 100;
    rate = 8;
}
void app_input(int event, int value) {
    if (event == APP_TURN) {
        km += value * 10;
        if (km < 0)
            km = 0;
        if (km > 9990)
            km = 9990;
    }
    if (event == APP_SELECT)
        km = 100;
    if (event == APP_TL && rate > 1)
        rate--;
    if (event == APP_TR && rate < 30)
        rate++;
}
void app_update(u32 now) { (void)now; }
void app_draw(void) {
    char b[16];
    host->fill(0, 0, 400, 136, 0);
    host->text(16, 10, "Trip fuel estimate", 2, 2);
    number(b, (unsigned)km);
    host->text(16, 37, b, 1, 3);
    host->text(120, 45, "km", 2, 1);
    number(b, (unsigned)(km * rate) / 100);
    host->text(210, 37, b, 1, 3);
    host->text(310, 45, "litres", 2, 1);
    number(b, (unsigned)rate);
    host->text(16, 84, b, 1, 2);
    host->text(55, 88, "L/100 km   Top keys: - / +", 2, 1);
    host->text(16, 120, "Turn: distance / Press: reset", 2, 1);
}
void app_shutdown(void) {}
