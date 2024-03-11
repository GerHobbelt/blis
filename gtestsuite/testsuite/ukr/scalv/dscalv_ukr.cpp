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
#include "test_scalv_ukr.h"

class dscalvUkrTest :
        public ::testing::TestWithParam<std::tuple<dscalv_ker_ft,   // Function pointer for dscalv kernels
                                                   char,            // conj_alpha
                                                   gtint_t,         // n
                                                   gtint_t,         // incx
                                                   double,          // alpha
                                                   bool>> {};       // is_memory_test
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(dscalvUkrTest);

// Tests using random integers as vector elements.
TEST_P( dscalvUkrTest, FunctionalTest )
{
    using T = double;
    //----------------------------------------------------------
    // Initialize values from the parameters passed through
    // test suite instantiation (INSTANTIATE_TEST_SUITE_P).
    //----------------------------------------------------------
    // denotes the kernel to be tested:
    dscalv_ker_ft ukr = std::get<0>(GetParam());
    // denotes whether alpha or conj(alpha) will be used:
    char conj_alpha = std::get<1>(GetParam());
    // vector length:
    gtint_t n = std::get<2>(GetParam());
    // stride size for x:
    gtint_t incx = std::get<3>(GetParam());
    // alpha:
    T alpha = std::get<4>(GetParam());
    // is_memory_test:
    bool is_memory_test = std::get<5>(GetParam());

    // Set the threshold for the errors:
    double thresh = testinghelpers::getEpsilon<T>();
    //----------------------------------------------------------
    //     Call generic test body using those parameters
    //----------------------------------------------------------
    test_scalv_ukr<T, T, dscalv_ker_ft>( ukr, conj_alpha, n, incx, alpha, thresh, is_memory_test );
}

// Test-case logger : Used to print the test-case details.
class dscalvUkrTestPrint {
public:
    std::string operator()(
        testing::TestParamInfo<std::tuple<dscalv_ker_ft, char, gtint_t, gtint_t, double, bool>> str) const {
        char conjx = std::get<1>(str.param);
        gtint_t n = std::get<2>(str.param);
        gtint_t incx = std::get<3>(str.param);
        double alpha = std::get<4>(str.param);
        bool is_memory_test = std::get<5>(str.param);

        std::string str_name = "d";
        str_name += "_n" + std::to_string(n);
        str_name += (conjx == 'n') ? "_noconjx" : "_conjx";
        std::string incx_str = ( incx > 0) ? std::to_string(incx) : "m" + std::to_string(std::abs(incx));
        str_name += "_incx" + incx_str;
        str_name = str_name + "_alpha" + testinghelpers::get_value_string(alpha);
        str_name += ( is_memory_test ) ? "_mem_test_enabled" : "_mem_test_disabled";

        return str_name;
    }
};


// ----------------------------------------------
// ----- Begin ZEN1/2/3 (AVX2) Kernel Tests -----
// ----------------------------------------------
#if defined(BLIS_KERNELS_ZEN) && defined(GTEST_AVX2FMA3)
// Tests for bli_dscalv_zen_int (AVX2) kernel.
/**
 * Loops:
 * L16     - Main loop, handles 16 elements
 * LScalar - leftover loop (also handles non-unit increments)
*/
INSTANTIATE_TEST_SUITE_P(
        bli_dscalv_zen_int_unitPositiveStride,
        dscalvUkrTest,
        ::testing::Combine(
            ::testing::Values(bli_dscalv_zen_int),
            // conj(alpha): uses n (no_conjugate) since it is real.
            ::testing::Values('n'),
            // m: size of vector.
            ::testing::Values(
                                gtint_t(32),       // L16 (executed twice)
                                gtint_t(17),       // L16 + Ln_left
                                gtint_t(16),       // L16
                                gtint_t( 1)        // LScalar
            ),
            // incx: stride of x vector.
            ::testing::Values(
                                gtint_t(1)      // unit stride
            ),
            // alpha: value of scalar.
            ::testing::Values(
                                // @note: disabling alpha = 0 test for bli_dscalv_zen_int.
                                //  Segmentation Fault is being observed for alpha = 0 since the
                                //  kernel isn't handling the condition where cntx = NULL.
                                // double( 0.0),
                                double( 7.0),
                                double(-3.0)
            ),
            ::testing::Values(false, true)                 // is_memory_test
        ),
        ::dscalvUkrTestPrint()
    );

