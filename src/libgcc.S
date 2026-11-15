! The support routines GCC calls on its own for SH-4 code built with
! -m4-nofpu, in place of libgcc (the cross compilers that are easy to
! get are little-endian and their libgcc cannot be linked here).
!
! GCC assumes these keep every register it does not list as clobbered,
! so each one uses only what its pattern in GCC's sh.md allows:
!
!   __udivsi3_i4i  r0 = r4 / r5, unsigned;  may change r1, T, PR, MACH, MACL
!   __sdivsi3_i4i  r0 = r4 / r5, signed;    the same
!   __movmem_i4_even, __movmem_i4_odd
!                  copy 8 * (r6 + 1) bytes (and 4 more for _odd) from r5
!                  to r4, both long-word aligned;  may change r0-r6, T, PR
!
! Division by zero gives an undefined result, as in C.  The division
! loops are the SH-4 manual's own, with the dividend in r0 so that r4
! and r5 survive.

        .text
        .align  2

        .global __udivsi3_i4i
__udivsi3_i4i:
        mov     r4, r0
        mov     #0, r1          ! r1:r0 is the 64-bit dividend
        div0u
        .rept   32
        rotcl   r0
        div1    r5, r1
        .endr
        rts
        rotcl   r0              ! r0 = quotient

        .global __sdivsi3_i4i
__sdivsi3_i4i:
        mov     r4, r1
        rotcl   r1              ! T = sign of the dividend
        subc    r1, r1          ! r1 = 0 or -1: the dividend sign-extended
        mov     r4, r0
        add     r1, r0          ! a negative dividend in one's complement
        div0s   r5, r1
        .rept   32
        rotcl   r0
        div1    r5, r1
        .endr
        rotcl   r0              ! the quotient in one's complement ...
        mov     #0, r1
        rts
        addc    r1, r0          ! ... and in two's

        .global __movmem_i4_odd
        .global __movmem_i4_even
__movmem_i4_odd:
        mov.l   @r5+, r0
        mov.l   r0, @r4
        add     #4, r4
__movmem_i4_even:
        add     #1, r6
1:      mov.l   @r5+, r0
        mov.l   @r5+, r1
        mov.l   r0, @r4
        mov.l   r1, @(4, r4)
        dt      r6
        bf/s    1b
        add     #8, r4
        rts
        nop

        .section .note.GNU-stack,"",%progbits
