! side193: what the stock software calls, and the resident's entry
! from the stock software's tick.
!
! The module header's four entry points come first, as in any program
! for the loader's place.  The stock application calls the first and
! then the third, on its own task's stack, with its interrupts open;
! this code keeps all of that as it finds it and returns, so that the
! stock software goes on (docs/side193.md).  No mark for `rnse custom`
! behind the entry points: nothing here hands over to a stock loader.

        .section .entry, "ax"
        .global _entry
_entry:
        .long   _first
        .long   _first
        .long   _third
        .long   0
        .ascii  "SIDE"
        .long   0, 0, 0

        .section .inject, "ax"
_first:
        rts
         nop

! Put the resident in its place, unless one is at work there already
! (a disc put in a second time), and let it hook itself in.
_third:
        mov.l   r8, @-r15
        mov.l   r9, @-r15
        mov.l   r10, @-r15
        sts.l   pr, @-r15
        ! A resident at work has its mark in its first word and its
        ! entry in the stock software's table (side_live looks).
        mov.l   1f, r8                  ! _res_start
        mov.l   @r8, r0
        mov.l   2f, r1
        cmp/eq  r0, r1
        bf      3f
        mov.l   @(4, r8), r0            ! side_live
        jsr     @r0
         nop
        tst     r0, r0
        bf      9f
3:      ! The copy, a long word at a time, each line of it then put
        ! out of both caches' way: written back, and what the
        ! instruction cache may hold of that memory dropped.
        mov.l   4f, r9                  ! _res_load
        mov.l   5f, r10                 ! _res_end
        mov     r8, r1
6:      cmp/hs  r10, r1
        bt      7f
        mov.l   @r9+, r0
        mov.l   r0, @r1
        ocbwb   @r1
        .word   0x01e3                  ! icbi @r1
        bra     6b
         add    #4, r1
7:      ! Its variables start as zeroes.
        mov.l   10f, r1
        mov.l   11f, r2
        mov     #0, r0
8:      cmp/hs  r2, r1
        bt      12f
        mov.l   r0, @r1
        bra     8b
         add    #4, r1
12:     mov.l   @(8, r8), r0            ! side_init
        jsr     @r0
         nop
9:      lds.l   @r15+, pr
        mov.l   @r15+, r10
        mov.l   @r15+, r9
        mov.l   @r15+, r8
        rts
         mov    #0, r0
        .align  2
1:      .long   _res_start
2:      .long   0x53494445              ! "SIDE"
4:      .long   _res_load
5:      .long   _res_end
10:     .long   _bss_start
11:     .long   _bss_end

! The resident's first words: its mark, and what the piece above and
! programs brought in later call.
        .section .res.head, "ax"
        .long   0x53494445
        .long   side_live
        .long   side_init
        .long   side_log

! From the stock software's interrupt entry, in the place of its tick
! handler: that entry gets here by RTE with the interrupted program's
! registers all in place, its stack, and PR at its own way out.  The
! resident's work is done on a stack of its own with everything it
! may change put away, and then the stock handler runs as if nothing
! had been before it.  (No floating point here: the kit's code is
! built without.)
        .text
        .global side_entry
        .align  2
side_entry:
        mov.l   r0, @-r15
        mov.l   r1, @-r15
        mov.l   1f, r0
        mov.l   r15, @r0
        mov.l   2f, r15
        mov.l   r2, @-r15
        mov.l   r3, @-r15
        mov.l   r4, @-r15
        mov.l   r5, @-r15
        mov.l   r6, @-r15
        mov.l   r7, @-r15
        sts.l   pr, @-r15
        sts.l   mach, @-r15
        sts.l   macl, @-r15
        mov.l   3f, r0
        jsr     @r0
         nop
        lds.l   @r15+, macl
        lds.l   @r15+, mach
        lds.l   @r15+, pr
        mov.l   @r15+, r7
        mov.l   @r15+, r6
        mov.l   @r15+, r5
        mov.l   @r15+, r4
        mov.l   @r15+, r3
        mov.l   @r15+, r2
        mov.l   1f, r0
        mov.l   @r0, r15
        mov.l   4f, r0
        mov.l   @r0, r0
        mov.l   @r15+, r1
        jmp     @r0
         mov.l  @r15+, r0
        .align  2
1:      .long   side_sp
2:      .long   side_stack + 8192
3:      .long   side_tick
4:      .long   side_stock_tick

        .section .note.GNU-stack, "", @progbits
