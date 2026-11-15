/* Fixed-ID factory transport documented in docs/telephone.md and tp20.rs.
 * No invented module status/network-management frames. Connection is explicit.
 * Call state is read from +CLCC; command OK never means a call connected. */
#include "phone.h"
static int state, pending, waiting, tx_len, tx_at, rx_n, line_n,
    call_state = -1;
static int rx_seq, tx_seq, command, seen_call, init;
static u32 deadline, next_poll, last_seen, revision;
static char tx[64], rx[256], line[128], number[24], deferred[48];
static int deferred_kind;
static const char *message = "Select Connect for factory module";
static const u8 parameters[6] = {0xa0, 0x0f, 0x8a, 0xff, 0x4a, 0xff};
static int prefix(const char *s, const char *p) {
    while (*p)
        if (*s++ != *p++)
            return 0;
    return 1;
}
static int equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}
static void status(const char *s) {
    message = s;
    revision++;
}
static int send(const u8 *d, int n) { return can_send_for(0x6c4, d, n, 3000); }
void phone_disconnect(void) {
    u8 d = 0xa8;
    if (state)
        send(&d, 1);
    state = pending = waiting = deferred_kind = 0;
    call_state = -1;
    number[0] = 0;
    status("Factory module disconnected");
}
static void fail(const char *s) {
    state = pending = waiting = deferred_kind = 0;
    call_state = -1;
    number[0] = 0;
    status(s);
}
void phone_connect(void) {
    phone_disconnect();
    rx_seq = tx_seq = rx_n = line_n = 0;
    init = 0;
    state = 1;
    send(parameters, 6);
    deadline = millis() + 2000;
    status("Connecting to factory module...");
}
static int queue(const char *s, int kind) {
    int i = 0;
    if (state != 2 || pending || waiting)
        return -1;
    while (s[i] && i < 62) {
        tx[i] = s[i];
        i++;
    }
    if (s[i])
        return -1;
    tx_len = i;
    tx_at = 0;
    waiting = 0;
    pending = 1;
    command = kind;
    deadline = millis() + 3000;
    return 0;
}
/* User actions may wait behind one read-only call-status query. Never retry
 * a dial request automatically: an ambiguous timeout closes the channel. */
