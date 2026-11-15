! void cache_setup(u32 ccr)
!
! Set the cache control register to ccr, safely from whatever state the
! caches are in: every line of the operand cache is first written back
! (when dirty) and invalidated through the cache's address array, then
! CCR is written with both invalidate bits added.  Changing the index
! mode (CCR.OIX, IIX) or switching a cache off with dirty lines in it
! would otherwise write lines back to the wrong places, or not at all.
!
! The SH-4 manuals want CCR changed by code that runs uncached, so this
! continues at the P2 address of its own code; interrupts are masked
! meanwhile.  The sweep (zero into 1024 entries of 32 bytes from
! 0xF4000000: a non-associative write that leaves a line invalid
! writes it back first) covers the SH7750R's 32 KiB two-way operand
! cache and the SH-4A's 32 KiB four-way one alike.

        .text
        .align  2
        .global cache_setup
cache_setup:
        mov.l   .Lbody, r1
        mov.l   .Lphys, r0
        and     r0, r1
        mov.l   .Lp2, r0
        or      r0, r1
        jmp     @r1             ! to .body, uncached; rts goes back as called
        nop

        .align  2
.body:
        stc     sr, r7
        mov     r7, r0
        or      #0xf0, r0       ! SR.IMASK = 15
        ldc     r0, sr
        mov.l   .Loc_array, r1
        mov.w   .Lentries, r2
        mov     #0, r3
1:      mov.l   r3, @r1
        dt      r2
        bf/s    1b
        add     #32, r1
        mov.l   .Lccr, r1
        mov.l   .Linvalidate, r0
        or      r4, r0
        mov.l   r0, @r1
        ! Eight instructions before anything may be fetched through the
        ! new setting (the manual's rule for a change of CCR).
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        ldc     r7, sr
        rts
        nop

        .align  2
.Lentries:
        .word   1024
        .align  2
.Lbody:
        .long   .body
.Lphys:
        .long   0x1fffffff
.Lp2:
        .long   0xa0000000
.Loc_array:
        .long   0xf4000000
.Lccr:
        .long   0xff00001c
.Linvalidate:
        .long   0x00000808      ! CCR.ICI | CCR.OCI

        .section .note.GNU-stack,"",%progbits