INSTANTIATE_TEST_SUITE_P(
        bli_dscalv_zen_int_nonUnitPositiveStrides,
        dscalvUkrTest,
        ::testing::Combine(
            ::testing::Values(bli_dscalv_zen_int),
            // conj(alpha): uses n (no_conjugate) since it is real.
            ::testing::Values('n'),
            // m: size of vector.
            ::testing::Values(
                                gtint_t(3), gtint_t(30), gtint_t(112)
            ),
            // incx: stride of x vector.
            ::testing::Values(
                                gtint_t(3), gtint_t(7)       // few non-unit strides for sanity check
            ),
            // alpha: value of scalar.
            ::testing::Values(
                                // @note: disabling alpha = 0 test for bli_dscalv_zen_int.
                                //  Segmentation Fault is being observed for alpha = 0 since the
                                //  kernel isn't handling the condition where cntx = NULL.
                                // double( 0.0),
                                double( 7.0),
                                double(-3.0)
            ),
            ::testing::Values(false, true)                 // is_memory_test
        ),
        ::dscalvUkrTestPrint()
    );

// Tests for bli_dscalv_zen_int10 (AVX2) kernel.
/**
 * Cases and Loops:
 * C0 L64     - Main loop, handles 64 elements
 * C0 L48     - handles 48 elements
 * C1 L32     - handles 32 elements
 * C2 L12     - handles 12 elements
 * C2 L4      - handles 4 elements
 * C2 LScalar - leftover loop
 *
 * LNUnit - loop for non-unit increments
*/
INSTANTIATE_TEST_SUITE_P(
        bli_dscalv_zen_int10_unitPositiveStride,
        dscalvUkrTest,
        ::testing::Combine(
            ::testing::Values(bli_dscalv_zen_int10),
            // conj(alpha): uses n (no_conjugate) since it is real.
            ::testing::Values('n'),
            // m: size of vector.
            ::testing::Values(
                                // testing case 0 (n > 500)
                                gtint_t(512),       // C0 L64
                                gtint_t(560),       // C0
                                gtint_t(544),       // C0 L0 + C1
                                gtint_t(572),       // C0 + C2 (L12)
                                gtint_t(564),       // C0 + C2 (L4)
                                gtint_t(573),       // C0 + C2 (L12 + LScalar)
                                gtint_t(565),       // C0 + C2 (L4  + LScalar)
                                gtint_t(561),       // C0 + C2 (LScalar)
                                gtint_t(556),       // C0 L64 + C1 + C2 (L12)
                                gtint_t(557),       // C0 L64 + C1 + C2 (L12 + LScalar)
                                gtint_t(548),       // C0 L64 + C1 + C2 (L4)
                                gtint_t(549),       // C0 L64 + C1 + C2 (L4 + LScalar)

                                // testing case 1 (200 < n < 500)
                                gtint_t(224),       // C1
                                gtint_t(236),       // C1 + C2 (L12)
                                gtint_t(240),       // C1 + C2 (L12 + L4)
                                gtint_t(241),       // C1 + C2 (L12 + L4 + LScalar)

                                // testing case 2 (n < 200)
                                gtint_t(12),        // C2 (L12)
                                gtint_t(16),        // C2 (L12 + L4)
                                gtint_t(17)         // C2 (L12 + L4 + LScalar)
            ),
            // incx: stride of x vector.
            ::testing::Values(
                                gtint_t(1)      // unit stride
            ),
            // alpha: value of scalar.
            ::testing::Values(
                                double( 0.0),
                                double( 7.0),
                                double(-3.0)
            ),
            ::testing::Values(false, true)                 // is_memory_test
        ),
        ::dscalvUkrTestPrint()
    );

INSTANTIATE_TEST_SUITE_P(
        bli_dscalv_zen_int10_nonUnitPositiveStrides,
        dscalvUkrTest,
        ::testing::Combine(
            ::testing::Values(bli_dscalv_zen_int10),
            // conj(alpha): uses n (no_conjugate) since it is real.
            ::testing::Values('n'),
            // m: size of vector.
            ::testing::Values(
                                gtint_t(3), gtint_t(30), gtint_t(112)
            ),
            // incx: stride of x vector.
            ::testing::Values(
                                gtint_t(3), gtint_t(7)       // few non-unit strides for sanity check
            ),
            // alpha: value of scalar.
            ::testing::Values(
                                double( 0.0),
                                double( 7.0),
                                double(-3.0)
            ),
            ::testing::Values(false, true)                 // is_memory_test
        ),
        ::dscalvUkrTestPrint()
    );
