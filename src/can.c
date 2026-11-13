/* Received CAN frames, from the interrupt to the program: a ring of 32
 * with one writer (the interrupt) and one reader. */
#include "rnse.h"

#define RING 32

static struct {
    u16 id;
    u8 len;
    u8 data[8];
} ring[RING];
static volatile u32 head; /* where the interrupt writes next */
static volatile u32 tail; /* where the program reads next */
static volatile u32 lost;

void can_interrupt(void)
{
    u16 id;
    u8 data[8];
    int len, i;
    while ((len = can_take(&id, data)) != -1) {
        u32 next = (head + 1) & (RING - 1);
        if (len < 0 || next == tail) {
            lost++;
            continue;
        }
        ring[head].id = id;
        ring[head].len = (u8)len;
        for (i = 0; i < len; i++)
            ring[head].data[i] = data[i];
        head = next;
    }
}

int can_receive(u16 *id, u8 *data)
{
    int len, i;
    if (tail == head)
        return -1;
    *id = ring[tail].id;
    len = ring[tail].len;
    for (i = 0; i < len; i++)
        data[i] = ring[tail].data[i];
    tail = (tail + 1) & (RING - 1);
    return len;
}

u32 can_lost(void)
{
    return lost;
}
