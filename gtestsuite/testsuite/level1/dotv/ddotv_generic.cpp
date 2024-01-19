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

#include <gtest/gtest.h>
#include "test_dotv.h"

class ddotvGenericTest :
        public ::testing::TestWithParam<std::tuple<char,
                                                   char,
                                                   gtint_t,
                                                   gtint_t,
                                                   gtint_t>> {};

// Tests using random integers as vector elements.
TEST_P( ddotvGenericTest, RandomData )
{
    using T = double;
    //----------------------------------------------------------
    // Initialize values from the parameters passed through
    // test suite instantiation (INSTANTIATE_TEST_SUITE_P).
    //----------------------------------------------------------
    // denotes whether vec x is n,c
    char conjx = std::get<0>(GetParam());
    // denotes whether vec y is n,c
    char conjy = std::get<1>(GetParam());
    // vector length:
    gtint_t n = std::get<2>(GetParam());
    // stride size for x:
    gtint_t incx = std::get<3>(GetParam());
    // stride size for y:
    gtint_t incy = std::get<4>(GetParam());

    // Set the threshold for the errors:
    double thresh = n*testinghelpers::getEpsilon<T>();

    //----------------------------------------------------------
    //     Call generic test body using those parameters
    //----------------------------------------------------------
    test_dotv<T>( conjx, conjy, n, incx, incy, thresh );
}

// Used to generate a test case with a sensible name.
// Beware that we cannot use fp numbers (e.g., 2.3) in the names,
// so we are only printing int(2.3). This should be enough for debugging purposes.
// If this poses an issue, please reach out.
class ddotvGenericTestPrint {
public:
    std::string operator()(
        testing::TestParamInfo<std::tuple<char,char,gtint_t,gtint_t,gtint_t>> str) const {
        char conjx    = std::get<0>(str.param);
        char conjy    = std::get<1>(str.param);
        gtint_t n     = std::get<2>(str.param);
        gtint_t incx  = std::get<3>(str.param);
        gtint_t incy  = std::get<4>(str.param);
#ifdef TEST_BLAS
        std::string str_name = "ddot_";
#elif TEST_CBLAS
        std::string str_name = "cblas_ddot";
#else  //#elif TEST_BLIS_TYPED
        std::string str_name = "bli_ddotv";
#endif
        str_name += "_n" + std::to_string(n);
        str_name += (conjx == 'n') ? "_noconjx" : "_conjx";
        str_name += (conjy == 'n') ? "_noconjy" : "_conjy";
        std::string incx_str = ( incx > 0) ? std::to_string(incx) : "m" + std::to_string(std::abs(incx));
        str_name += "_incx" + incx_str;
        std::string incy_str = ( incy > 0) ? std::to_string(incy) : "m" + std::to_string(std::abs(incy));
        str_name += "_incy" + incy_str;
        return str_name;
    }
};

// Black box testing for generic use of ddot.
INSTANTIATE_TEST_SUITE_P(
        unitPositiveStride,
        ddotvGenericTest,
        ::testing::Combine(
            // conj(x): user n (no_conjugate) since it is real.
            ::testing::Values('n'),
            // conj(y): user n (no_conjugate) since it is real.
            ::testing::Values('n'),
            // m: size of vector.
            ::testing::Range(gtint_t(10), gtint_t(101), 10),
            // incx: stride of x vector.
            ::testing::Values(gtint_t(1)),          // unit stride
            // incy: stride of y vector.
            ::testing::Values(gtint_t(1))           // unit stride
        ),
        ::ddotvGenericTestPrint()
    );

#ifdef TEST_BLIS_TYPED // BLIS-api specific
// Test when conjugate of x is used as an argument.
// Only test very few cases as sanity check since conj(x) = x for real types.
// We can modify the values using implementantion details.
INSTANTIATE_TEST_SUITE_P(
        ConjX,
        ddotvGenericTest,
        ::testing::Combine(
            ::testing::Values('c'),                                          // c: use conj(x)
            ::testing::Values('c'),                                          // c: use conj(y)
            ::testing::Values(gtint_t(3), gtint_t(30), gtint_t(112)),        // m size of vector
            ::testing::Values(gtint_t(1)),                                   // stride size for x
            ::testing::Values(gtint_t(1))                                    // stride size for y
        ),
        ::ddotvGenericTestPrint()
    );
#endif

// Test for non-unit increments.
// Only test very few cases as sanity check.
// We can modify the values using implementantion details.
INSTANTIATE_TEST_SUITE_P(
        nonUnitPositiveStrides,
        ddotvGenericTest,
        ::testing::Combine(
            // conj(x): user n (no_conjugate) since it is real.
            ::testing::Values('n'),
            // conj(y): user n (no_conjugate) since it is real.
            ::testing::Values('n'),
            // m: size of vector.
            ::testing::Range(gtint_t(10), gtint_t(101), 10),
            // incx: stride of x vector.
            ::testing::Values(
                               gtint_t(3), gtint_t(7)   // few non-unit positive strides for sanity check
            ),
            // incy: stride of y vector.
            ::testing::Values(
                               gtint_t(3), gtint_t(7)   // few non-unit positive strides for sanity check
            )
        ),
        ::ddotvGenericTestPrint()
    );

#ifndef TEST_BLIS_TYPED
// Test for negative increments.
// Only test very few cases as sanity check.
// We can modify the values using implementantion details.
INSTANTIATE_TEST_SUITE_P(
        negativeStrides,
        ddotvGenericTest,
        ::testing::Combine(
            // conj(x): user n (no_conjugate) since it is real.
            ::testing::Values('n'),
            // conj(y): user n (no_conjugate) since it is real.
            ::testing::Values('n'),
            // m: size of vector.
            ::testing::Range(gtint_t(10), gtint_t(101), 10),
            // incx: stride of x vector.
            ::testing::Values(
                               gtint_t(-1), gtint_t(-3), gtint_t(-7)   // few non-unit negative strides for sanity check
            ),
            // incy: stride of y vector.
            ::testing::Values(
                               gtint_t(-1), gtint_t(-3), gtint_t(-7)   // few non-unit negative strides for sanity check
            )
        ),
        ::ddotvGenericTestPrint()
    );
#endif

#if defined(BLIS_ENABLE_OPENMP) && defined(AOCL_DYNAMIC)
INSTANTIATE_TEST_SUITE_P(
        AOCLDynamicThresholds,
        ddotvGenericTest,
        ::testing::Combine(
            // conj(x): user n (no_conjugate) since it is real.
            ::testing::Values('n'),
            // conj(y): user n (no_conjugate) since it is real.
            ::testing::Values('n'),
            // m: size of vector.
            ::testing::Values(
                               gtint_t(  2500),     // nt_ideal = 1
                               gtint_t(  5000),     // nt_ideal = 4
                               gtint_t( 15000),     // nt_ideal = 8
                               gtint_t( 40000),     // nt_ideal = 16
                               gtint_t(200000),     // nt_ideal = 32
                               gtint_t(250000)      // nt_ideal = max_available
            ),
            // incx: stride of x vector.
            ::testing::Values(
                               gtint_t(1)           // unit stride
            ),
            // incy: stride of y vector.
            ::testing::Values(
                               gtint_t(1)           // unit stride
            )
        ),
        ::ddotvGenericTestPrint()
    );
#endif