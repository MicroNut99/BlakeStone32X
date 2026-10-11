! BlakeStone32X - hand-written SH2 inner loops (run from SDRAM)
! Shade table pointers point at the middle of a table rotated by 128,
! so a sign-extended texel byte (mov.b) indexes it directly.

        .section .sdata
        .align  4

!-----------------------------------------------------------------------
! void R_SpanPairsAsm(uint16_t *c_end, uint16_t *f_end, int pairs,
!                     uint32_t xy_last, uint32_t step_neg,
!                     const uint8_t *pp, const uint8_t *sh);
! Ceiling and floor rows, two pixels per 16-bit write, right to left.
! xy = Blake packed texture coord: y frac in bits 31..16, x frac in 15..0
! texel index = ((xy >> 3) & 0x1F80) | ((xy >> 25) & 0x7E), pp[i] ceiling, pp[i+1] floor
! r4 c_end, r5 f_end, r6 pairs (>0), r7 xy, stack: step, pp, sh
!-----------------------------------------------------------------------
        .global _R_SpanPairsAsm
_R_SpanPairsAsm:
        mov.l   r8,@-r15
        mov.l   r9,@-r15
        mov.l   r10,@-r15
        mov.l   r11,@-r15
        mov.l   r12,@-r15
        mov.l   r13,@-r15
        mov.l   r14,@-r15
        mov.l   @(28,r15),r8            /* step (already negated) */
        mov.l   @(32,r15),r10           /* pp: ceiling texels */
        mov.l   @(36,r15),r12           /* sh: shade table middle */
        mov     r10,r11
        add     #1,r11                  /* pp + 1: floor texels */
        mov.w   .Lmask1f80,r9
1:
        /* right pixel */
        mov     r7,r0
        shlr16  r0
        mov     r7,r1
        shlr8   r0
        shlr2   r1
        shlr    r0
        shlr    r1
        and     #126,r0
        and     r9,r1
        or      r1,r0
        add     r8,r7
        mov.b   @(r0,r10),r2            /* ceiling right */
        mov.b   @(r0,r11),r3            /* floor right */
        /* left pixel */
        mov     r7,r0
        shlr16  r0
        mov     r7,r1
        shlr8   r0
        shlr2   r1
        shlr    r0
        shlr    r1
        and     #126,r0
        and     r9,r1
        or      r1,r0
        add     r8,r7
        mov.b   @(r0,r10),r13           /* ceiling left */
        mov.b   @(r0,r11),r14           /* floor left */
        /* shade */
        mov     r2,r0
        mov.b   @(r0,r12),r2
        mov     r13,r0
        mov.b   @(r0,r12),r13
        mov     r3,r0
        mov.b   @(r0,r12),r3
        mov     r14,r0
        mov.b   @(r0,r12),r14
        extu.b  r2,r2
        shll8   r13
        or      r13,r2
        mov.w   r2,@-r4
        extu.b  r3,r3
        shll8   r14
        or      r14,r3
        dt      r6
        bf/s    1b
        mov.w   r3,@-r5

        mov.l   @r15+,r14
        mov.l   @r15+,r13
        mov.l   @r15+,r12
        mov.l   @r15+,r11
        mov.l   @r15+,r10
        mov.l   @r15+,r9
        rts
        mov.l   @r15+,r8

        .align  1
.Lmask1f80:
        .word   0x1F80

!-----------------------------------------------------------------------
! void R_ColAsm(uint8_t *dst, int count, int32_t frac, int32_t step,
!               const uint8_t *src, const uint8_t *sh, int pitch);
! One wall column, top to bottom, two pixels per loop.
! r4 dst, r5 count (>0), r6 frac (16.16 texel), r7 step, stack: src, sh, pitch
!-----------------------------------------------------------------------
        .align  4
        .global _R_ColAsm
