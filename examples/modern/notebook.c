#include "notebook.h"
/* Two 384-byte reservations immediately below the guard at 0x3ff00.
 * Stock data ends at 0x3f630; shell settings reserve 0x3f800..0x3fbff. */
typedef struct {
    u32 magic, seq;
    Contact contacts[CONTACTS];
    Destination destinations[DESTINATIONS];
    u32 checksum;
} Book;
typedef char notebook_must_fit[(sizeof(Book) <= 384) ? 1 : -1];
Contact contacts[CONTACTS];
Destination destinations[DESTINATIONS];
static u32 sequence;
static u32 hash(const u8 *p, u32 n) {
    u32 h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
static int valid(const Book *b) {
    int i, j;
    if (b->magic != 0x424f4f31 ||
        b->checksum != hash((const u8 *)b, sizeof(*b) - 4))
        return 0;
    for (i = 0; i < CONTACTS; i++) {
        if (b->contacts[i].name[19] || b->contacts[i].number[19])
            return 0;
        for (j = 0; b->contacts[i].number[j]; j++) {
            char c = b->contacts[i].number[j];
            if (!((c >= '0' && c <= '9') || (c == '+' && j == 0) || c == '*' ||
                  c == '#'))
                return 0;
        }
    }
    for (i = 0; i < DESTINATIONS; i++)
        if (b->destinations[i].name[19] || b->destinations[i].lat < -900000 ||
            b->destinations[i].lat > 900000 ||
            b->destinations[i].lon < -1800000 ||
            b->destinations[i].lon > 1800000)
            return 0;
    return 1;
}
void notebook_load(void) {
    Book a, b;
    Book *chosen = 0;
    int i;
    volatile u32 *p = (volatile u32 *)0xb003fc00;
    for (i = 0; i < (int)sizeof(Book) / 4; i++) {
        ((u32 *)&a)[i] = p[i];
        ((u32 *)&b)[i] = p[96 + i];
    }
    if (valid(&a) && (!valid(&b) || (s32)(a.seq - b.seq) > 0))
        chosen = &a;
    else if (valid(&b))
        chosen = &b;
    if (chosen) {
        sequence = chosen->seq;
        for (i = 0; i < CONTACTS; i++)
            contacts[i] = chosen->contacts[i];
        for (i = 0; i < DESTINATIONS; i++)
            destinations[i] = chosen->destinations[i];
    }
}
void notebook_save(void) {
    Book b;
    int i;
    volatile u32 *p =
        (volatile u32 *)(0xb003fc00 + ((++sequence & 1) ? 384 : 0));
    b.magic = 0x424f4f31;
    b.seq = sequence;
    for (i = 0; i < CONTACTS; i++)
        b.contacts[i] = contacts[i];
    for (i = 0; i < DESTINATIONS; i++)
        b.destinations[i] = destinations[i];
    b.checksum = hash((u8 *)&b, sizeof(b) - 4);
    p[0] = 0;
    for (i = 1; i < (int)sizeof(b) / 4; i++)
        p[i] = ((u32 *)&b)[i];
    p[0] = b.magic;
}
