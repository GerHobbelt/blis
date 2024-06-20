/*

   BLIS
   An object-based framework for developing high-performance BLAS-like
   libraries.

   Copyright (C) 2024, Advanced Micro Devices, Inc. All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:
    - Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    - Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    - Neither the name(s) of the copyright holder(s) nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE UNIVERSITY
   OF TEXAS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
   OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "blis.h"
#include "bli_x86_asm_macros.h"
// BLIS_ASM_SYNTAX_INTEL syntax is followed

/*
 * Enable code to handle BETA = 0 and BETA = 1
 * Enabling this is causing regression when BETA is not equal
 * 0 or 1, no improvement is observed when BETA = o or 1.
 * Enabled this code for compliance reasons.
 */
#define BETA_OPTIMIZATION
#define ENABLE_COL_GEN_STORE


/*
 * Prefetch distance for C
 * TAIL_NITER = 26 is working better for single thread
 * TAIL_NITER = 20 is working better for 128 threads
 * TAIL_NITER = 24 used which gives good performance for 1 thread
 * as well as 128 threads
 *
 * Prefetch C distance = TAIL_NITER + MR  (24+8 = 32)
 */
#define TAIL_NITER 24

/*
 * A_ADDITION is the negative offset added to address of A matrix
 * so that the range of offsets for all references of A can be minimized
 * in order to reduce the encoded instruction size.
 * Max offset for A matrix will be :=
 *      (MR*(UNROLL_FACTOR-1+ (MR+ number of A preloads))*sizeof(double) = 264 (used when
 *       SUBITER_1(3) macro is expanded ).
 * Using A_ADDITION = 132 should reduce the instructions size
 * the most, but A_ADDITION = 512 is giving better performance
 *
 * Similarly for B_ADDITION, max offset will be (24*3+16)*8 + 24*8*2
 * = 1088, therefore using B_ADDITION = 544 should reduce instruction
 * size the most, but B_ADDITION = 1024 is giving better performance.
 */
#define A_ADDITION (512)
#define B_ADDITION (1024)

#define LOOP_ALIGN ALIGN32

/*
 * A_L1_PREFETCH_DIST specifies the numbers of
 * K iterations ahead we have to prefetch current micro panel
 * of A matrix.
 * If current A is A(:k), prefetch A will be A(:k+4)
 *
 * A_L1_PREFETCH_DIST = 4 is giving the best performance
 *                      for single thread or when K is small.
 * A_L1_PREFETCH_DIST = 8 is giving the best performance
 *                      for 128 threads large square sizes.
 * Enabling prefetch A causes regression for large square
 * size in multi thread and improves performance in single thread
 * or when K is small.
 *
 */
#define A_L1_PREFETCH_DIST 4


#define PREFETCH_A_L1(n) \
    PREFETCH(0, MEM(RAX, A_L1_PREFETCH_DIST*8*8 +  n*(8*8) - A_ADDITION)) //1 0
    // RAX + (A_L1_PREFETCH_DIST * MR * sizeof(double)) + (n*MR*sizeof*(double))


/*
 * Prefetch next panel of B matrix.
 * Improvement can be observed in ST or when K is small.
 * Causing small regression in case of 128 threads square sizes.
 */
#define PREFETCH_B_NXT() \
    PREFETCH(2, MEM(RDX)) LEA(RDX, MEM(RDX, 8*8))

/*
 * Two different subiters(SUBITER_0 and SUBITER_1) are used
 * so that latency of mov can be hidden
 * SUBITER_0 laods B into ZMM0-2
 * SUBITER_0 laods B into ZMM3-5
 * SUBITER_0 and SUBITER_1 called alternatively
 *
 * ----------------------------------------------------------------
 * SUBITER_0
 * computes 8x24 block of C for one iteration of k loop
 * parameters: n k index A(i,k) * B(k,j)
 * Registers: rbx     matrix b pointer
 *            rax     matrix a pointer
 *            zmm6, zmm7 broadcast registers for a
 *            zmm0-zmm2 - 24 elements of "b"
 *            zmm8-zmm31 - stores a*b product
 * --------------------------------------------------------------
*/
#define SUBITER_0(n) \
\
    VFMADD231PD(ZMM( 8), ZMM(0), ZMM(6))                       /*b(0 : 7, n) * a(n, 0) */\
    VFMADD231PD(ZMM( 9), ZMM(1), ZMM(6))                       /*b(8 :15, n) * a(n, 0) */ \
    VFMADD231PD(ZMM(10), ZMM(2), ZMM(6))                       /*b(16:23, n) * a(n, 0) */ \
    \
    VBROADCASTSD(ZMM(6), MEM(RAX,(8*n+ 2)*8 - A_ADDITION))     /*zmm6 = a(n, 2)*/  \
    PREFETCH_A_L1(n) \
    VFMADD231PD(ZMM(11), ZMM(0), ZMM(7))                       /*b(0 : 7, n) * a(n, 1) */\
    VFMADD231PD(ZMM(12), ZMM(1), ZMM(7))                       /*b(8 :15, n) * a(n, 1) */ \
    VFMADD231PD(ZMM(13), ZMM(2), ZMM(7))                       /*b(16:23, n) * a(n, 1) */ \
    \
    VBROADCASTSD(ZMM(7), MEM(RAX,(8*n+ 3)*8 - A_ADDITION))     /*zmm7 = a(n, 3)*/ \
    VFMADD231PD(ZMM(14), ZMM(0), ZMM(6))                       /*b(0 : 7, n) * a(n, 2) */\
    VFMADD231PD(ZMM(15), ZMM(1), ZMM(6))                       /*b(8 :15, n) * a(n, 2) */ \
    VFMADD231PD(ZMM(16), ZMM(2), ZMM(6))                       /*b(16:23, n) * a(n, 2) */ \
    \
    VBROADCASTSD(ZMM(6), MEM(RAX,(8*n+ 4)*8 - A_ADDITION))     /*zmm6 = a(n, 4)*/ \
    VFMADD231PD(ZMM(17), ZMM(0), ZMM(7))                       /*b(0 : 7, n) * a(n, 3) */\
    VFMADD231PD(ZMM(18), ZMM(1), ZMM(7))                       /*b(8 :15, n) * a(n, 3) */ \
    VFMADD231PD(ZMM(19), ZMM(2), ZMM(7))                       /*b(16:23, n) * a(n, 3) */ \
    \
    VBROADCASTSD(ZMM(7), MEM(RAX,(8*n+ 5)*8 - A_ADDITION))     /*zmm7 = a(n, 5)*/ \
    VFMADD231PD(ZMM(20), ZMM(0), ZMM(6))                       /*b(0 : 7, n) * a(n, 4) */\
    VFMADD231PD(ZMM(21), ZMM(1), ZMM(6))                       /*b(8 :15, n) * a(n, 4) */ \
    VFMADD231PD(ZMM(22), ZMM(2), ZMM(6))                       /*b(16:23, n) * a(n, 4) */ \
    \
    VBROADCASTSD(ZMM(6), MEM(RAX,(8*n+ 6)*8 - A_ADDITION))     /*zmm6 = a(n, 6)*/ \
    VFMADD231PD(ZMM(23), ZMM(0), ZMM(7))                       /*b(0 : 7, n) * a(n, 5) */\
    VFMADD231PD(ZMM(24), ZMM(1), ZMM(7))                       /*b(8 :15, n) * a(n, 5) */ \
    VFMADD231PD(ZMM(25), ZMM(2), ZMM(7))                       /*b(16:23, n) * a(n, 5) */ \
    \
    VBROADCASTSD(ZMM(7), MEM(RAX,(8*n+ 7)*8 - A_ADDITION))     /*zmm7 = a(n, 7)*/ \
    VFMADD231PD(ZMM(26), ZMM(0), ZMM(6))                       /*b(0 : 7, n) * a(n, 6) */\
    VFMADD231PD(ZMM(27), ZMM(1), ZMM(6))                       /*b(8 :15, n) * a(n, 6) */ \
    VFMADD231PD(ZMM(28), ZMM(2), ZMM(6))                       /*b(16:23, n) * a(n, 6) */ \
    \
    VBROADCASTSD(ZMM(6), MEM(RAX,(8*n+ 8)*8 - A_ADDITION))     /*zmm6 = a(n+1, 0)*/\
    VFMADD231PD(ZMM(29), ZMM(0), ZMM(7))                       /*b(0 : 7, n) * a(n, 7) */\
    VFMADD231PD(ZMM(30), ZMM(1), ZMM(7))                       /*b(8 :15, n) * a(n, 7) */ \
    VFMADD231PD(ZMM(31), ZMM(2), ZMM(7))                       /*b(16:23, n) * a(n, 7) */ \
    VBROADCASTSD(ZMM(7), MEM(RAX,(8*n+ 9)*8 - A_ADDITION))     /*zmm7 = a(n+1, 1)*/ \
    VMOVAPD(ZMM(0), MEM(RBX,(24*n+0 )*8 - B_ADDITION + 24*8*2))/*zmm0 = b(0 :7 , n+2)*/ \
    VMOVAPD(ZMM(1), MEM(RBX,(24*n+8 )*8 - B_ADDITION + 24*8*2))/*zmm1 = b(8 :15, n+2)*/ \
    VMOVAPD(ZMM(2), MEM(RBX,(24*n+16)*8 - B_ADDITION + 24*8*2))/*zmm2 = b(16:23, n+2)*/ \
    /*24*8*2 is preload offset compensated for B preload*/ \
