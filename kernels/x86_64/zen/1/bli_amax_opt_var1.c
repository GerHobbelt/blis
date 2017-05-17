/*

   BLIS    
   An object-based framework for developing high-performance BLAS-like
   libraries.

   Copyright (C) 2016, Advanced Micro Devices, Inc

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:
    - Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    - Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    - Neither the name of The University of Texas at Austin nor the names
      of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "blis.h"
#include "immintrin.h"
//
// Define BLAS-like interfaces with typed operands.
//
/* Union data structure to access AVX registers
*  AVX 256 bit register holds 8 SP data*/
typedef union
{
    __m256 v;
    float f[8];
}v8ff_t;

typedef union
{
    __m128 v;
    float f[4];
}v4ff_t;

void bli_samaxv_opt_var1
     (
       dim_t    n,
       float*   x, inc_t incx,
       gint_t* abmax_i,
       cntx_t*  cntx
     )
{
    float* minus_one = ( ( float*    ) ( void* )(
               ( ( char* )( ( (BLIS_MINUS_ONE).buffer ) ) ) +
                 ( dim_t )( BLIS_FLOAT * sizeof(dcomplex) )
             ) );
    gint_t* zero_i    = ( ( gint_t*   ) ( void* )(
               ( ( char* )( ( (BLIS_ZERO).buffer ) ) ) +
                 ( dim_t )( BLIS_INT * sizeof(dcomplex) )
             ) );

    float*   chi1;
    float  chi1_r;
    float  abs_chi1_max;
    gint_t  i_max;
    dim_t   i;

    /* Initialize the index of the maximum absolute value to zero. */
    { (i_max) = ( gint_t ) (*zero_i); };

    /* If the vector length is zero, return early. This directly emulates
       the behavior of netlib BLAS's i?amax() routines. */
    if ( ( (n) == 0 ) ) return;

    /* Initialize the maximum absolute value search candidate with
       -1, which is guaranteed to be less than all values we will
       compute. */

    abs_chi1_max = (*minus_one);

    if((incx != 1) || ( n < 8))
    {
        for ( i = 0; i < n; ++i )
        {
            chi1 = x + (i  )*incx;

            /* Get the real and imaginary components of chi1. */
            (chi1_r) = ( *chi1 );

            /* Replace chi1_r and chi1_i with their absolute values. */

            (chi1_r)       = fabsf(chi1_r);

            /* If the absolute value of the current element exceeds that of
           the previous largest, save it and its index. If NaN is
           encountered, then treat it the same as if it were a valid
           value that was smaller than any previously seen. This
           behavior mimics that of LAPACK's ?lange(). */
            if ( abs_chi1_max < chi1_r || isnan( chi1_r ) )
            {
                (( abs_chi1_max )) = (( chi1_r ));
                (i_max) = ( gint_t ) (i);
            }
        }
    }
    else
    {
        int n_iter, n_left;
        int num_vec_elements = 8;
        v8ff_t x_vec, max_vec, maxInx_vec, mask_vec;
        v8ff_t idx_vec, inc_vec;
        v8ff_t sign_mask;

        v4ff_t max_vec_lo, max_vec_hi, mask_vec_lo;
        v4ff_t maxInx_vec_lo, maxInx_vec_hi;

        n_iter = n / num_vec_elements;
        n_left = n % num_vec_elements;

        idx_vec.v = _mm256_set_ps(7,6,5,4,3,2,1,0);
        inc_vec.v = _mm256_set1_ps(8);
        max_vec.v = _mm256_set1_ps(-1);
        maxInx_vec.v = _mm256_setzero_ps();
        sign_mask.v = _mm256_set1_ps(-0.f);

        for( i = 0; i < n_iter; i++)
        {
            x_vec.v = _mm256_loadu_ps(x);
            // get the absolute value of the vector element
            x_vec.v = _mm256_andnot_ps(sign_mask.v, x_vec.v);

            mask_vec.v = _mm256_cmp_ps(x_vec.v, max_vec.v, _CMP_GT_OS);

            max_vec.v = _mm256_blendv_ps(max_vec.v, x_vec.v, mask_vec.v);
            maxInx_vec.v = _mm256_blendv_ps(maxInx_vec.v, idx_vec.v, mask_vec.v);

            idx_vec.v += inc_vec.v;
            x += num_vec_elements;
        }
        max_vec_lo.v = _mm256_extractf128_ps(max_vec.v, 0);
        max_vec_hi.v = _mm256_extractf128_ps(max_vec.v, 1);
        mask_vec_lo.v = _mm_cmp_ps(max_vec_hi.v, max_vec_lo.v, _CMP_GT_OS);

        max_vec_lo.v = _mm_blendv_ps(max_vec_lo.v, max_vec_hi.v, mask_vec_lo.v);

        maxInx_vec_lo.v = _mm256_extractf128_ps(maxInx_vec.v, 0);
        maxInx_vec_hi.v = _mm256_extractf128_ps(maxInx_vec.v, 1);
        maxInx_vec_lo.v = _mm_blendv_ps(maxInx_vec_lo.v, maxInx_vec_hi.v, mask_vec_lo.v);

        max_vec_hi.v = _mm_permute_ps(max_vec_lo.v, 14);
        maxInx_vec_hi.v = _mm_permute_ps(maxInx_vec_lo.v, 14);
        mask_vec_lo.v = _mm_cmp_ps(max_vec_hi.v, max_vec_lo.v, _CMP_GT_OS);

        max_vec_lo.v = _mm_blendv_ps(max_vec_lo.v, max_vec_hi.v, mask_vec_lo.v);
        maxInx_vec_lo.v = _mm_blendv_ps(maxInx_vec_lo.v, maxInx_vec_hi.v, mask_vec_lo.v);

        if(max_vec_lo.f[0] > max_vec_lo.f[1])
        {
            abs_chi1_max = max_vec_lo.f[0];
            i_max = maxInx_vec_lo.f[0];
        }
        else
        {
            abs_chi1_max = max_vec_lo.f[1];
            i_max = maxInx_vec_lo.f[1];
        }

        for(i = n - n_left; i < n; i++)
        {
            chi1 = x;

            /* Get the real and imaginary components of chi1. */
            (chi1_r) = ( *chi1 );

            /* Replace chi1_r and chi1_i with their absolute values. */

            (chi1_r)       = fabsf(chi1_r);

            /* If the absolute value of the current element exceeds that of
           the previous largest, save it and its index. If NaN is
           encountered, then treat it the same as if it were a valid
           value that was smaller than any previously seen. This
           behavior mimics that of LAPACK's ?lange(). */
            if ( abs_chi1_max < chi1_r || isnan( chi1_r ) )
            {

                (( abs_chi1_max )) = (( chi1_r ));

                (i_max) = ( gint_t ) (i);
            }

            x += 1;
        }
    }
    /* Store final index to output variable. */
    { (*abmax_i) = ( gint_t ) (i_max); };
}
/* Union data structure to access AVX registers
*  AVX 256 bit register holds 4 DP data*/
typedef union
{
    __m256d v;
    double d[4];
}v4dd_t;

