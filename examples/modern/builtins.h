#ifndef MODERN_BUILTINS_H
#define MODERN_BUILTINS_H
#include "ui.h"
/* Returns nonzero when a key/view belongs to these built-ins. */
int builtin_open(int key);
int builtin_key(int key);
void builtin_turn(int delta);
void builtin_render(int volume);
void builtin_init(void);
int builtin_poll(void);
#endif
