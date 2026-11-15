#ifndef MODERN_NOTEBOOK_H
#define MODERN_NOTEBOOK_H
#include "rnse.h"
#define CONTACTS 6
#define DESTINATIONS 4
typedef struct {
    char name[20], number[20];
} Contact;
typedef struct {
    char name[20];
    s32 lat, lon;
} Destination;
extern Contact contacts[CONTACTS];
extern Destination destinations[DESTINATIONS];
void notebook_load(void);
void notebook_save(void);
#endif
