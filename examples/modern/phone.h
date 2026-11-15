#ifndef MODERN_PHONE_H
#define MODERN_PHONE_H
#include "rnse.h"
/* Factory telephone transport, not a host-side simulated phone. */
void phone_connect(void);
void phone_disconnect(void);
void phone_receive(u16 id, const u8 *data, int len);
void phone_poll(void);
int phone_dial(const char *number);
int phone_answer(void);
int phone_hangup(void);
const char *phone_status(void);
const char *phone_number(void);
int phone_ringing(void);
int phone_busy(void);
u32 phone_revision(void);
#endif
