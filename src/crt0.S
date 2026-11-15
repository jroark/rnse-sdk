! Start-up code and vectors.
!
! The module header's four entry points come first (the build puts this
! section at the module's load address + 0x80); whoever starts a loader
! calls the first one, with the screen language and two more values in
! r4-r6, and after it the third.
!
! Behind them a mark for `rnse custom`: the size of the memory the
! program uses from its load address on, and two words in which it puts
! the offsets of the directories of the stock loader it packs into the
! image above that (see wayback.c).

        .section .entry, "ax"
        .global _entry
_entry:
        .long   _start
        .long   _start
        .long   _start
        .long   0
        .ascii  "RNSE"
        .long   SIZE
        .global way_back_offset
way_back_offset:
        .long   0, 0

        .text
        .global _start
_start:
        ! Privileged, register bank 0, exceptions blocked, interrupts
        ! masked: the installed software calls this with its interrupts
        ! open and its own handlers in place, and none of that is relied
        ! on.  The arguments are kept for the stock loader, should the
        ! program hand over to it in the end.
        ! (What the caller had is kept as well, for the curious: SR,
        ! VBR, the return address and the stack pointer.)
        stc     sr, r11
        stc     vbr, r12
        sts     pr, r13
        mov     r15, r14
        mov.l   1f, r0
        ldc     r0, sr
        mov     r4, r8
        mov     r5, r9
        mov     r6, r10
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
7:      mov.l   11f, r1
        mov.l   r8, @r1
        mov.l   r9, @(4, r1)
        mov.l   r10, @(8, r1)
        mov.l   12f, r1
        mov.l   r11, @r1
        mov.l   r12, @(4, r1)
        mov.l   r13, @(8, r1)
        mov.l   r14, @(12, r1)
        ! Exceptions may be taken from here on (BL = 0); interrupts stay
        ! masked until board_init has set the hardware up.
        mov.l   8f, r0
        ldc     r0, sr
        ! A program installed in flash counts its starts before anything
        ! else (flash.c): nothing for one started any other way.
        mov.l   13f, r0
        jsr     @r0
         mov    r8, r4
        mov.l   9f, r0
        jsr     @r0
         nop
        mov     r0, r4
        mov.l   14f, r0
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
11:     .long   start_args
12:     .long   start_state
13:     .long   firmware_guard
14:     .long   loader_return

! Allow interrupts of every level (called by board_init at its end).
        .global irq_enable
irq_enable:
        mov.l   1f, r0
        ldc     r0, sr
        rts
         nop
        .align  2
1:      .long   0x40000000

! Start the unit again: interrupts masked, and to the boot ROM's reset
! code, which is at the start of the flash on both units.  That is where
! the stock loaders end an update as well.  Nothing is written anywhere:
! the unit starts its installed software, and with the disc still in
! the drive that software starts the program again.  The display's
! motor is stopped first: the jump is no reset, the port keeps its
! levels, and nothing would stop the motor until the OS is up.
        .global restart
restart:
        mov.l   1f, r0
        ldc     r0, sr
        mov.l   3f, r0
        jsr     @r0
         nop
        mov.l   2f, r0
        jmp     @r0
         nop
        .align  2
1:      .long   0x400000f0
2:      .long   0xa0000000
3:      .long   display_halt

! Mask interrupts of every level.
        .global irq_disable
irq_disable:
        mov.l   1f, r0
        ldc     r0, sr
        rts
         nop
        .align  2
1:      .long   0x400000f0

! Mask interrupts of every level and return what SR was, for
! irq_restore: around a short piece of code that must not meet an
! interrupt handler, whether interrupts were open before or not.
        .global irq_save
irq_save:
        stc     sr, r0
        mov     r0, r1
        or      #0xf0, r0
        ldc     r0, sr
        rts
         mov    r1, r0

        .global irq_restore
irq_restore:
        ldc     r4, sr
        rts
         nop

! The last step of handing over to the stock loader (wayback.c).  Copied
! to memory outside the program's place and run there through its
! uncached address, since it replaces the program it was part of:
!   r4  the unpacked stock loader
!   r5  where it goes, as an uncached address
!   r6  its length in long words
!   r7  eight long words: the place's cached address, its length in
!       cache lines, the loader's first and third entry points, the
!       three arguments, a stack
! Position-independent, and it uses nothing but what it is given.
        .global way_back_stub, way_back_stub_end
        .align  2
way_back_stub:
        ! The watchdog's strobe (a byte port, bit 0) and two port bits
        ! to show progress on, where the unit has them (192): else 0.
        mov.l   @(32, r7), r10
        mov.l   @(36, r7), r11
        mov.l   @(40, r7), r12
        mov.w   7f, r13
        ! Progress 1: the stub runs (bit 3 low).
        tst     r11, r11
        bt      10f
        mov     r12, r0
        mov     #-9, r1
        and     r1, r0
        mov.b   r0, @r11
10:
        ! What the data cache holds of the place goes to memory and out
        ! of the cache, so that nothing is written back later over what
        ! is about to be put there.
        mov.l   @(0, r7), r1
        mov.l   @(4, r7), r2
1:      ocbp    @r1
        dt      r2
        bf/s    1b
         add    #32, r1
        ! The instruction cache forgets the program (CCR.ICI; this code
        ! runs uncached, as changing CCR requires).
        mov.l   9f, r1
        mov.l   @r1, r0
        mov.w   8f, r2
        or      r2, r0
        mov.l   r0, @r1
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        ! Progress 2: the caches are done with (both bits low).
        tst     r11, r11
        bt      11f
        mov     r12, r0
        mov     #-13, r1
        and     r1, r0
        mov.b   r0, @r11
11:
        ! The copy, with an edge on the watchdog's strobe every 4096
        ! words: this code runs uncached and interrupts are off, so on
        ! a real unit the copy takes its time and nothing else strobes.
2:      mov.l   @r4+, r0
        mov.l   r0, @r5
        add     #4, r5
        tst     r13, r6
        bf      12f
        tst     r10, r10
        bt      12f
        mov.b   @r10, r0
        xor     #1, r0
        mov.b   r0, @r10
12:     dt      r6
        bf      2b
        ! Progress 3: the stock loader is in place (bit 3 high, bit 2 low).
        tst     r11, r11
        bt      13f
        mov     r12, r0
        mov     #-5, r1
        and     r1, r0
        mov.b   r0, @r11
13:
        mov.l   @(8, r7), r8
        mov.l   @(12, r7), r9
        mov.l   @(28, r7), r15
        mov.l   @(16, r7), r4
        mov.l   @(20, r7), r5
        jsr     @r8
         mov.l  @(24, r7), r6
        jsr     @r9
         nop
3:      bra     3b
         nop
        .align  2
9:      .long   0xff00001c
8:      .word   0x0800
7:      .word   0x0fff
        .align  2
way_back_stub_end:

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
