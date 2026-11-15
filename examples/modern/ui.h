#include "rnse_app.h"
void ui_text(int x, int y, const char *s, u8 c, int scale);
void ui_app_fill(int x, int y, int w, int h, u8 c);
void ui_app_text(int x, int y, const char *s, u8 c, int scale);
void ui_frame(const char *title, const char *tl, const char *tr, const char *bl,
              const char *br, int volume);
void ui_row(int row, const char *s, int selected);
void ui_number(char *out, int n);