/*
 * ----------------------------------------------------------------
 * SUBITER_1
 * computes 8x24 block of C for one iteration of k loop
 * parameters: n k index A(i,k) * B(k,j)
 * Registers: rbx     matrix b pointer
 *            rax     matrix a pointer
 *            zmm6, zmm7 broadcast registers for a
 *            zmm3-zmm5 - 24 elements of "b"
 *            zmm8-zmm31 - stores a*b product
 * --------------------------------------------------------------
*/
#define SUBITER_1(n) \
\
    VFMADD231PD(ZMM( 8), ZMM(3), ZMM(6))                       /*b(0 : 7, n) * a(n, 0) */\
    VFMADD231PD(ZMM( 9), ZMM(4), ZMM(6))                       /*b(8 :15, n) * a(n, 0) */ \
    VFMADD231PD(ZMM(10), ZMM(5), ZMM(6))                       /*b(16:23, n) * a(n, 0) */ \
    \
    VBROADCASTSD(ZMM(6), MEM(RAX,(8*n+ 2)*8 - A_ADDITION))     /*zmm6 = a(n, 2)*/  \
    PREFETCH_A_L1(n) \
    VFMADD231PD(ZMM(11), ZMM(3), ZMM(7))                       /*b(0 : 7, n) * a(n, 1) */\
    VFMADD231PD(ZMM(12), ZMM(4), ZMM(7))                       /*b(8 :15, n) * a(n, 1) */ \
    VFMADD231PD(ZMM(13), ZMM(5), ZMM(7))                       /*b(16:23, n) * a(n, 1) */ \
    \
    VBROADCASTSD(ZMM(7), MEM(RAX,(8*n+ 3)*8 - A_ADDITION))     /*zmm7 = a(n, 3)*/ \
    VFMADD231PD(ZMM(14), ZMM(3), ZMM(6))                       /*b(0 : 7, n) * a(n, 2) */\
    VFMADD231PD(ZMM(15), ZMM(4), ZMM(6))                       /*b(8 :15, n) * a(n, 2) */ \
    VFMADD231PD(ZMM(16), ZMM(5), ZMM(6))                       /*b(16:23, n) * a(n, 2) */ \
    \
    VBROADCASTSD(ZMM(6), MEM(RAX,(8*n+ 4)*8 - A_ADDITION))     /*zmm6 = a(n, 4)*/ \
    VFMADD231PD(ZMM(17), ZMM(3), ZMM(7))                       /*b(0 : 7, n) * a(n, 3) */\
    VFMADD231PD(ZMM(18), ZMM(4), ZMM(7))                       /*b(8 :15, n) * a(n, 3) */ \
    VFMADD231PD(ZMM(19), ZMM(5), ZMM(7))                       /*b(16:23, n) * a(n, 3) */ \
    \
    VBROADCASTSD(ZMM(7), MEM(RAX,(8*n+ 5)*8 - A_ADDITION))     /*zmm7 = a(n, 5)*/ \
    VFMADD231PD(ZMM(20), ZMM(3), ZMM(6))                       /*b(0 : 7, n) * a(n, 4) */\
    VFMADD231PD(ZMM(21), ZMM(4), ZMM(6))                       /*b(8 :15, n) * a(n, 4) */ \
    VFMADD231PD(ZMM(22), ZMM(5), ZMM(6))                       /*b(16:23, n) * a(n, 4) */ \
    \
    VBROADCASTSD(ZMM(6), MEM(RAX,(8*n+ 6)*8 - A_ADDITION))     /*zmm6 = a(n, 6)*/ \
    VFMADD231PD(ZMM(23), ZMM(3), ZMM(7))                       /*b(0 : 7, n) * a(n, 5) */\
    VFMADD231PD(ZMM(24), ZMM(4), ZMM(7))                       /*b(8 :15, n) * a(n, 5) */ \
    VFMADD231PD(ZMM(25), ZMM(5), ZMM(7))                       /*b(16:23, n) * a(n, 5) */ \
    \
    VBROADCASTSD(ZMM(7), MEM(RAX,(8*n+ 7)*8 - A_ADDITION))     /*zmm7 = a(n, 7)*/ \
    VFMADD231PD(ZMM(26), ZMM(3), ZMM(6))                       /*b(0 : 7, n) * a(n, 6) */\
    VFMADD231PD(ZMM(27), ZMM(4), ZMM(6))                       /*b(8 :15, n) * a(n, 6) */ \
    VFMADD231PD(ZMM(28), ZMM(5), ZMM(6))                       /*b(16:23, n) * a(n, 6) */ \
    VBROADCASTSD(ZMM(6), MEM(RAX,(8*n+ 8)*8 - A_ADDITION))     /*zmm6 = a(n+1, 0)*/ \
    \
    VFMADD231PD(ZMM(29), ZMM(3), ZMM(7))                       /*b(0 : 7, n) * a(n, 7) */\
    VFMADD231PD(ZMM(30), ZMM(4), ZMM(7))                       /*b(8 :15, n) * a(n, 7) */ \
    VFMADD231PD(ZMM(31), ZMM(5), ZMM(7))                       /*b(16:23, n) * a(n, 7) */ \
    VBROADCASTSD(ZMM(7), MEM(RAX,(8*n+ 9)*8 - A_ADDITION))     /*zmm7 = a(n+1, 1)*/ \
    VMOVAPD(ZMM(3), MEM(RBX,(24*n+0 )*8 - B_ADDITION + 24*8*2))/*zmm3 = b(0 :7 , n+2)*/ \
    VMOVAPD(ZMM(4), MEM(RBX,(24*n+8 )*8 - B_ADDITION + 24*8*2))/*zmm4 = b(8 :15, n+2)*/ \
    VMOVAPD(ZMM(5), MEM(RBX,(24*n+16)*8 - B_ADDITION + 24*8*2))/*zmm5 = b(16:23, n+2)*/ \
    /*24*8*2 is preload offset compensated for B preload*/ \


// Update C when C is general stored
#define UPDATE_C_SCATTERED(R1,R2,R3) \
\
    KXNORW(K(1), K(0), K(0)) /*set mask register to zero*/ \
    KXNORW(K(2), K(0), K(0)) /*set mask register to zero*/ \
    KXNORW(K(3), K(0), K(0)) /*set mask register to zero*/ \
    VGATHERQPD(ZMM(0) MASK_K(1), MEM(RCX,ZMM(2),1)) /*load C(0:7) from current row of C*/\
    /*scale by beta*/ \
    VFMADD231PD(ZMM(R1), ZMM(0), ZMM(1)) /*zmmR1 += zmm0(C(0:7)*zmm1(beta)*/\
    VGATHERQPD(ZMM(0) MASK_K(2), MEM(RCX,ZMM(3),1)) /*load C(8:15)*/        \
    VFMADD231PD(ZMM(R2), ZMM(0), ZMM(1)) /*zmmR3 += zmm0(C(8:15)*zmm1(beta)*/\
    VGATHERQPD(ZMM(0) MASK_K(3), MEM(RCX,ZMM(4),1)) /*load C(16:23)*/       \
    VFMADD231PD(ZMM(R3), ZMM(0), ZMM(1)) /*zmmR3 += zmm0(C(16:23)*zmm1(beta)*/\
    /*mask registers are reset to 1 after gather/scatter instruction*/ \
    KXNORW(K(1), K(0), K(0)) /*set mask registers to zero*/\
    KXNORW(K(2), K(0), K(0)) \
    KXNORW(K(3), K(0), K(0)) \
    /*store c*/ \
    VSCATTERQPD(MEM(RCX,ZMM(2),1) MASK_K(1), ZMM(R1)) /*store C(0:7)*/   \
    VSCATTERQPD(MEM(RCX,ZMM(3),1) MASK_K(2), ZMM(R2)) /*store C(7:15)*/  \
    VSCATTERQPD(MEM(RCX,ZMM(4),1) MASK_K(3), ZMM(R3)) /*store C(16:23)*/ \
    LEA(RCX, MEM(RCX,R8,1))