_R_ColAsm:
        mov.l   r8,@-r15
        mov.l   r9,@-r15
        mov.l   @(8,r15),r1             /* src: 64 texels of the column */
        mov.l   @(12,r15),r2            /* sh */
        mov.l   @(16,r15),r3            /* pitch */
        mov     r5,r0
        tst     #1,r0
        bt      2f
        /* odd count: one pixel first */
        mov     r6,r0
        add     r7,r6
        shlr16  r0
        and     #63,r0
        mov.b   @(r0,r1),r0
        mov.b   @(r0,r2),r0
        mov.b   r0,@r4
        add     r3,r4
2:
        shlr    r5
        tst     r5,r5
        bt      9f
1:
        mov     r6,r0
        add     r7,r6
        shlr16  r0
        and     #63,r0
        mov.b   @(r0,r1),r8
        mov     r6,r0
        add     r7,r6
        shlr16  r0
        and     #63,r0
        mov.b   @(r0,r1),r9
        mov     r8,r0
        mov.b   @(r0,r2),r8
        mov     r9,r0
        mov.b   @(r0,r2),r9
        mov.b   r8,@r4
        add     r3,r4
        mov.b   r9,@r4
        dt      r5
        bf/s    1b
        add     r3,r4
9:
        mov.l   @r15+,r9
        rts
        mov.l   @r15+,r8

!-----------------------------------------------------------------------
! void R_SpanLoAsm(uint16_t *c_end, uint16_t *f_end, int count,
!                  uint32_t xy_last, uint32_t step_neg,
!                  const uint8_t *pp, const uint16_t *sh16);
! One texel per 16-bit write (two pixels), ceiling and floor, right to left.
! sh16 points at the middle of a rotated table of colour*0x0101 words.
!-----------------------------------------------------------------------
        .align  4
        .global _R_SpanLoAsm
_R_SpanLoAsm:
        mov.l   r8,@-r15
        mov.l   r9,@-r15
        mov.l   r10,@-r15
        mov.l   r11,@-r15
        mov.l   r12,@-r15
        mov.l   @(20,r15),r8            /* step (negated) */
        mov.l   @(24,r15),r10           /* pp */
        mov.l   @(28,r15),r12           /* sh16 */
        mov     r10,r11
        add     #1,r11
        mov.w   .Lmask1f80b,r9
1:
        mov     r7,r0
        shlr16  r0
        mov     r7,r1
        shlr8   r0
        shlr2   r1
        shlr    r0
        shlr    r1
        and     #126,r0
        and     r9,r1
        or      r1,r0
        add     r8,r7
        mov.b   @(r0,r10),r2            /* ceiling texel */
        mov.b   @(r0,r11),r3            /* floor texel */
        mov     r2,r0
        add     r0,r0
        mov.w   @(r0,r12),r2
        mov     r3,r0
        add     r0,r0
        mov.w   @(r0,r12),r3
        mov.w   r2,@-r4
        dt      r6
        bf/s    1b
        mov.w   r3,@-r5

        mov.l   @r15+,r12
        mov.l   @r15+,r11
        mov.l   @r15+,r10
        mov.l   @r15+,r9
        rts
        mov.l   @r15+,r8

        .align  1
.Lmask1f80b:
        .word   0x1F80

!-----------------------------------------------------------------------
! void R_ColAsm16(uint16_t *dst, int count, int32_t frac, int32_t step,
!                 const uint8_t *src, const uint16_t *sh16, int pitch);
! Low-detail wall column: one 16-bit write (two pixels) per row.
!-----------------------------------------------------------------------
        .align  4
        .global _R_ColAsm16
_R_ColAsm16:
        mov.l   r8,@-r15
        mov.l   @(4,r15),r1             /* src */
        mov.l   @(8,r15),r2             /* sh16 */
        mov.l   @(12,r15),r3            /* pitch */
1:
        mov     r6,r0
        add     r7,r6
        shlr16  r0
        and     #63,r0
        mov.b   @(r0,r1),r0
        add     r0,r0
        mov.w   @(r0,r2),r8
        mov.w   r8,@r4
        dt      r5
        bf/s    1b
        add     r3,r4
        rts
        mov.l   @r15+,r8