typedef union
{
    __m128d v;
    double d[2];
}v2dd_t;

void bli_damaxv_opt_var1
     (
       dim_t    n,
       double*  x, inc_t incx,
       dim_t*   i_max,
       cntx_t*  cntx
     )
{
    double* minus_one = ( ( double*   ) ( void* )(
               ( ( char* )( ( (BLIS_MINUS_ONE).buffer ) ) ) +
                 ( dim_t )( BLIS_DOUBLE * sizeof(dcomplex) )
             ) );
    dim_t*   zero_i    = ( ( gint_t*   ) ( void* )(
               ( ( char* )( ( (BLIS_ZERO).buffer ) ) ) +
                 ( dim_t )( BLIS_INT * sizeof(dcomplex) )
             ) );

    double  chi1_r;
    double  abs_chi1;
    double  abs_chi1_max;
    dim_t    i;

    /* Initialize the index of the maximum absolute value to zero. */ \
    { (*i_max) = ( gint_t ) (zero_i); };

    /* If the vector length is zero, return early. This directly emulates
       the behavior of netlib BLAS's i?amax() routines. */
    if ( ( (n) == 0 ) ) return;

    /* Initialize the maximum absolute value search candidate with
       -1, which is guaranteed to be less than all values we will
       compute. */

    (( abs_chi1_max )) = (( *minus_one ));


    if( (incx != 1) || (n < 4))
    {
        for ( i = 0; i < n; ++i )
         {
             double* chi1 = x + i * incx;

             /* Get the real and imaginary components of chi1. */
             chi1_r  = ( *chi1 );

             /* Replace chi1_r and chi1_i with their absolute values. */
             chi1_r = fabs(chi1_r);

             /* Add the real and imaginary absolute values together. */

             abs_chi1 = chi1_r ;

             /* If the absolute value of the current element exceeds that of
                the previous largest, save it and its index. If NaN is
                encountered, then treat it the same as if it were a valid
                value that was smaller than any previously seen. This
                behavior mimics that of LAPACK's ?lange(). */
             if ( abs_chi1_max < abs_chi1 || isnan( abs_chi1 ) )
             {
                 abs_chi1_max = abs_chi1;
                 *i_max       = i;
             }
         }
    }
    else
    {
        int n_iter, n_left;
        int num_vec_elements = 4;
        v4dd_t x_vec, max_vec, maxInx_vec, mask_vec;
        v4dd_t idx_vec, inc_vec;
        v4dd_t sign_mask;

        v2dd_t max_vec_lo, max_vec_hi, mask_vec_lo;
        v2dd_t maxInx_vec_lo, maxInx_vec_hi;

        n_iter = n / num_vec_elements;
        n_left = n % num_vec_elements;

        idx_vec.v = _mm256_set_pd(3,2,1,0);
        inc_vec.v = _mm256_set1_pd(4);
        max_vec.v = _mm256_set1_pd(-1);
        maxInx_vec.v = _mm256_setzero_pd();
        sign_mask.v = _mm256_set1_pd(-0.f);

        for( i = 0; i < n_iter; i++)
        {
            x_vec.v = _mm256_loadu_pd(x);
            // get the absolute value of the vector element
            x_vec.v = _mm256_andnot_pd(sign_mask.v, x_vec.v);

            mask_vec.v = _mm256_cmp_pd(x_vec.v, max_vec.v, _CMP_GT_OS);

            max_vec.v = _mm256_blendv_pd(max_vec.v, x_vec.v, mask_vec.v);
            maxInx_vec.v = _mm256_blendv_pd(maxInx_vec.v, idx_vec.v, mask_vec.v);

            idx_vec.v += inc_vec.v;
            x += num_vec_elements;
        }

        max_vec_lo.v = _mm256_extractf128_pd(max_vec.v, 0);
        max_vec_hi.v = _mm256_extractf128_pd(max_vec.v, 1);
        mask_vec_lo.v = _mm_cmp_pd(max_vec_hi.v, max_vec_lo.v, _CMP_GT_OS);

        max_vec_lo.v = _mm_blendv_pd(max_vec_lo.v, max_vec_hi.v, mask_vec_lo.v);

        maxInx_vec_lo.v = _mm256_extractf128_pd(maxInx_vec.v, 0);
        maxInx_vec_hi.v = _mm256_extractf128_pd(maxInx_vec.v, 1);
        maxInx_vec_lo.v = _mm_blendv_pd(maxInx_vec_lo.v, maxInx_vec_hi.v, mask_vec_lo.v);

        if(max_vec_lo.d[0] > max_vec_lo.d[1])
        {
            abs_chi1_max = max_vec_lo.d[0];
            *i_max = maxInx_vec_lo.d[0];
        }
        else
        {
            abs_chi1_max = max_vec_lo.d[1];
            *i_max = maxInx_vec_lo.d[1];
        }

        for(i = n - n_left; i < n; i++)
          {
              /* Get the real and imaginary components of chi1. */
              (chi1_r) = ( *x );

              /* Replace chi1_r and chi1_i with their absolute values. */

              (chi1_r)       = fabsf(chi1_r);

              /* If the absolute value of the current element exceeds that of
             the previous largest, save it and its index. If NaN is
             encountered, then treat it the same as if it were a valid
             value that was smaller than any previously seen. This
             behavior mimics that of LAPACK's ?lange(). */
              if ( abs_chi1_max < chi1_r || isnan( chi1_r ) )
              {

                  (( abs_chi1_max )) = (( chi1_r ));

                  (*i_max) = ( gint_t ) (i);
              }

              x += 1;
          }
    }
}

