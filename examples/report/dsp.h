/* What dsp_probe() found out about each of the 192's card processors:
 *   0  how far it got: 1 = all of it; 0xe1.. = no answer at that step
 *      (e1 the boot ROM took no word, e2 the program, e3 no word from
 *      the program, e4/e6 it did not take the count, or the loop did
 *      not end, e5 the clock was not switched)
 *   1  the port pins after reset
 *   2  the clock control register after reset
 *   3  the host status register as the program finds it
 *   4  counts of timer 0 for the loops on the clock input
 *   5  the same on the multiplied clock
 *   6  the number of loops, of 32768 clocks each */
#define DSP_VALUES 7
extern u32 dsp_values[2][DSP_VALUES];
void dsp_probe(void);