// Update C when C is general stored and beta = 0
#define UPDATE_C_SCATTERED_BZ(R1,R2,R3) \
\
    KXNORW(K(1), K(0), K(0)) \
    KXNORW(K(2), K(0), K(0)) \
    KXNORW(K(3), K(0), K(0)) \
    VSCATTERQPD(MEM(RCX,ZMM(2),1) MASK_K(1), ZMM(R1)) \
    VSCATTERQPD(MEM(RCX,ZMM(3),1) MASK_K(2), ZMM(R2)) \
    VSCATTERQPD(MEM(RCX,ZMM(4),1) MASK_K(3), ZMM(R3))  \
    LEA(RCX, MEM(RCX,R8,1))

// 8x8 in register transpose, used for column stored C
#define TRANSPOSE_8X8(R0, R1, R2, R3, R4, R5, R6, R7) \
\
    VUNPCKLPD(ZMM(6), ZMM(R0), ZMM(R1)) \
    VUNPCKLPD(ZMM(7), ZMM(R2), ZMM(R3)) \
    VUNPCKLPD(ZMM(2), ZMM(R4), ZMM(R5)) \
    VUNPCKLPD(ZMM(3), ZMM(R6), ZMM(R7)) \
    VMOVUPD(ZMM(0), ZMM(R0)) \
    VMOVUPD(ZMM(1), ZMM(R4)) \
    /*Stage2*/ \
    VSHUFF64X2(ZMM(4), ZMM(6), ZMM(7), IMM(0x88)) \
    VSHUFF64X2(ZMM(5), ZMM(2), ZMM(3), IMM(0x88)) \
    /*Stage3  1,5*/ \
    VSHUFF64X2(ZMM(R0), ZMM(4), ZMM(5), IMM(0x88)) \
    VSHUFF64X2(ZMM(R4), ZMM(4), ZMM(5), IMM(0xDD)) \
    /*Stage2*/ \
    VSHUFF64X2(ZMM(4), ZMM(6), ZMM(7), IMM(0xDD)) \
    VSHUFF64X2(ZMM(5), ZMM(2), ZMM(3), IMM(0xDD)) \
    /*Stage3  3,7*/ \
    VUNPCKHPD(ZMM(6), ZMM(0 ), ZMM(R1)) \
    VUNPCKHPD(ZMM(7), ZMM(R2), ZMM(R3)) \
    VUNPCKHPD(ZMM(2), ZMM(1 ), ZMM(R5)) \
    VUNPCKHPD(ZMM(3), ZMM(R6), ZMM(R7)) \
    VSHUFF64X2(ZMM(R2), ZMM(4), ZMM(5), IMM(0x88)) \
    VSHUFF64X2(ZMM(R6), ZMM(4), ZMM(5), IMM(0xDD)) \
    \
    /*Stage2*/ \
    VSHUFF64X2(ZMM(4), ZMM(6), ZMM(7), IMM(0x88)) \
    VSHUFF64X2(ZMM(5), ZMM(2), ZMM(3), IMM(0x88)) \
    /*Stage3  2,6*/ \
    VSHUFF64X2(ZMM(R1), ZMM(4), ZMM(5), IMM(0x88)) \
    VSHUFF64X2(ZMM(R5), ZMM(4), ZMM(5), IMM(0xDD)) \
    /*Stage2*/ \
    VSHUFF64X2(ZMM(4), ZMM(6), ZMM(7), IMM(0xDD)) \
    VSHUFF64X2(ZMM(5), ZMM(2), ZMM(3), IMM(0xDD)) \
    /*Stage3  4,8*/ \
    VSHUFF64X2(ZMM(R3), ZMM(4), ZMM(5), IMM(0x88)) \
    VSHUFF64X2(ZMM(R7), ZMM(4), ZMM(5), IMM(0xDD)) \

// Update C when C is column stored
#define UPDATE_C_COL_STORE(R0, R1, R2, R3, R4, R5, R6, R7) \
    \
    /* scale by alpha */\
    VMULPD(ZMM(R0), ZMM(R0), ZMM(0)) \
    VMULPD(ZMM(R1), ZMM(R1), ZMM(0)) \
    VMULPD(ZMM(R2), ZMM(R2), ZMM(0)) \
    VMULPD(ZMM(R3), ZMM(R3), ZMM(0)) \
    VMULPD(ZMM(R4), ZMM(R4), ZMM(0)) \
    VMULPD(ZMM(R5), ZMM(R5), ZMM(0)) \
    VMULPD(ZMM(R6), ZMM(R6), ZMM(0)) \
    VMULPD(ZMM(R7), ZMM(R7), ZMM(0)) \
    /*scale by beta*/\
    VFMADD231PD(ZMM(R0), ZMM(1), MEM(RCX)) \
    /*store c*/ \
    VMOVUPD(MEM(RCX), ZMM(R0)) \
    VFMADD231PD(ZMM(R1), ZMM(1), MEM(RCX, R9, 1)) \
    VMOVUPD(MEM(RCX, R9, 1), ZMM(R1)) \
    VFMADD231PD(ZMM(R2), ZMM(1), MEM(RCX, R9, 2)) \
    VMOVUPD(MEM(RCX, R9, 2), ZMM(R2)) \
    VFMADD231PD(ZMM(R3), ZMM(1), MEM(RCX, RDI, 1)) \
    VMOVUPD(MEM(RCX, RDI, 1), ZMM(R3)) \
    VFMADD231PD(ZMM(R4), ZMM(1), MEM(RCX, R9, 4)) \
    VMOVUPD(MEM(RCX, R9, 4), ZMM(R4)) \
    VFMADD231PD(ZMM(R5), ZMM(1), MEM(RCX, RDX, 1)) \
    VMOVUPD(MEM(RCX, RDX, 1), ZMM(R5)) \
    VFMADD231PD(ZMM(R6), ZMM(1), MEM(RCX, RDI, 2)) \
    VMOVUPD(MEM(RCX, RDI, 2), ZMM(R6)) \
    VFMADD231PD(ZMM(R7), ZMM(1), MEM(RCX, RSI, 1)) \
    VMOVUPD(MEM(RCX, RSI, 1), ZMM(R7)) \
    LEA(RCX, MEM(RCX,R9,8))

// Update C when C is column stored and beta = 0
#define UPDATE_C_COL_STORE_BZ(R0, R1, R2, R3, R4, R5, R6, R7) \
    /* scale by alpha */\
    VMULPD(ZMM(R0), ZMM(R0), ZMM(0)) \
    VMULPD(ZMM(R1), ZMM(R1), ZMM(0)) \
    VMULPD(ZMM(R2), ZMM(R2), ZMM(0)) \
    VMULPD(ZMM(R3), ZMM(R3), ZMM(0)) \
    VMULPD(ZMM(R4), ZMM(R4), ZMM(0)) \
    VMULPD(ZMM(R5), ZMM(R5), ZMM(0)) \
    VMULPD(ZMM(R6), ZMM(R6), ZMM(0)) \
    VMULPD(ZMM(R7), ZMM(R7), ZMM(0)) \
    /*store c*/ \
    VMOVUPD(MEM(RCX), ZMM(R0)) \
    VMOVUPD(MEM(RCX, R9, 1), ZMM(R1)) /*R9 = cs_c*/ \
    VMOVUPD(MEM(RCX, R9, 2), ZMM(R2)) \
    VMOVUPD(MEM(RCX, RDI, 1), ZMM(R3)) /*RDI = 3*cs_c*/\
    VMOVUPD(MEM(RCX, R9, 4), ZMM(R4)) \
    VMOVUPD(MEM(RCX, RDX, 1), ZMM(R5)) /*RDX = 5*cs_c*/\
    VMOVUPD(MEM(RCX, RDI, 2), ZMM(R6)) \
    VMOVUPD(MEM(RCX, RSI, 1), ZMM(R7)) /*RSI = 7*cs_c*/\
    LEA(RCX, MEM(RCX,R9,8))

//This is an array used for the scatter/gather instructions.
static int64_t offsets[24] __attribute__((aligned(64))) =
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23};


/*
 * number of accumulation registers = 24/8 * 8 = 24     zmm8 to zmm31
 * number of registers used for load B =
 *       24/8 = 3 (*2 for hiding load latency) zmm0 to zmm5
 * number of registers used for broadcast A = 2         zmm6 and zmm7
 */
