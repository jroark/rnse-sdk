/* examples/adp193 with the one write it knows: channel 67 of the stock
 * software's adaptation values from 255 back to 0, in the battery-backed
 * memory, so that the stock software ejects discs again.  See there. */
#define ADP_CLEAR 1
#include "../adp193/main.c"