static int action(const char *text, int kind) {
    int i;
    if (state != 2 || !init || deferred_kind)
        return -1;
    if (pending || waiting) {
        if (command != 2)
            return -1;
        for (i = 0; text[i] && i < 47; i++)
            deferred[i] = text[i];
        deferred[i] = 0;
        if (text[i])
            return -1;
        deferred_kind = kind;
        return 0;
    }
    return queue(text, kind);
}
static void process_line(void) {
    const char *p = line;
    int fields[5], i;
    if (equal(p, "RING")) {
        call_state = 4;
        status("Incoming call");
    } else if (equal(p, "NO CARRIER") || equal(p, "BUSY") ||
               equal(p, "NO ANSWER")) {
        call_state = -1;
        number[0] = 0;
        status(p[0] == 'B' ? "Line busy" : "Call ended");
    } else if (prefix(p, "+CLCC:")) {
        p += 6;
        for (i = 0; i < 5; i++) {
            while (*p == ' ' || *p == ',')
                p++;
            if (*p < '0' || *p > '9')
                return;
            fields[i] = 0;
            while (*p >= '0' && *p <= '9') {
                if (fields[i] > 100)
                    return;
                fields[i] = fields[i] * 10 + *p++ - '0';
            }
        }
        if (fields[2] > 5)
            return;
        seen_call = 1;
        call_state = fields[2];
        while (*p && *p != '"')
            p++;
        i = 0;
        if (*p == '"') {
            p++;
            while (*p && *p != '"' && i < 23) {
                if ((*p >= '0' && *p <= '9') || *p == '+' || *p == '*' ||
                    *p == '#')
                    number[i++] = *p;
                p++;
            }
        }
        number[i] = 0;
        status(call_state == 0   ? "Call active"
               : call_state == 1 ? "Call held"
               : call_state == 2 ? "Dialling"
               : call_state == 3 ? "Remote phone ringing"
                                 : "Incoming call");
    } else if (equal(p, "OK") && pending &&
               (tx_at == tx_len || (waiting && tx_len - tx_at <= 7))) {
        if (command == 1) {
            init = 1;
            status("Factory module responding");
        }
        if (command == 2 && !seen_call) {
            call_state = -1;
            number[0] = 0;
            status("No current call");
        }
        if (command == 3)
            status("Dial request accepted");
        if (command == 4)
            status("Answer request accepted");
        if (command == 5)
            status("End request accepted");
        pending = 0;
        next_poll = millis() + 500;
    } else if (equal(p, "ERROR") || prefix(p, "+CME ERROR:")) {
        pending = 0;
        next_poll = millis() + 2000;
        status("Factory module rejected command");
    }
}
void phone_receive(u16 id, const u8 *d, int len) {
    int op, i;
    u8 reply[6];
    if (id != 0x6c5 || len < 1 || !state)
        return;
    op = d[0];
    last_seen = millis();
    if (op == 0xa1 && len == 6) {
        if (state == 1) {
            state = 2;
            rx_seq = tx_seq = rx_n = line_n = 0;
            queue("AT\r", 1);
            status("Checking factory module...");
        }
        return;
    }
    if (op == 0xa0 && len == 6) {
        for (i = 0; i < 6; i++)
            reply[i] = d[i];
        reply[0] = 0xa1;
        send(reply, 6);
        state = 2;
        rx_seq = tx_seq = rx_n = line_n = 0;
        pending = waiting = 0;
        queue("AT\r", 1);
        return;
    }
    if (op == 0xa3) {
        for (i = 0; i < 6; i++)
            reply[i] = parameters[i];
        reply[0] = 0xa1;
        send(reply, 6);
        return;
    }
    if (op == 0xa8) {
        reply[0] = 0xa8;
        send(reply, 1);
        fail("Factory module closed connection");
        return;
    }
    if ((op & 0xf0) == 0xb0) {
        if (waiting && (op & 15) == ((tx_seq + 1) & 15)) {
            waiting = 0;
            tx_seq = (tx_seq + 1) & 15;
            tx_at += tx_len - tx_at > 7 ? 7 : tx_len - tx_at;
        }
        return;
    }
    if (op > 0x3f || state != 2)
        return;
    if ((op & 15) != rx_seq) {
        reply[0] = 0xb0 | rx_seq;
        send(reply, 1);
        return;
    }
    if (rx_n + len - 1 >= (int)sizeof(rx)) {
        fail("Telephone reply too long");
        return;
    }
    for (i = 1; i < len; i++)
        rx[rx_n++] = d[i];
    rx_seq = (rx_seq + 1) & 15;
    if (!(op & 0x20)) {
        reply[0] = 0xb0 | rx_seq;
        send(reply, 1);
    }
    if (op & 0x10) {
        for (i = 0; i < rx_n; i++) {
            char c = rx[i];
            if (c == '\r' || c == '\n') {
                if (line_n) {
                    line[line_n] = 0;
                    process_line();
                    line_n = 0;
                }
            } else if (c >= 32 && c < 127) {
                if (line_n >= (int)sizeof(line) - 1) {
                    fail("Telephone line too long");
                    return;
                }
                line[line_n++] = c;
            }
        }
        rx_n = 0;
    }
}
void phone_poll(void) {
    u32 now = millis();
    u8 d[8];
    int i, n;
    if (!state)
        return;
    if (state == 1) {
        if ((s32)(now - deadline) >= 0)
            fail("Factory module did not answer");
        return;
    }
    if (pending || waiting) {
        if ((s32)(now - deadline) >= 0) {
            phone_disconnect();
            status("Telephone command timed out");
            return;
        }
        if (!waiting && tx_at < tx_len) {
            n = tx_len - tx_at;
            if (n > 7)
                n = 7;
            d[0] = (tx_at + n == tx_len ? 0x10 : 0) | tx_seq;
            for (i = 0; i < n; i++)
                d[i + 1] = tx[tx_at + i];
            if (send(d, n + 1)) {
                fail("Telephone CAN send failed");
                return;
            }
            waiting = 1;
        }
    } else if (deferred_kind) {
        int kind = deferred_kind;
        deferred_kind = 0;
        if ((kind == 3 && call_state >= 0) || (kind == 4 && !phone_ringing()) ||
            (kind == 5 && call_state < 0))
            status("Call state changed - request cancelled");
        else
            queue(deferred, kind);
    } else if (init && (s32)(now - next_poll) >= 0) {
        seen_call = 0;
        queue("AT+CLCC\r", 2);
    }
    if (now - last_seen > 1500) {
        d[0] = 0xa3;
        send(d, 1);
        last_seen = now;
    }
}
int phone_dial(const char *n) {
    char b[48];
    int i = 0;
    if (!init || state != 2 || call_state >= 0)
        return -1;
    while (n[i]) {
        if (i >= 23 || !((n[i] >= '0' && n[i] <= '9') ||
                         (n[i] == '+' && i == 0) || n[i] == '*' || n[i] == '#'))
            return -1;
        b[3 + i] = n[i];
        i++;
    }
    if (!i)
        return -1;
    b[0] = 'A';
    b[1] = 'T';
    b[2] = 'D';
    b[3 + i] = ';';
    b[4 + i] = '\r';
    b[5 + i] = 0;
    return action(b, 3);
}
int phone_answer(void) { return phone_ringing() ? action("ATA\r", 4) : -1; }
int phone_hangup(void) { return call_state >= 0 ? action("AT+CHUP\r", 5) : -1; }
const char *phone_status(void) { return message; }
const char *phone_number(void) { return number; }
int phone_ringing(void) { return call_state == 4 || call_state == 5; }
int phone_busy(void) { return pending; }
u32 phone_revision(void) { return revision; }