#undef  GENTFUNCR
#define GENTFUNCR( ctype, ctype_r, ch, chr, varname ) \
\
void PASTEMAC(ch,varname) \
     ( \
       dim_t    n, \
       ctype*   x, inc_t incx, \
       dim_t*   i_max, \
       cntx_t*  cntx  \
     ) \
{ \
	ctype_r* minus_one = PASTEMAC(chr,m1); \
	dim_t*   zero_i    = PASTEMAC(i,0); \
\
	ctype_r  chi1_r; \
	ctype_r  chi1_i; \
	ctype_r  abs_chi1; \
	ctype_r  abs_chi1_max; \
	dim_t    i; \
\
	/* Initialize the index of the maximum absolute value to zero. */ \
	PASTEMAC(i,copys)( zero_i, *i_max ); \
\
	/* If the vector length is zero, return early. This directly emulates
	   the behavior of netlib BLAS's i?amax() routines. */ \
	if ( bli_zero_dim1( n ) ) return; \
\
	/* Initialize the maximum absolute value search candidate with
	   -1, which is guaranteed to be less than all values we will
	   compute. */ \
	PASTEMAC(chr,copys)( *minus_one, abs_chi1_max ); \
\
	if ( incx == 1 ) \
	{ \
		for ( i = 0; i < n; ++i ) \
		{ \
			/* Get the real and imaginary components of chi1. */ \
			PASTEMAC2(ch,chr,gets)( x[i], chi1_r, chi1_i ); \
\
			/* Replace chi1_r and chi1_i with their absolute values. */ \
			PASTEMAC(chr,abval2s)( chi1_r, chi1_r ); \
			PASTEMAC(chr,abval2s)( chi1_i, chi1_i ); \
\
			/* Add the real and imaginary absolute values together. */ \
			PASTEMAC(chr,set0s)( abs_chi1 ); \
			PASTEMAC(chr,adds)( chi1_r, abs_chi1 ); \
			PASTEMAC(chr,adds)( chi1_i, abs_chi1 ); \
\
			/* If the absolute value of the current element exceeds that of
			   the previous largest, save it and its index. If NaN is
			   encountered, then treat it the same as if it were a valid
			   value that was smaller than any previously seen. This
			   behavior mimics that of LAPACK's ?lange(). */ \
			if ( abs_chi1_max < abs_chi1 || bli_isnan( abs_chi1 ) ) \
			{ \
				abs_chi1_max = abs_chi1; \
				*i_max       = i; \
			} \
		} \
	} \
	else \
	{ \
		for ( i = 0; i < n; ++i ) \
		{ \
			ctype* chi1 = x + (i  )*incx; \
\
			/* Get the real and imaginary components of chi1. */ \
			PASTEMAC2(ch,chr,gets)( *chi1, chi1_r, chi1_i ); \
\
			/* Replace chi1_r and chi1_i with their absolute values. */ \
			PASTEMAC(chr,abval2s)( chi1_r, chi1_r ); \
			PASTEMAC(chr,abval2s)( chi1_i, chi1_i ); \
\
			/* Add the real and imaginary absolute values together. */ \
			PASTEMAC(chr,set0s)( abs_chi1 ); \
			PASTEMAC(chr,adds)( chi1_r, abs_chi1 ); \
			PASTEMAC(chr,adds)( chi1_i, abs_chi1 ); \
\
			/* If the absolute value of the current element exceeds that of
			   the previous largest, save it and its index. If NaN is
			   encountered, then treat it the same as if it were a valid
			   value that was smaller than any previously seen. This
			   behavior mimics that of LAPACK's ?lange(). */ \
			if ( abs_chi1_max < abs_chi1 || bli_isnan( abs_chi1 ) ) \
			{ \
				abs_chi1_max = abs_chi1; \
				*i_max       = i; \
			} \
		} \
	} \
}
GENTFUNCR( scomplex, float,  c, s, amaxv_opt_var1 )
GENTFUNCR( dcomplex, double, z, d, amaxv_opt_var1 )