#endif
// ----------------------------------------------
// -----  End ZEN1/2/3 (AVX2) Kernel Tests  -----
// ----------------------------------------------


// ----------------------------------------------
// -----  Begin ZEN4 (AVX512) Kernel Tests  -----
// ----------------------------------------------
#if defined(BLIS_KERNELS_ZEN4) && defined(GTEST_AVX512)
// Tests for bli_dscalv_zen_int_avx512 (AVX512) kernel.
/**
 * Loops:
 * L64     - Main loop, handles 64 elements
 * L32     - handles 32 elements
 * L16     - handles 16 elements
 * L8      - handles 8 elements
 * L4      - handles 4 elements
 * L2      - handles 2 elements
 * LScalar - leftover loop (also handles non-unit increments)
*/
INSTANTIATE_TEST_SUITE_P(
        bli_dscalv_zen_int_avx512_unitPositiveStride,
        dscalvUkrTest,
        ::testing::Combine(
            ::testing::Values(bli_dscalv_zen_int_avx512),
            // conj(alpha): uses n (no_conjugate) since it is real.
            ::testing::Values('n'),
            // m: size of vector.
            ::testing::Values(
                                // testing each loop individually
                                gtint_t(128),        // L64 (executed twice)
                                gtint_t( 64),        // L64
                                gtint_t( 32),        // L32
                                gtint_t( 16),        // L16
                                gtint_t(  8),        // L8
                                gtint_t(  4),        // L4
                                gtint_t(  2),        // L2
                                gtint_t(  1),        // LScalar

                                // testing all loops from top to bottom
                                gtint_t(123),       // L64 to LScalar
                                gtint_t(126),       // L64 to L2
                                gtint_t(124),       // L64 to L4
                                gtint_t(120),       // L64 to L8
                                gtint_t(112),       // L64 to L16
                                gtint_t( 96),       // L64 to L32

                                gtint_t( 63),       // L32 to LScalar
                                gtint_t( 62),       // L32 to L2
                                gtint_t( 60),       // L32 to L4
                                gtint_t( 56),       // L32 to L8
                                gtint_t( 48),       // L32 to L16

                                gtint_t( 31),       // L16 - LScalar
                                gtint_t( 30),       // L16 - L2
                                gtint_t( 28),       // L16 - L4
                                gtint_t( 24),       // L16 - L8

                                gtint_t( 15),       // L8 to LScalar
                                gtint_t( 14),       // L8 to L2
                                gtint_t( 12),       // L8 to L4

                                gtint_t(  7),       // L4 to LScalar
                                gtint_t(  6),       // L4 to L2

                                gtint_t(  3)        // L2 to LScalar
            ),
            // incx: stride of x vector.
            ::testing::Values(
                                gtint_t(1)      // unit stride
            ),
            // alpha: value of scalar.
            ::testing::Values(
                                double( 0.0),
                                double( 7.0),
                                double(-3.0)
            ),
            ::testing::Values(false, true)                 // is_memory_test
        ),
        ::dscalvUkrTestPrint()
    );

INSTANTIATE_TEST_SUITE_P(
        bli_dscalv_zen_int_avx512_nonUnitPositiveStrides,
        dscalvUkrTest,
        ::testing::Combine(
            ::testing::Values(bli_dscalv_zen_int_avx512),
            // conj(alpha): uses n (no_conjugate) since it is real.
            ::testing::Values('n'),
            // m: size of vector.
            ::testing::Values(
                                gtint_t(3), gtint_t(30), gtint_t(112)
            ),
            // incx: stride of x vector.
            ::testing::Values(
                                gtint_t(3), gtint_t(7)       // few non-unit strides for sanity check
            ),
            // alpha: value of scalar.
            ::testing::Values(
                                double( 0.0),
                                double( 7.0),
                                double(-3.0)
            ),
            ::testing::Values(false, true)                 // is_memory_test
        ),
        ::dscalvUkrTestPrint()
    );
#endif
// ----------------------------------------------
// -----   End ZEN4 (AVX512) Kernel Tests   -----
// ----------------------------------------------
