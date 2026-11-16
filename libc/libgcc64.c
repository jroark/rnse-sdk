/* The compiler's helpers for 64-bit integers that the SDK's libgcc.S
 * does not have (the cross compiler's own libgcc is little-endian):
 * division (FixedDiv, printf of long long), shifts by a variable count
 * and multiplication.  Written without the operations they implement. */
typedef unsigned long long u64;
typedef long long s64;
typedef unsigned int u32;

union parts {
    u64 v;
    struct { u32 hi, lo; } w; /* big-endian */
};

u64 __lshrdi3(u64 a, int n);
u64 __ashldi3(u64 a, int n);
s64 __ashrdi3(s64 a, int n);
u64 __udivmoddi4(u64 n, u64 d, u64 *rem);
u64 __udivdi3(u64 n, u64 d);
u64 __umoddi3(u64 n, u64 d);
s64 __divdi3(s64 n, s64 d);
s64 __moddi3(s64 n, s64 d);
u64 __muldi3(u64 a, u64 b);
int __cmpdi2(s64 a, s64 b);
int __ucmpdi2(u64 a, u64 b);

u64 __lshrdi3(u64 a, int n)
{
    union parts p, r;
    p.v = a;
    if (n <= 0)
        return a;
    if (n >= 64)
        return 0;
    if (n >= 32) {
        r.w.hi = 0;
        r.w.lo = p.w.hi >> (n - 32);
    } else {
        r.w.hi = p.w.hi >> n;
        r.w.lo = p.w.lo >> n | p.w.hi << (32 - n);
    }
    return r.v;
}

u64 __ashldi3(u64 a, int n)
{
    union parts p, r;
    p.v = a;
    if (n <= 0)
        return a;
    if (n >= 64)
        return 0;
    if (n >= 32) {
        r.w.lo = 0;
        r.w.hi = p.w.lo << (n - 32);
    } else {
        r.w.lo = p.w.lo << n;
        r.w.hi = p.w.hi << n | p.w.lo >> (32 - n);
    }
    return r.v;
}

s64 __ashrdi3(s64 a, int n)
{
    union parts p, r;
    p.v = (u64)a;
    if (n <= 0)
        return a;
    if (n >= 64)
        n = 63;
    if (n >= 32) {
        r.w.hi = (u32)((int)p.w.hi >> 31);
        r.w.lo = (u32)((int)p.w.hi >> (n - 32));
    } else {
        r.w.hi = (u32)((int)p.w.hi >> n);
        r.w.lo = p.w.lo >> n | p.w.hi << (32 - n);
    }
    return (s64)r.v;
}

/* Shift and subtract, from the divisor's highest bit that fits. */
u64 __udivmoddi4(u64 n, u64 d, u64 *rem)
{
    u64 q = 0, bit = 1;
    if (!d) {
        if (rem)
            *rem = n;
        return ~0ull;
    }
    while (d < n && !(d >> 63)) {
        d <<= 1;
        bit <<= 1;
    }
    while (bit) {
        if (n >= d) {
            n -= d;
            q |= bit;
        }
        d >>= 1;
        bit >>= 1;
    }
    if (rem)
        *rem = n;
    return q;
}

u64 __udivdi3(u64 n, u64 d)
{
    return __udivmoddi4(n, d, 0);
}

u64 __umoddi3(u64 n, u64 d)
{
    u64 r;
    __udivmoddi4(n, d, &r);
    return r;
}

s64 __divdi3(s64 n, s64 d)
{
    int neg = (n < 0) ^ (d < 0);
    u64 q = __udivmoddi4(n < 0 ? 0 - (u64)n : (u64)n, d < 0 ? 0 - (u64)d : (u64)d, 0);
    return neg ? (s64)(0 - q) : (s64)q;
}

s64 __moddi3(s64 n, s64 d)
{
    u64 r;
    __udivmoddi4(n < 0 ? 0 - (u64)n : (u64)n, d < 0 ? 0 - (u64)d : (u64)d, &r);
    return n < 0 ? (s64)(0 - r) : (s64)r;
}

u64 __muldi3(u64 a, u64 b)
{
    union parts x, y, r;
    x.v = a;
    y.v = b;
    /* The low words' full product (dmulu.l), and the cross terms. */
    r.v = (u64)x.w.lo * y.w.lo;
    r.w.hi += x.w.hi * y.w.lo + x.w.lo * y.w.hi;
    return r.v;
}

int __cmpdi2(s64 a, s64 b)
{
    return a < b ? 0 : a > b ? 2 : 1;
}

int __ucmpdi2(u64 a, u64 b)
{
    return a < b ? 0 : a > b ? 2 : 1;
}
