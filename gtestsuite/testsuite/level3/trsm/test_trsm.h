/*

   BLIS
   An object-based framework for developing high-performance BLAS-like
   libraries.

   Copyright (C) 2023 - 2024, Advanced Micro Devices, Inc. All rights reserved.

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

#pragma once

#include "trsm.h"
#include "level3/ref_trsm.h"
#include "inc/check_error.h"
#include "common/testing_helpers.h"
#include <stdexcept>
#include <algorithm>

// ENUM for extreme value testing
typedef enum
{
    ZERO,
    NaN,
    NEG_NaN,
    INF,
    NEG_INF,
    NaN_INF,
    DIAG_NaN,
    DIAG_INF,
    NO_EVT
} EVT_TYPE;


/**
 * @brief Insert NaN/Inf in the matrix for extreme value testing
 *
 * @tparam T
 * @param mat     input matrix where NAN/Inf needs to be inserted
 * @param uploa   specify if input matrix in uppper or lower triangular
 * @param m       size of the input matrix
 * @param ld      leading dimension of input matrix
 * @param type    type of extreme value to be inserted ( EVT_TYPE )
 * @param is_a    is the input matrix traingular( matrix A in TRSM )
 * @param is_diag insert extreme value in diagonal element
 */
template<typename T>
void generate_NAN_INF( T* mat, char uploa, gtint_t m, gtint_t ld, EVT_TYPE type, bool is_a, bool is_diag = false)
{
    // RT contains the real type of T.
    using RT = typename testinghelpers::type_info<T>::real_type;
    // inf_nan will contain either inf or nan depending on requirement
    RT inf_nan = std::numeric_limits<RT>::quiet_NaN();

    if(type == INF)
    {
        inf_nan = std::numeric_limits<RT>::infinity();
    }
    else if (type == NEG_INF)
    {
        inf_nan = RT{-1} * std::numeric_limits<RT>::infinity();
    }
    else if (type == NEG_NaN)
    {
        inf_nan = RT{-1} * std::numeric_limits<RT>::quiet_NaN();
    }
    else // type == NaN
    {
        inf_nan = std::numeric_limits<RT>::quiet_NaN();
    }

    // exval will contain the exception value to be injected in the matrix.
    T exval;
    if constexpr ( testinghelpers::type_info<T>::is_real ) exval = T{inf_nan};
    else exval = T{inf_nan, inf_nan};

    // if size is one, then set the only element in matrix
    // to inf or nan
    if (m <= 1)
    {
        *(mat) = exval;
    }
    else
    {
        // get a random number in range of 1 to m;
        gtint_t mn = (std::max)(gtint_t(1), gtint_t(rand()) % m);
        if( uploa == 'l' || uploa == 'L')
        {
            // set one element to inf/nan in lower half of matrix
            *(mat + mn + ((mn - (!is_diag)) * ld) ) = exval;
        }
        else
        {
            // set one element to inf/nan in upper half of matrix
            *(mat + (mn - (!is_diag)) + (mn * ld) ) = exval;
        }
    }

    /*  // Make All elements NaN\INF
        // This test is commented out inorder to reduce the
        // testing time.
        // It is not needed to cover all the test cases, but
        // it can be enabled in future if the need arises.
        for (gtint_t i=0; i<m; i++)
        {
            for (gtint_t j=0; j<m; j++)
            {
                mat[i+j*ld] = inf_nan;
            }
        }
    */
}

/**
 * @brief initialize a matrix with random values within a range with some extreme values for TRSM
 *        From and to are set as double instead of int to make sure that the matrices can be
 *        initialized to decimal values as well.
 *
 * @tparam T
 * @param mat     // input matrix
 * @param uploa   // upper of lower triangulat matrix
 * @param storage // storage scheme of the matrix
 * @param trans   // is matrix transposed
 * @param from    // starting range for the random values to be inserted in input matrix
 * @param to      // enduing range for the random values to be inserted in input matrix
 * @param m       // m dim of input matrix
 * @param n       // n dim of input matrix
 * @param ld      // leading dimension of the matrix
 * @param type    // type of extreme value (EVT_TYPE )
 * @param is_a    // is input matrix a triangular matrix
 */
