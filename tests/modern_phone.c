/* Host tests of the exact firmware phone client. CAN peer and time are
 * fixtures. */
#include "../examples/modern/phone.h"
#include "rnse.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static u32 now;
static u8 frames[256][8];
static int lengths[256], count, rx_seq;
u32 millis(void) { return now; }
int can_send_for(u16 id, const u8 *d, int n, u32 us) {
    assert(id == 0x6c4 && n > 0 && n <= 8 && us == 3000);
    assert(count < 256);
    memcpy(frames[count], d, (size_t)n);
    lengths[count++] = n;
    return 0;
}
static void input(const u8 *d, int n) { phone_receive(0x6c5, d, n); }
static void block(const char *s) {
    int len = (int)strlen(s);
    while (len) {
        int n = len > 7 ? 7 : len;
        u8 d[8];
        d[0] = (len == n ? 0x10 : 0x20) | rx_seq;
        memcpy(d + 1, s, (size_t)n);
        rx_seq = (rx_seq + 1) & 15;
        input(d, n + 1);
        s += n;
        len -= n;
    }
}
static void ack(void) {
    u8 d = 0xb0 | ((frames[count - 1][0] + 1) & 15);
    input(&d, 1);
}
static void drain(const char *expected) {
    char result[80] = {0};
    int at = 0, i;
    for (i = 0; i < 20; i++) {
        int before = count;
        phone_poll();
        if (count == before)
            continue;
        assert(frames[count - 1][0] <= 0x3f);
        int n = lengths[count - 1] - 1;
        memcpy(result + at, frames[count - 1] + 1, (size_t)n);
        at += n;
        ack();
    }
    if (strcmp(result, expected))
        fprintf(stderr, "drain got [%s], expected [%s]\n", result, expected);
    assert(strcmp(result, expected) == 0);
}
static void connect(void) {
    const u8 answer[] = {0xa1, 0x0f, 0x8a, 0xff, 0x4a, 0xff};
    phone_disconnect();
    count = 0;
    rx_seq = 0;
    phone_connect();
    assert(frames[count - 1][0] == 0xa0);
    input(answer, 6);
    drain("AT\r");
    block("\r\nOK\r\n");
    assert(strcmp(phone_status(), "Factory module responding") == 0);
}
int main(void) {
    phone_connect();
    now += 2001;
    phone_poll();
    assert(strstr(phone_status(), "did not answer"));
    connect();
    assert(phone_dial("12;ATH") < 0);
    assert(phone_dial("+123") == 0);
    drain("ATD+123;\r");
    block("\r\nOK\r\n");
    assert(strcmp(phone_status(), "Dial request accepted") == 0);
    assert(phone_answer() < 0); /* OK alone never means an active call. */
    now += 501;
    phone_poll();
    ack();
    drain("AT+CLCC\r");
    block("\r\n+CLCC: 1,0,0,0,0,\"+123\",145\r\nOK\r\n");
    assert(strcmp(phone_status(), "Call active") == 0);
    assert(strcmp(phone_number(), "+123") == 0);
    assert(phone_hangup() == 0);
    drain("AT+CHUP\r");
    block("\r\nOK\r\nNO CARRIER\r\n");
    assert(phone_number()[0] == 0);
    block("\r\nRING\r\n");
    assert(phone_ringing());
    assert(phone_answer() == 0);
    drain("ATA\r");
    block("\r\nOK\r\n");
    assert(phone_ringing());
    block("\r\n+CLCC: 1,1,0,0,0,\"123\",129\r\n");
    assert(!phone_ringing());
    connect();
    /* An OK may arrive before the transport ACK; preserve sequence ownership.
     */
    assert(phone_dial("12") == 0);
    phone_poll();
    block("\r\nOK\r\n");
    /* Last frame emitted was a reply ACK, so acknowledge the outgoing seq
     * explicitly. */
    {
        u8 d = 0xb2;
        input(&d, 1);
    } /* init was sequence 0, dial was sequence 1 */
    now += 501;
    drain("AT+CLCC\r");
    block("\r\nOK\r\n");
    /* During a status query, a user call is deferred once and sent after its
     * reply. */
    now += 501;
    drain("AT+CLCC\r");
    assert(phone_dial("123") == 0);
    assert(phone_dial("123") < 0);
    block("\r\nOK\r\n");
    drain("ATD123;\r");
    block("\r\nERROR\r\n");
    assert(strstr(phone_status(), "rejected"));
    connect();
    now += 501;
    drain("AT+CLCC\r");
    assert(phone_dial("123") == 0);
    block("\r\n+CLCC: 1,0,0,0,0,\"123\",129\r\nOK\r\n");
    drain("");
    assert(strstr(phone_status(), "request cancelled"));
    connect();
    phone_poll();
    now += 3001;
    phone_poll(); /* idle read query starts after elapsed idle */
    now += 3001;
    phone_poll();
    assert(strstr(phone_status(), "timed out"));
    connect();
    {
        char long_line[140];
        memset(long_line, 'X', sizeof(long_line) - 1);
        long_line[sizeof(long_line) - 1] = 0;
        block(long_line);
        assert(strstr(phone_status(), "line too long"));
    }
    connect();
    {
        u8 wrong[] = {0x1f, 'O', 'K', '\r'};
        input(wrong, 4);
        assert(frames[count - 1][0] == 0xb1);
        assert(!phone_ringing());
    }
    puts("modern phone: transport, call replies, malformed input and timeouts "
         "passed");
}
