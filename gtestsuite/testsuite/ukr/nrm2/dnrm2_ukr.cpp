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

#include <gtest/gtest.h>
#include "test_nrm2_ukr.h"

using T = double;
using RT = typename testinghelpers::type_info<T>::real_type;

class dnrm2Ukr :
        public ::testing::TestWithParam<std::tuple<nrm2_ker_ft<T, RT>,  // Kernel pointer type
                                                   gtint_t,             // n
                                                   gtint_t,             // incx
                                                   bool>> {};           // is_memory_test

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(dnrm2Ukr);

TEST_P( dnrm2Ukr, AccuracyCheck )
{
    //----------------------------------------------------------
    // Initialize values from the parameters passed through
    // test suite instantiation (INSTANTIATE_TEST_SUITE_P).
    //----------------------------------------------------------
    nrm2_ker_ft<T, RT> ukr_fp = std::get<0>(GetParam());
    // vector length
    gtint_t n = std::get<1>(GetParam());
    // stride size for x
    gtint_t incx = std::get<2>(GetParam());
    // is_memory_test
    bool is_memory_test = std::get<3>(GetParam());

    // Set the threshold for the errors:
    double thresh = std::sqrt(n)*testinghelpers::getEpsilon<T>();

    //----------------------------------------------------------
    //     Call test body using these parameters
    //----------------------------------------------------------
    test_nrm2_ukr<T, RT>( ukr_fp, n, incx, thresh, is_memory_test );
}

// Prints the test case combination
class dnrm2UkrPrint {
public:
    std::string operator()(
        testing::TestParamInfo<std::tuple<nrm2_ker_ft<T, RT>, gtint_t, gtint_t, bool>> str) const {
        gtint_t n     = std::get<1>(str.param);
        gtint_t incx  = std::get<2>(str.param);
        bool is_memory_test = std::get<3>(str.param);

        std::string str_name = "_n_" + std::to_string(n);
        str_name += "_incx_" + testinghelpers::get_value_string(incx);
        str_name += ( is_memory_test ) ? "_mem_test_enabled" : "_mem_test_disabled";
        return str_name;
    }
};

#if defined(BLIS_KERNELS_ZEN) && defined(GTEST_AVX2FMA3)
/*
    Unit testing for functionality of bli_dnorm2fv_unb_var1_avx2 kernel.
    The code structure for bli_dnorm2fv_unb_var1_avx2( ... ) is as follows :
    For unit strides :
        Main loop    :  In blocks of 8 --> L8
        Fringe loops :  In blocks of 4 --> L4
                        Element-wise loop --> LScalar

    For non-unit strides : A single loop, to process element wise.
*/
// Unit testing with unit strides, across all loops.
INSTANTIATE_TEST_SUITE_P(
        bli_dnorm2fv_unb_var1_avx2_unitStrides,
        dnrm2Ukr,
        ::testing::Combine(
            ::testing::Values(bli_dnorm2fv_unb_var1_avx2), // ukr function
            // m size of vector
            ::testing::Values(// Testing the loops standalone
                              gtint_t(8),                  // size n, for L8
                              gtint_t(4),                  // L4
                              gtint_t(3),                  // 3(LScalar)
                              gtint_t(40),                 // 5*L8
                              gtint_t(43),                 // 5*L8 + 3(LScalar)
                              gtint_t(44),                 // 5*L8 + L4
                              gtint_t(47)),                // 5*L8 + L4 + 3(LScalar)
            ::testing::Values(gtint_t(1)),                 // stride size for x
            ::testing::Values(true, false)                 // is_memory_test
        ),
        ::dnrm2UkrPrint()
    );

// Unit testing with non-unit strides.
INSTANTIATE_TEST_SUITE_P(
        bli_dnorm2fv_unb_var1_avx2_nonUnitStrides,
        dnrm2Ukr,
        ::testing::Combine(
            ::testing::Values(bli_dnorm2fv_unb_var1_avx2), // ukr function
            // m size of vector
            ::testing::Values(// Testing the loops standalone
                              gtint_t(25),                 // n, size of the vector
                              gtint_t(41),
                              gtint_t(17),
                              gtint_t(9)),
            ::testing::Values(gtint_t(3), gtint_t(5)),     // stride size for x
            ::testing::Values(true, false)                 // is_memory_test
        ),
        ::dnrm2UkrPrint()
    );
#endif