template<typename T>
void random_generator_with_INF_NAN( T* mat, char uploa, char storage, char trans, double from, double to, gtint_t m,
gtint_t n, gtint_t ld, EVT_TYPE type = NO_EVT, bool is_a = false )
{
    switch( type )
    {
        case ZERO:
            testinghelpers::datagenerators::randomgenerators<T>( 0, 0, storage, m, n, mat, ld);
            break;
        case NaN:
        case INF:
            testinghelpers::datagenerators::randomgenerators<T>( from, to, storage, m, n, mat, ld);
            generate_NAN_INF(mat, uploa, (std::min)(m, n), ld, type, is_a);
            break;
        case DIAG_INF:
        case DIAG_NaN:
            testinghelpers::datagenerators::randomgenerators<T>( from, to, storage, m, n, mat, ld);
            generate_NAN_INF(mat, uploa, (std::min)(m, n), ld, type, is_a, true);
            break;
        case NaN_INF:
            testinghelpers::datagenerators::randomgenerators<T>( from, to, storage, m, n, mat, ld);
            generate_NAN_INF(mat, uploa, (std::min)(m, n), ld, type, is_a);
            generate_NAN_INF(mat, uploa, (std::min)(m, n), ld, INF, is_a);
            break;
        case NO_EVT:
            testinghelpers::datagenerators::randomgenerators<T>( from, to, storage, m, n, mat, ld);
            break;
        default: ;
    }
}

template<typename T>
void test_trsm( char storage, char side, char uploa, char transa, char diaga,
    gtint_t m, gtint_t n, T alpha, gtint_t lda_inc, gtint_t ldb_inc, double thresh,
    EVT_TYPE a_init = NO_EVT, EVT_TYPE b_init = NO_EVT)
{
    gtint_t mn;
    testinghelpers::set_dim_with_side( side, m, n, &mn );
    gtint_t lda = testinghelpers::get_leading_dimension( storage, transa, mn, mn, lda_inc );
    gtint_t ldb = testinghelpers::get_leading_dimension( storage, 'n', m, n, ldb_inc );

    //----------------------------------------------------------
    //        Initialize matrics with random values.
    //----------------------------------------------------------
    gtint_t lower = (diaga = 'n')||(diaga = 'N') ? 3 : 0;
    gtint_t upper = (diaga = 'n')||(diaga = 'N') ? 10 : 1;
    std::vector<T> a( testinghelpers::matsize(storage, transa, mn, mn, lda) );
    std::vector<T> b( testinghelpers::matsize(storage, 'n', m, n, ldb) );
    srand(time(0));
    random_generator_with_INF_NAN( a.data(), uploa, storage, transa, lower, upper, mn, mn, lda, NO_EVT, true);
    random_generator_with_INF_NAN( b.data(), uploa, storage, 'n', 3, 10, m, n, ldb, b_init, false);

    // Make A matix diagonal dominant to make sure that algorithm doesn't diverge
    for ( dim_t a_dim = 0; a_dim < mn; ++a_dim )
    {
        a[a_dim + (a_dim* lda)] = a[a_dim + (a_dim* lda)] * T{10};
    }
    bool nan_inf_check = false;
    // Setting the nan_inf_check boolean to true if alpa has
    // Nan/Inf in it
    if constexpr (testinghelpers::type_info<T>::is_real)
    {
      nan_inf_check = (isnan(alpha) || isinf(alpha));
    }
    else
    {
      nan_inf_check = (isnan(alpha.real + alpha.imag) || isinf(alpha.real + alpha.imag));
    }
    nan_inf_check = ( nan_inf_check ||
                     ((a_init != NO_EVT) && (a_init != ZERO)) ||
                     ((b_init != NO_EVT) && (a_init != ZERO)) );

    // Create a copy of v so that we can check reference results.
    std::vector<T> b_ref(b);

    testinghelpers::make_triangular<T>( storage, uploa, mn, a.data(), lda );
    //----------------------------------------------------------
    //                  Call BLIS function
    //----------------------------------------------------------
    trsm<T>( storage, side, uploa, transa, diaga, m, n, &alpha, a.data(), lda, b.data(), ldb );

    //----------------------------------------------------------
    //                  Call reference implementation.
    //----------------------------------------------------------
    testinghelpers::ref_trsm<T>( storage, side, uploa, transa, diaga, m, n, alpha, a.data(),
                                                                     lda, b_ref.data(), ldb );

    //----------------------------------------------------------
    //              check component-wise error.
    //----------------------------------------------------------
    computediff<T>( storage, m, n, b.data(), b_ref.data(), ldb, thresh, nan_inf_check );
}
