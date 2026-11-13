! Start-up code and vectors.
!
! The module header's four entry points come first (the build puts this
! section at the module's load address + 0x80); the boot ROM calls the
! first one.

        .section .entry, "ax"
        .global _entry
_entry:
        .long   _start
        .long   _start
        .long   _start
        .long   0

        .text
        .global _start
_start:
        ! Privileged, register bank 0, exceptions blocked, interrupts
        ! masked: whatever state the boot ROM left is not relied on.
        mov.l   1f, r0
        ldc     r0, sr
        mov.l   2f, r15
        mov.l   3f, r0
        ldc     r0, vbr
        ! Clear .bss.
        mov.l   4f, r1
        mov.l   5f, r2
        mov     #0, r0
6:      cmp/hs  r2, r1
        bt      7f
        mov.l   r0, @r1
        bra     6b
         add    #4, r1
7:      ! Exceptions may be taken from here on (BL = 0); interrupts stay
        ! masked until board_init has set the hardware up.
        mov.l   8f, r0
        ldc     r0, sr
        mov.l   9f, r0
        jsr     @r0
         nop
10:     bra     10b
         nop
        .align  2
1:      .long   0x500000f0
2:      .long   _stack_top
3:      .long   _vectors
4:      .long   _bss_start
5:      .long   _bss_end
8:      .long   0x400000f0
9:      .long   main

! Allow interrupts of every level (called by board_init at its end).
        .global irq_enable
irq_enable:
        mov.l   1f, r0
        ldc     r0, sr
        rts
         nop
        .align  2
1:      .long   0x40000000

! Stop the CPU until the next interrupt.
        .global cpu_idle
cpu_idle:
        sleep
        rts
         nop

! Exception and interrupt entry points, at the offsets from VBR the SH-4
! uses: 0x100 general exceptions, 0x400 TLB misses, 0x600 interrupts.
! On entry SR has MD = BL = RB = 1: r0-r7 are a bank of their own.
        .section .vectors, "ax"
        .global _vectors
_vectors:
        .org    0x100
        bra     _exception
         nop
        .org    0x400
        bra     _exception
         nop
        .org    0x600
        ! Interrupt: C code with the call-clobbered state saved.  The
        ! interrupted program's r0-r7 are in the other bank.
        sts.l   pr, @-r15
        sts.l   mach, @-r15
        sts.l   macl, @-r15
        stc.l   gbr, @-r15
        mov.l   1f, r0
        jsr     @r0
         nop
        ldc.l   @r15+, gbr
        lds.l   @r15+, macl
        lds.l   @r15+, mach
        lds.l   @r15+, pr
        rte
         nop
_exception:
        mov.l   2f, r0
        mov.l   @r0, r4                 ! EXPEVT
        mov.l   3f, r0
        mov.l   @r0, r5                 ! INTEVT
        stc     spc, r6
        mov.l   4f, r0
        jsr     @r0
         nop
5:      bra     5b
         nop
        .align  2
1:      .long   interrupt
2:      .long   0xff000024
3:      .long   0xff000028
4:      .long   unexpected

        .section .note.GNU-stack, "", @progbits