void bli_dgemm_avx512_asm_8x24(
                              dim_t            k_,
                              double* restrict alpha,
                              double* restrict a,
                              double* restrict b,
                              double* restrict beta,
                              double* restrict c, inc_t rs_c_, inc_t cs_c_,
                              auxinfo_t*       data,
                              cntx_t* restrict cntx
                            )
{
    (void)data;
    (void)cntx;
    (void)cs_c_;

    double* b_next = bli_auxinfo_next_b( data );
    const int64_t* offsetPtr = &offsets[0];
    const int64_t k = k_;
    const int64_t rs_c = rs_c_*8; //convert strides to bytes
    const int64_t cs_c = cs_c_*8; //convert strides to bytes


    BEGIN_ASM()

    VXORPD(ZMM(8) , ZMM(8),  ZMM(8)) // clear out registers
    VXORPD(ZMM(9) , ZMM(9),  ZMM(9))
    VXORPD(ZMM(10), ZMM(10), ZMM(10))
    VXORPD(ZMM(11), ZMM(11), ZMM(11))
    VXORPD(ZMM(12), ZMM(12), ZMM(12))
    VXORPD(ZMM(13), ZMM(13), ZMM(13))
    VXORPD(ZMM(14), ZMM(14), ZMM(14))
    VXORPD(ZMM(15), ZMM(15), ZMM(15))
    VXORPD(ZMM(16), ZMM(16), ZMM(16))
    VXORPD(ZMM(17), ZMM(17), ZMM(17))
    VXORPD(ZMM(18), ZMM(18), ZMM(18))
    VXORPD(ZMM(19), ZMM(19), ZMM(19))
    VXORPD(ZMM(20), ZMM(20), ZMM(20))
    VXORPD(ZMM(21), ZMM(21), ZMM(21))
    VXORPD(ZMM(22), ZMM(22), ZMM(22))
    VXORPD(ZMM(23), ZMM(23), ZMM(23))
    VXORPD(ZMM(24), ZMM(24), ZMM(24))
    VXORPD(ZMM(25), ZMM(25), ZMM(25))
    VXORPD(ZMM(26), ZMM(26), ZMM(26))
    VXORPD(ZMM(27), ZMM(27), ZMM(27))
    VXORPD(ZMM(28), ZMM(28), ZMM(28))
    VXORPD(ZMM(29), ZMM(29), ZMM(29))
    VXORPD(ZMM(30), ZMM(30), ZMM(30))
    VXORPD(ZMM(31), ZMM(31), ZMM(31))

    MOV(RSI, VAR(k))   // loop index
    MOV(RAX, VAR(a))   // load address of a
    MOV(RBX, VAR(b))   // load address of b
    MOV(RCX, VAR(c))   // load address of c
    MOV(R8, VAR(rs_c)) // load rs_c
    MOV(RDX, VAR(b_next)) // load next panel of b for prefetch

    LEA(R9, MEM(RCX,63)) // c for prefetching R9 := C + cacheline_offset

    // pre-load two rows of B
    VMOVAPD(ZMM(0), MEM(RBX, 0*8)) //zmm0 = b[0:7]
    VMOVAPD(ZMM(1), MEM(RBX, 8*8)) //zmm1 = b[8:15]
    VMOVAPD(ZMM(2), MEM(RBX,16*8)) //zmm2 = b[16:23]

    VMOVAPD(ZMM(3), MEM(RBX,24*8)) //zmm3 = b[24:31]
    VMOVAPD(ZMM(4), MEM(RBX,32*8)) //zmm4 = b[32:39]
    VMOVAPD(ZMM(5), MEM(RBX,40*8)) //zmm5 = b[40:48]

    // pre-load A
    VBROADCASTSD(ZMM(6), MEM(RAX,(8*0+0)*8)) // zmm6 = a[0]
    VBROADCASTSD(ZMM(7), MEM(RAX,(8*0+1)*8)) // zmm7 = a[1]

    // move address of A and B forward so that negative addresses
    // can be used
    ADD(RBX, IMM( 0+B_ADDITION )) // A += A_ADDITION
    ADD(RAX, IMM( 0+A_ADDITION )) // B += B_ADDITION



    MOV(RDI, RSI)    // RDI = k
    AND(RSI, IMM(3)) // RSI(k_left) = k & 3, RSI = k % 4
    SAR(RDI, IMM(2)) // RDI(k_iter) = k >> 2, RDI = k / 4

    SUB(RDI, IMM(8+TAIL_NITER)) // k/4 - 8 - TAIL_NITER
    JLE(K_PREFETCH)   // jump to C prefetch loop if k_iter <= 0
    // LABEL(K_MAIN)

        LOOP_ALIGN
        LABEL(LOOP1)

            SUBITER_0(0)     // k=0
            SUBITER_1(1)     // k=1
            SUB(RDI, IMM(1)) // k_iter-=1
            SUBITER_0(2)     // k=2
            PREFETCH_B_NXT()
            SUBITER_1(3)     // k=3

            LEA(RAX, MEM(RAX,4*8*8))  // rax -> (UNROLL_FACTOR * MR * sizeof(double)) next 4th col of a
            LEA(RBX, MEM(RBX,4*24*8)) // rbx -> (UNROLL_FACTOR * NR * sizeof(double)) next 4th row of b

        JNZ(LOOP1) // if RDI != 0 jump to loop1

    LABEL(K_PREFETCH)

    ADD(RDI, IMM(8)) // add prefetch loop count ( RDI(k_iter) += MR )
    JLE(K_TAIL) // jump to tail iteration if k_iter <= 0

        LOOP_ALIGN
        // MR * 24 block of c is prefetched
        LABEL(LOOP2)

            PREFETCHW0(MEM(R9))      // prefetch C(k, 0:7)
            SUBITER_0(0)             // k=0
            PREFETCHW0(MEM(R9,8*8))  // prefetch C(k, 8:15)
            SUBITER_1(1)             // k=1
            SUB(RDI, IMM(1))         // rdi-=1
            PREFETCHW0(MEM(R9,16*8)) // prefetch C(k, 16:23)
            SUBITER_0(2)             // k=2
            PREFETCH_B_NXT()
            SUBITER_1(3)             // k=3

            LEA(RAX, MEM(RAX,4*8*8))  // rax -> (UNROLL_FACTOR * MR * sizeof(double)) next 4th col of a
            LEA(RBX, MEM(RBX,4*24*8)) // rbx -> (UNROLL_FACTOR * NR * sizeof(double)) next 4th row of b
            LEA(R9, MEM(R9,R8,1))     // r9  -> c += ldc (next row of c)

        JNZ(LOOP2) // if RDI != 0 jump to loop2

    LABEL(K_TAIL)

    ADD(RDI, IMM(0+TAIL_NITER)) // RDI(k_iter) += TAIL_ITER
    JLE(POST_K) // jump to TAIL loop if k_iter <= 0

        LOOP_ALIGN
        LABEL(LOOP3)

            SUBITER_0(0)       //k=0
            SUBITER_1(1)       //k=1
            SUB(RDI, IMM(1))   //rdi-=1
            SUBITER_0(2)       //k=2
            PREFETCH_B_NXT()
            SUBITER_1(3)       //k=3

            LEA(RAX, MEM(RAX,4*8*8))  // rax -> next 4th col of a
            LEA(RBX, MEM(RBX,4*24*8)) // rbx -> next 4th row of b

        JNZ(LOOP3) // if RDI != 0 jump to LOOP3

    LABEL(POST_K)

    TEST(RSI, RSI)
    JZ(POSTACCUM)
        // Only SUBITER_0 is used in this loop,
        // therefore negative offset is done for 1 iter
        // of K only(24*8)
        SUB(RBX, IMM(24*8)) // rbx -> prev 4th row of b
        LOOP_ALIGN
        LABEL(LOOP4)

            SUB(RSI, IMM(1)) //rsi-=1
            SUBITER_0(0)     //k=0

            LEA(RAX, MEM(RAX,8*8))  // rax -> (UNROLL_FACTOR(1) * MR * sizeof(double)) next col of a
            LEA(RBX, MEM(RBX,24*8)) // rbx -> (UNROLL_FACTOR(1) * NR * sizeof(double)) next row of b

        JNZ(LOOP4)

    LABEL(POSTACCUM)

    MOV(RAX, VAR(alpha))
    MOV(RBX, VAR(beta))
    VBROADCASTSD(ZMM(0), MEM(RAX)) // broadcast alpha into zmm0

    // r8 = rs_c
    LEA(RDI, MEM(R8,  R8, 2)) // (RDI)rs_c*3 -> rs_c + rs_c*2
    LEA(RDX, MEM(R8,  R8, 4)) // (RDX)rs_c*5 -> rs_c + rs_c*4
    LEA(RSI, MEM(R8, RDI, 2)) // (RSI)rs_c*7 -> rs_c + rs_c*3*2

#ifdef ENABLE_COL_GEN_STORE
    VXORPD(ZMM(2), ZMM(2), ZMM(2))
    MOV(R9, VAR(cs_c)) // load cs_c
    CMP(R8, IMM(8))
    JE(COLUPDATE) // jump to COLUPDATE if rs_c(r8) == 1

    CMP(R9, IMM(8)) // r9 = cs_c
    JNE(SCATTERUPDATE) // if cs_c(r9) != 1 jump to scatterupdate
#endif

#ifdef BETA_OPTIMIZATION // if beta = 0 and beta = 1 are handled separately
    CMP(RBX, IMM(0))
    JZ(BETA_ZERO) // jump to BETA_ZERO if beta == 0
    CMP(RBX, IMM(1))
    JNZ(BETA_NZ_N1)// jump to BETA_NZ_N1 if beta != 1

    // no jumps for beta = 1
        // LABEL(BETA_ONE)

            // row0
            // scale by alpha, zmm0 = alpha
            VMULPD(ZMM( 8), ZMM( 8), ZMM(0)) // zmm8 *= alpha
            VMULPD(ZMM( 9), ZMM( 9), ZMM(0)) // zmm9 *= alpha
            VMULPD(ZMM(10), ZMM(10), ZMM(0)) // zmm10*= alpha
            /*since beta == 1, C += alpha(AB)*/
            VADDPD(ZMM( 8), ZMM( 8), MEM(RCX))     // zmm8 = C(0 :7 ) + zmm8 *alpha
            VADDPD(ZMM( 9), ZMM( 9), MEM(RCX,64))  // zmm9 = C(8 :15) + zmm9 *alpha
            VADDPD(ZMM(10), ZMM(10), MEM(RCX,128)) // zmm10= C(16:23) + zmm10*alpha
            /*store c*/
            VMOVUPD(MEM(RCX    ), ZMM( 8)) // C(0 :7 ) = zmm8
            VMOVUPD(MEM(RCX, 64), ZMM( 9)) // C(8 :15) = zmm9
            VMOVUPD(MEM(RCX,128), ZMM(10)) // C(16:23) = zmm10

            // row1
            VMULPD(ZMM(11), ZMM(11), ZMM(0)) // zmm11 *= alpha
            VMULPD(ZMM(12), ZMM(12), ZMM(0)) // zmm12 *= alpha
            VMULPD(ZMM(13), ZMM(13), ZMM(0)) // zmm13 *= alpha
            /*scale by beta*/
            VADDPD(ZMM(11), ZMM(11), MEM(RCX, R8, 1     )) // zmm11= C(0 :7 ) + zmm11*alpha
            VADDPD(ZMM(12), ZMM(12), MEM(RCX, R8, 1, 64 )) // zmm12= C(8 :15) + zmm12*alpha
            VADDPD(ZMM(13), ZMM(13), MEM(RCX, R8, 1, 128)) // zmm13= C(16:23) + zmm13*alpha
            /*store c*/
            VMOVUPD(MEM(RCX, R8, 1     ), ZMM(11))  // C(0 :7 ) = zmm11
            VMOVUPD(MEM(RCX, R8, 1, 64 ), ZMM(12))  // C(8 :15) = zmm12
            VMOVUPD(MEM(RCX, R8, 1, 128), ZMM(13))  // C(16:23) = zmm13

            // row2
            VMULPD(ZMM(14), ZMM(14), ZMM(0)) // zmm14 *= alpha
            VMULPD(ZMM(15), ZMM(15), ZMM(0)) // zmm15 *= alpha
            VMULPD(ZMM(16), ZMM(16), ZMM(0)) // zmm16 *= alpha
            /*scale by beta*/
            VADDPD(ZMM(14), ZMM(14), MEM(RCX, R8, 2     )) // zmm14 = C(0 :7 ) + zmm14 *alpha
            VADDPD(ZMM(15), ZMM(15), MEM(RCX, R8, 2, 64 )) // zmm15 = C(8 :15) + zmm15  *alpha
            VADDPD(ZMM(16), ZMM(16), MEM(RCX, R8, 2, 128)) // zmm16 = C(16:23) + zmm16  *alpha
            /*store c*/
            VMOVUPD(MEM(RCX, R8, 2     ), ZMM(14)) // C(0 :7 ) = zmm14
            VMOVUPD(MEM(RCX, R8, 2, 64 ), ZMM(15)) // C(8 :15) = zmm15
            VMOVUPD(MEM(RCX, R8, 2, 128), ZMM(16)) // C(16:23) = zmm16

            // row3
            VMULPD(ZMM(17), ZMM(17), ZMM(0)) // zmm17 *= alpha
            VMULPD(ZMM(18), ZMM(18), ZMM(0)) // zmm18 *= alpha
            VMULPD(ZMM(19), ZMM(19), ZMM(0)) // zmm19 *= alpha
            /*scale by beta*/
            VADDPD(ZMM(17), ZMM(17), MEM(RCX, RDI, 1     )) // zmm17 = C(0 :7 ) + zmm17 *alpha
            VADDPD(ZMM(18), ZMM(18), MEM(RCX, RDI, 1, 64 )) // zmm18 = C(8 :15) + zmm18 *alpha
            VADDPD(ZMM(19), ZMM(19), MEM(RCX, RDI, 1, 128)) // zmm18 = C(16:23) + zmm18 *alpha
            /*store c*/
            VMOVUPD(MEM(RCX, RDI, 1     ), ZMM(17)) // C(0 :7 ) = zmm17
            VMOVUPD(MEM(RCX, RDI, 1, 64 ), ZMM(18)) // C(8 :15) = zmm18
            VMOVUPD(MEM(RCX, RDI, 1, 128), ZMM(19)) // C(16:23) = zmm18

            // row4
            VMULPD(ZMM(20), ZMM(20), ZMM(0)) // zmm20 *= alpha
            VMULPD(ZMM(21), ZMM(21), ZMM(0)) // zmm21 *= alpha
            VMULPD(ZMM(22), ZMM(22), ZMM(0)) // zmm22 *= alpha
            /*scale by beta*/
            VADDPD(ZMM(20), ZMM(20), MEM(RCX, R8, 4     )) // zmm20 = C(0 :7 ) + zmm20 *alpha
            VADDPD(ZMM(21), ZMM(21), MEM(RCX, R8, 4, 64 )) // zmm21 = C(8 :15) + zmm21 *alpha
            VADDPD(ZMM(22), ZMM(22), MEM(RCX, R8, 4, 128)) // zmm22 = C(16:23) + zmm22 *alpha
            /*store c*/
            VMOVUPD(MEM(RCX, R8, 4     ), ZMM(20)) // C(0 :7 ) = zmm20
            VMOVUPD(MEM(RCX, R8, 4, 64 ), ZMM(21)) // C(8 :15) = zmm21
            VMOVUPD(MEM(RCX, R8, 4, 128), ZMM(22)) // C(16:23) = zmm22

            // row5
            VMULPD(ZMM(23), ZMM(23), ZMM(0)) // zmm23 *= alpha
            VMULPD(ZMM(24), ZMM(24), ZMM(0)) // zmm24 *= alpha
            VMULPD(ZMM(25), ZMM(25), ZMM(0)) // zmm25 *= alpha
            /*scale by beta*/
            VADDPD(ZMM(23), ZMM(23), MEM(RCX, RDX, 1     )) // zmm23 = C(0 :7 ) + zmm23 *alpha
            VADDPD(ZMM(24), ZMM(24), MEM(RCX, RDX, 1, 64 )) // zmm24 = C(8 :15) + zmm24 *alpha
            VADDPD(ZMM(25), ZMM(25), MEM(RCX, RDX, 1, 128)) // zmm25 = C(16:23) + zmm25 *alpha
            /*store c*/
            VMOVUPD(MEM(RCX, RDX, 1     ), ZMM(23)) // C(0 :7 ) = zmm23
            VMOVUPD(MEM(RCX, RDX, 1, 64 ), ZMM(24)) // C(8 :15) = zmm24
            VMOVUPD(MEM(RCX, RDX, 1, 128), ZMM(25)) // C(16:23) = zmm25

            // row6
            VMULPD(ZMM(26), ZMM(26), ZMM(0)) // zmm26 *= alpha
            VMULPD(ZMM(27), ZMM(27), ZMM(0)) // zmm27 *= alpha
            VMULPD(ZMM(28), ZMM(28), ZMM(0)) // zmm28 *= alpha
            /*scale by beta*/
            VADDPD(ZMM(26), ZMM(26), MEM(RCX, RDI, 2     )) // zmm26 = C(0 :7 ) + zmm26 *alpha
            VADDPD(ZMM(27), ZMM(27), MEM(RCX, RDI, 2, 64 )) // zmm27 = C(8 :15) + zmm27 *alpha
            VADDPD(ZMM(28), ZMM(28), MEM(RCX, RDI, 2, 128)) // zmm28 = C(16:23) + zmm28 *alpha
            /*store c*/
            VMOVUPD(MEM(RCX, RDI, 2     ), ZMM(26)) // C(0 :7 ) = zmm26
            VMOVUPD(MEM(RCX, RDI, 2, 64 ), ZMM(27)) // C(8 :15) = zmm27
            VMOVUPD(MEM(RCX, RDI, 2, 128), ZMM(28)) // C(16:23) = zmm28

            // row6
            VMULPD(ZMM(29), ZMM(29), ZMM(0)) // zmm29 *= alpha
            VMULPD(ZMM(30), ZMM(30), ZMM(0)) // zmm30 *= alpha
            VMULPD(ZMM(31), ZMM(31), ZMM(0)) // zmm31 *= alpha
            /*scale by beta*/
            VADDPD(ZMM(29), ZMM(29), MEM(RCX, RSI, 1     )) // zmm29 = C(0 :7 ) + zmm29 *alpha
            VADDPD(ZMM(30), ZMM(30), MEM(RCX, RSI, 1, 64 )) // zmm30 = C(8 :15) + zmm30 *alpha
            VADDPD(ZMM(31), ZMM(31), MEM(RCX, RSI, 1, 128)) // zmm31 = C(16:23) + zmm31 *alpha
            /*store c*/
            VMOVUPD(MEM(RCX, RSI, 1     ), ZMM(29)) // C(0 :7 ) = zmm29
            VMOVUPD(MEM(RCX, RSI, 1, 64 ), ZMM(30)) // C(8 :15) = zmm30
            VMOVUPD(MEM(RCX, RSI, 1, 128), ZMM(31)) // C(16:23) = zmm31
            JMP(END)

        LABEL(BETA_ZERO)
            // row0
            VMULPD(ZMM( 8), ZMM( 8), ZMM(0)) // zmm8  *= alpha
            VMULPD(ZMM( 9), ZMM( 9), ZMM(0)) // zmm9  *= alpha
            VMULPD(ZMM(10), ZMM(10), ZMM(0)) // zmm10 *= alpha
            /*store c*/
            VMOVUPD(MEM(RCX    ), ZMM( 8)) // C(0 :7 ) = zmm8
            VMOVUPD(MEM(RCX, 64), ZMM( 9)) // C(7 :15) = zmm9
            VMOVUPD(MEM(RCX,128), ZMM(10)) // C(16:23) = zmm10

            // row1
            VMULPD(ZMM(11), ZMM(11), ZMM(0)) // zmm11 *= alpha
            VMULPD(ZMM(12), ZMM(12), ZMM(0)) // zmm12 *= alpha
            VMULPD(ZMM(13), ZMM(13), ZMM(0)) // zmm13 *= alpha
            /*store c*/
            VMOVUPD(MEM(RCX, R8, 1     ), ZMM(11)) // C(0 :7 ) = zmm11
            VMOVUPD(MEM(RCX, R8, 1, 64 ), ZMM(12)) // C(7 :15) = zmm12
            VMOVUPD(MEM(RCX, R8, 1, 128), ZMM(13)) // C(16:23) = zmm13

            // row2
            VMULPD(ZMM(14), ZMM(14), ZMM(0)) // zmm14 *= alpha
            VMULPD(ZMM(15), ZMM(15), ZMM(0)) // zmm15 *= alpha
            VMULPD(ZMM(16), ZMM(16), ZMM(0)) // zmm16 *= alpha
            /*store c*/
            VMOVUPD(MEM(RCX, R8, 2     ), ZMM(14)) // C(0 :7 ) = zmm14
            VMOVUPD(MEM(RCX, R8, 2, 64 ), ZMM(15)) // C(7 :15) = zmm15
            VMOVUPD(MEM(RCX, R8, 2, 128), ZMM(16)) // C(16:23) = zmm16

            // row3
            VMULPD(ZMM(17), ZMM(17), ZMM(0)) // zmm17 *= alpha
            VMULPD(ZMM(18), ZMM(18), ZMM(0)) // zmm18 *= alpha
            VMULPD(ZMM(19), ZMM(19), ZMM(0)) // zmm19 *= alpha
            /*store c*/
            VMOVUPD(MEM(RCX, RDI, 1     ), ZMM(17)) // C(0 :7 ) = zmm17
            VMOVUPD(MEM(RCX, RDI, 1, 64 ), ZMM(18)) // C(7 :15) = zmm18
            VMOVUPD(MEM(RCX, RDI, 1, 128), ZMM(19)) // C(16:23) = zmm19

            // row4
            VMULPD(ZMM(20), ZMM(20), ZMM(0)) // zmm20 *= alpha
            VMULPD(ZMM(21), ZMM(21), ZMM(0)) // zmm21 *= alpha
            VMULPD(ZMM(22), ZMM(22), ZMM(0)) // zmm22 *= alpha
            /*store c*/
            VMOVUPD(MEM(RCX, R8, 4     ), ZMM(20)) // C(0 :7 ) = zmm20
            VMOVUPD(MEM(RCX, R8, 4, 64 ), ZMM(21)) // C(7 :15) = zmm21
            VMOVUPD(MEM(RCX, R8, 4, 128), ZMM(22)) // C(16:23) = zmm22

            // row5
            VMULPD(ZMM(23), ZMM(23), ZMM(0)) // zmm23 *= alpha
            VMULPD(ZMM(24), ZMM(24), ZMM(0)) // zmm24 *= alpha
            VMULPD(ZMM(25), ZMM(25), ZMM(0)) // zmm25 *= alpha
            /*store c*/
            VMOVUPD(MEM(RCX, RDX, 1     ), ZMM(23)) // C(0 :7 ) = zmm23
            VMOVUPD(MEM(RCX, RDX, 1, 64 ), ZMM(24)) // C(7 :15) = zmm24
            VMOVUPD(MEM(RCX, RDX, 1, 128), ZMM(25)) // C(16:23) = zmm25

            // row6
            VMULPD(ZMM(26), ZMM(26), ZMM(0)) // zmm26 *= alpha
            VMULPD(ZMM(27), ZMM(27), ZMM(0)) // zmm27 *= alpha
            VMULPD(ZMM(28), ZMM(28), ZMM(0)) // zmm28 *= alpha
            /*store c*/
            VMOVUPD(MEM(RCX, RDI, 2     ), ZMM(26)) // C(0 :7 ) = zmm26
            VMOVUPD(MEM(RCX, RDI, 2, 64 ), ZMM(27)) // C(7 :15) = zmm27
            VMOVUPD(MEM(RCX, RDI, 2, 128), ZMM(28)) // C(16:23) = zmm28

            // row6
            VMULPD(ZMM(29), ZMM(29), ZMM(0)) // zmm29 *= alpha
            VMULPD(ZMM(30), ZMM(30), ZMM(0)) // zmm30 *= alpha
            VMULPD(ZMM(31), ZMM(31), ZMM(0)) // zmm31 *= alpha
            /*store c*/
            VMOVUPD(MEM(RCX, RSI, 1     ), ZMM(29)) // C(0 :7 ) = zmm29
            VMOVUPD(MEM(RCX, RSI, 1, 64 ), ZMM(30)) // C(7 :15) = zmm30
            VMOVUPD(MEM(RCX, RSI, 1, 128), ZMM(31)) // C(16:23) = zmm31

            JMP(END)

    LABEL(BETA_NZ_N1) // beta not zero or not 1
#endif //BETA_OPTIMIZATION
            VBROADCASTSD(ZMM(1), MEM(RBX)) // broadcast beta to zmm1

            // row0
            VMULPD(ZMM( 8), ZMM( 8), ZMM(0)) // zmm8  *= alpha
            VMULPD(ZMM( 9), ZMM( 9), ZMM(0)) // zmm9  *= alpha
            VMULPD(ZMM(10), ZMM(10), ZMM(0)) // zmm10 *= alpha
            /*scale by beta*/
            VFMADD231PD(ZMM( 8), ZMM(1), MEM(RCX))     // zmm8  = zmm1*C(0 :7 ) + zmm8, zmm8 = beta*C(0 :7 ) + zmm8
            VFMADD231PD(ZMM( 9), ZMM(1), MEM(RCX,64))  // zmm9  = zmm1*C(8 :15) + zmm9
            VFMADD231PD(ZMM(10), ZMM(1), MEM(RCX,128)) // zmm10 = zmm1*C(16:23) + zmm10
            /*store c*/
            VMOVUPD(MEM(RCX    ), ZMM( 8)) // C(0 :7 ) = zmm8
            VMOVUPD(MEM(RCX, 64), ZMM( 9)) // C(7 :15) = zmm9
            VMOVUPD(MEM(RCX,128), ZMM(10)) // C(16:23) = zmm10

            // row1
            VMULPD(ZMM(11), ZMM(11), ZMM(0)) // zmm11 *= alpha
            VMULPD(ZMM(12), ZMM(12), ZMM(0)) // zmm12 *= alpha
            VMULPD(ZMM(13), ZMM(13), ZMM(0)) // zmm13 *= alpha
            /*scale by beta*/
            VFMADD231PD(ZMM(11), ZMM(1), MEM(RCX, R8, 1     )) // zmm11 = zmm1*C(0 :7 ) + zmm11
            VFMADD231PD(ZMM(12), ZMM(1), MEM(RCX, R8, 1, 64 )) // zmm12 = zmm1*C(8 :15) + zmm12
            VFMADD231PD(ZMM(13), ZMM(1), MEM(RCX, R8, 1, 128)) // zmm13 = zmm1*C(16:23) + zmm13
            /*store c*/
            VMOVUPD(MEM(RCX, R8, 1     ), ZMM(11)) // C(0 :7 ) = zmm11
            VMOVUPD(MEM(RCX, R8, 1, 64 ), ZMM(12)) // C(7 :15) = zmm12
            VMOVUPD(MEM(RCX, R8, 1, 128), ZMM(13)) // C(16:23) = zmm13

            // row2
            VMULPD(ZMM(14), ZMM(14), ZMM(0)) // zmm14 *= alpha
            VMULPD(ZMM(15), ZMM(15), ZMM(0)) // zmm15 *= alpha
            VMULPD(ZMM(16), ZMM(16), ZMM(0)) // zmm16 *= alpha
            /*scale by beta*/
            VFMADD231PD(ZMM(14), ZMM(1), MEM(RCX, R8, 2     )) // zmm14 = zmm1*C(0 :7 ) + zmm14
            VFMADD231PD(ZMM(15), ZMM(1), MEM(RCX, R8, 2, 64 )) // zmm15 = zmm1*C(8 :15) + zmm15
            VFMADD231PD(ZMM(16), ZMM(1), MEM(RCX, R8, 2, 128)) // zmm16 = zmm1*C(16:23) + zmm16
            /*store c*/
            VMOVUPD(MEM(RCX, R8, 2     ), ZMM(14)) // C(0 :7 ) = zmm14
            VMOVUPD(MEM(RCX, R8, 2, 64 ), ZMM(15)) // C(7 :15) = zmm15
            VMOVUPD(MEM(RCX, R8, 2, 128), ZMM(16)) // C(16:23) = zmm16

            // row3
            VMULPD(ZMM(17), ZMM(17), ZMM(0)) // zmm17 *= alpha
            VMULPD(ZMM(18), ZMM(18), ZMM(0)) // zmm18 *= alpha
            VMULPD(ZMM(19), ZMM(19), ZMM(0)) // zmm19 *= alpha
            /*scale by beta*/
            VFMADD231PD(ZMM(17), ZMM(1), MEM(RCX, RDI, 1     )) // zmm17 = zmm1*C(0 :7 ) + zmm17
            VFMADD231PD(ZMM(18), ZMM(1), MEM(RCX, RDI, 1, 64 )) // zmm18 = zmm1*C(8 :15) + zmm18
            VFMADD231PD(ZMM(19), ZMM(1), MEM(RCX, RDI, 1, 128)) // zmm19 = zmm1*C(16:23) + zmm19
            /*store c*/
            VMOVUPD(MEM(RCX, RDI, 1     ), ZMM(17)) // C(0 :7 ) = zmm17
            VMOVUPD(MEM(RCX, RDI, 1, 64 ), ZMM(18)) // C(7 :15) = zmm18
            VMOVUPD(MEM(RCX, RDI, 1, 128), ZMM(19)) // C(16:23) = zmm19

            // row4
            VMULPD(ZMM(20), ZMM(20), ZMM(0)) // zmm20 *= alpha
            VMULPD(ZMM(21), ZMM(21), ZMM(0)) // zmm21 *= alpha
            VMULPD(ZMM(22), ZMM(22), ZMM(0)) // zmm22 *= alpha
            /*scale by beta*/
            VFMADD231PD(ZMM(20), ZMM(1), MEM(RCX, R8, 4     )) // zmm20 = zmm1*C(0 :7 ) + zmm20
            VFMADD231PD(ZMM(21), ZMM(1), MEM(RCX, R8, 4, 64 )) // zmm21 = zmm1*C(8 :15) + zmm21
            VFMADD231PD(ZMM(22), ZMM(1), MEM(RCX, R8, 4, 128)) // zmm22 = zmm1*C(16:23) + zmm22
            /*store c*/
            VMOVUPD(MEM(RCX, R8, 4     ), ZMM(20)) // C(0 :7 ) = zmm20
            VMOVUPD(MEM(RCX, R8, 4, 64 ), ZMM(21)) // C(7 :15) = zmm21
            VMOVUPD(MEM(RCX, R8, 4, 128), ZMM(22)) // C(16:23) = zmm22

            // row5
            VMULPD(ZMM(23), ZMM(23), ZMM(0)) // zmm23 *= alpha
            VMULPD(ZMM(24), ZMM(24), ZMM(0)) // zmm24 *= alpha
            VMULPD(ZMM(25), ZMM(25), ZMM(0)) // zmm25 *= alpha
            /*scale by beta*/
            VFMADD231PD(ZMM(23), ZMM(1), MEM(RCX, RDX, 1     )) // zmm23 = zmm1*C(0 :7 ) + zmm23
            VFMADD231PD(ZMM(24), ZMM(1), MEM(RCX, RDX, 1, 64 )) // zmm24 = zmm1*C(8 :15) + zmm24
            VFMADD231PD(ZMM(25), ZMM(1), MEM(RCX, RDX, 1, 128)) // zmm25 = zmm1*C(16:23) + zmm25
            /*store c*/
            VMOVUPD(MEM(RCX, RDX, 1     ), ZMM(23)) // C(0 :7 ) = zmm23
            VMOVUPD(MEM(RCX, RDX, 1, 64 ), ZMM(24)) // C(7 :15) = zmm24
            VMOVUPD(MEM(RCX, RDX, 1, 128), ZMM(25)) // C(16:23) = zmm25

            // row6
            VMULPD(ZMM(26), ZMM(26), ZMM(0)) // zmm26 *= alpha
            VMULPD(ZMM(27), ZMM(27), ZMM(0)) // zmm27 *= alpha
            VMULPD(ZMM(28), ZMM(28), ZMM(0)) // zmm28 *= alpha
            /*scale by beta*/
            VFMADD231PD(ZMM(26), ZMM(1), MEM(RCX, RDI, 2     )) // zmm26 = zmm1*C(0 :7 ) + zmm26
            VFMADD231PD(ZMM(27), ZMM(1), MEM(RCX, RDI, 2, 64 )) // zmm27 = zmm1*C(8 :15) + zmm27
            VFMADD231PD(ZMM(28), ZMM(1), MEM(RCX, RDI, 2, 128)) // zmm28 = zmm1*C(16:23) + zmm28
            /*store c*/
            VMOVUPD(MEM(RCX, RDI, 2     ), ZMM(26)) // C(0 :7 ) = zmm26
            VMOVUPD(MEM(RCX, RDI, 2, 64 ), ZMM(27)) // C(7 :15) = zmm27
            VMOVUPD(MEM(RCX, RDI, 2, 128), ZMM(28)) // C(16:23) = zmm28

            // row6
            VMULPD(ZMM(29), ZMM(29), ZMM(0)) // zmm29 *= alpha
            VMULPD(ZMM(30), ZMM(30), ZMM(0)) // zmm20 *= alpha
            VMULPD(ZMM(31), ZMM(31), ZMM(0)) // zmm31 *= alpha
            /*scale by beta*/
            VFMADD231PD(ZMM(29), ZMM(1), MEM(RCX, RSI, 1     )) // zmm29 = zmm1*C(0 :7 ) + zmm29
            VFMADD231PD(ZMM(30), ZMM(1), MEM(RCX, RSI, 1, 64 )) // zmm30 = zmm1*C(8 :15) + zmm30
            VFMADD231PD(ZMM(31), ZMM(1), MEM(RCX, RSI, 1, 128)) // zmm31 = zmm1*C(16:23) + zmm31
            /*store c*/
            VMOVUPD(MEM(RCX, RSI, 1     ), ZMM(29)) // C(0 :7 ) = zmm29
            VMOVUPD(MEM(RCX, RSI, 1, 64 ), ZMM(30)) // C(7 :15) = zmm30
            VMOVUPD(MEM(RCX, RSI, 1, 128), ZMM(31)) // C(16:23) = zmm31
#ifdef ENABLE_COL_GEN_STORE
            JMP(END)

    LABEL(COLUPDATE)
        // if C is col major stored
        // R9 = cs_c
        VBROADCASTSD(ZMM(1), MEM(RBX)) // broadcast beta to zmm1

        LEA(RDI, MEM(R9,  R9, 2)) // cs_c*3 -> cs_c + cs_c*2
        LEA(RDX, MEM(R9,  R9, 4)) // cs_c*5 -> cs_c + cs_c*4
        LEA(RSI, MEM(R9, RDI, 2)) // cs_c*7 -> cs_c + cs_c*3*2

        VCOMISD(XMM(1), XMM(2))
        JE(COLSTORBZ)             // jump is beta == 0
            // beta != 0

            /*
            * // registers pre tranpose
            *  _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
            * |    zmm8      |      zmm9      |     zmm10     |
            * |    zmm11     |      zmm12     |     zmm13     |
            * |    zmm14     |      zmm15     |     zmm16     |
            * |    zmm17     |      zmm18     |     zmm19     |
            * |    zmm20     |      zmm21     |     zmm22     |
            * |    zmm23     |      zmm24     |     zmm25     |
            * |    zmm26     |      zmm27     |     zmm28     |
            * |    zmm29     |      zmm30     |     zmm31     |
            *  _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
            *
            *
            * // registers post transpose
            *  __________________________
            * |  z  z  z  z  z  z  z  z |
            * |  m  m  m  m  m  m  m  m |
            * |  m  m  m  m  m  m  m  m |
            * |  8  1  1  1  2  2  2  2 |
            * |     1  4  7  0  3  6  9 |
            * | ________________________|
            * |  z  z  z  z  z  z  z  z |
            * |  m  m  m  m  m  m  m  m |
            * |  m  m  m  m  m  m  m  m |
            * |  9  1  1  1  2  2  2  3 |
            * |     2  5  8  1  4  7  0 |
            * | ________________________|
            * |  z  z  z  z  z  z  z  z |
            * |  m  m  m  m  m  m  m  m |
            * |  m  m  m  m  m  m  m  m |
            * |  1  1  1  1  2  2  2  3 |
            * |  0  3  6  9  2  5  8  1 |
            * | ________________________|
            */


            TRANSPOSE_8X8( 8, 11, 14, 17, 20, 23, 26, 29) // registers
            TRANSPOSE_8X8( 9, 12, 15, 18, 21, 24, 27, 30)
            TRANSPOSE_8X8(10, 13, 16, 19, 22, 25, 28, 31)
            VBROADCASTSD(ZMM(1), MEM(RBX)) // broadcast beta to zmm1
            VBROADCASTSD(ZMM(0), MEM(RAX)) // broadcast alpha into zmm0

            UPDATE_C_COL_STORE( 8, 11, 14, 17, 20, 23, 26, 29) // scale by beta and store
            UPDATE_C_COL_STORE( 9, 12, 15, 18, 21, 24, 27, 30)
            UPDATE_C_COL_STORE(10, 13, 16, 19, 22, 25, 28, 31)
            JMP(END)

        LABEL(COLSTORBZ)
            // beta == 0

            TRANSPOSE_8X8( 8, 11, 14, 17, 20, 23, 26, 29)
            TRANSPOSE_8X8( 9, 12, 15, 18, 21, 24, 27, 30)
            TRANSPOSE_8X8(10, 13, 16, 19, 22, 25, 28, 31)
            VBROADCASTSD(ZMM(0), MEM(RAX)) // broadcast alpha into zmm0

            UPDATE_C_COL_STORE_BZ( 8, 11, 14, 17, 20, 23, 26, 29)
            UPDATE_C_COL_STORE_BZ( 9, 12, 15, 18, 21, 24, 27, 30)
            UPDATE_C_COL_STORE_BZ(10, 13, 16, 19, 22, 25, 28, 31)
            JMP(END)

    LABEL(SCATTERUPDATE)
        // if C is general stride
        VMULPD(ZMM( 8), ZMM( 8), ZMM(0)) // scale all registers by alpha
        VMULPD(ZMM( 9), ZMM( 9), ZMM(0))
        VMULPD(ZMM(10), ZMM(10), ZMM(0))
        VMULPD(ZMM(11), ZMM(11), ZMM(0))
        VMULPD(ZMM(12), ZMM(12), ZMM(0))
        VMULPD(ZMM(13), ZMM(13), ZMM(0))
        VMULPD(ZMM(14), ZMM(14), ZMM(0))
        VMULPD(ZMM(15), ZMM(15), ZMM(0))
        VMULPD(ZMM(16), ZMM(16), ZMM(0))
        VMULPD(ZMM(17), ZMM(17), ZMM(0))
        VMULPD(ZMM(18), ZMM(18), ZMM(0))
        VMULPD(ZMM(19), ZMM(19), ZMM(0))
        VMULPD(ZMM(20), ZMM(20), ZMM(0))
        VMULPD(ZMM(21), ZMM(21), ZMM(0))
        VMULPD(ZMM(22), ZMM(22), ZMM(0))
        VMULPD(ZMM(23), ZMM(23), ZMM(0))
        VMULPD(ZMM(24), ZMM(24), ZMM(0))
        VMULPD(ZMM(25), ZMM(25), ZMM(0))
        VMULPD(ZMM(26), ZMM(26), ZMM(0))
        VMULPD(ZMM(27), ZMM(27), ZMM(0))
        VMULPD(ZMM(28), ZMM(28), ZMM(0))
        VMULPD(ZMM(29), ZMM(29), ZMM(0))
        VMULPD(ZMM(30), ZMM(30), ZMM(0))
        VMULPD(ZMM(31), ZMM(31), ZMM(0))

        MOV(RDI, VAR(offsetPtr))               // load pointer to the array containing
                                               // offsets for scatter/gather
        VPBROADCASTQ(ZMM(0), R9)               // broadcast cs_c to zmm0
        VPMULLQ(ZMM(2), ZMM(0), MEM(RDI))      // scale offsets array with cs_c
        VPMULLQ(ZMM(3), ZMM(0), MEM(RDI, 8*8))
        VPMULLQ(ZMM(4), ZMM(0), MEM(RDI,16*8))
        VBROADCASTSD(ZMM(1), MEM(RBX)) // broadcast beta to zmm1

        VCOMISD(XMM(1), XMM(2))
        JE(GENSTORBZ)                          // if beta == 0 jump
            UPDATE_C_SCATTERED( 8,  9, 10) // scale by beta and store
            UPDATE_C_SCATTERED(11, 12, 13)
            UPDATE_C_SCATTERED(14, 15, 16)
            UPDATE_C_SCATTERED(17, 18, 19)
            UPDATE_C_SCATTERED(20, 21, 22)
            UPDATE_C_SCATTERED(23, 24, 25)
            UPDATE_C_SCATTERED(26, 27, 28)
            UPDATE_C_SCATTERED(29, 30, 31)
            JMP(END)
        LABEL(GENSTORBZ)
            UPDATE_C_SCATTERED_BZ( 8,  9, 10)
            UPDATE_C_SCATTERED_BZ(11, 12, 13)
            UPDATE_C_SCATTERED_BZ(14, 15, 16)
            UPDATE_C_SCATTERED_BZ(17, 18, 19)
            UPDATE_C_SCATTERED_BZ(20, 21, 22)
            UPDATE_C_SCATTERED_BZ(23, 24, 25)
            UPDATE_C_SCATTERED_BZ(26, 27, 28)
            UPDATE_C_SCATTERED_BZ(29, 30, 31)
#endif

    LABEL(END)

    // VZEROUPPER() // slight imporvement when K is small by removing vzeroupper

    END_ASM
    (
        : // output operands
        : // input operands
          [k]         "m" (k),
          [a]         "m" (a),
          [b]         "m" (b),
          [alpha]     "m" (alpha),
          [beta]      "m" (beta),
          [c]         "m" (c),
          [rs_c]      "m" (rs_c),
          [cs_c]      "m" (cs_c),
          [offsetPtr] "m" (offsetPtr),
          [b_next]    "m" (b_next)
        : // register clobber list
          "rax", "rbx", "rcx", "rdi", "rsi", "r8", "r9",
          "k0", "k1", "k2", "k3", "xmm1", "xmm2",
          "zmm0", "zmm1", "zmm2", "zmm3", "zmm4", "zmm5", "zmm6",
          "zmm7", "zmm8", "zmm9", "zmm10", "zmm11", "zmm12", "zmm13",
          "zmm14", "zmm15", "zmm16", "zmm17", "zmm18", "zmm19",
          "zmm20", "zmm21", "zmm22", "zmm23", "zmm24", "zmm25",
          "zmm26", "zmm27", "zmm28", "zmm29", "zmm30", "zmm31", "memory"
    )
}