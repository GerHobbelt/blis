/*

   BLIS
   An object-based framework for developing high-performance BLAS-like
   libraries.

   Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.

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
#include "test_axpbyv.h"

class zaxpbyvGenericTest :
        public ::testing::TestWithParam<std::tuple<char,
                                                   gtint_t,
                                                   gtint_t,
                                                   gtint_t,
                                                   dcomplex,
                                                   dcomplex>> {};
// Tests using random integers as vector elements.
TEST_P( zaxpbyvGenericTest, RandomData )
{
    using T = dcomplex;
    //----------------------------------------------------------
    // Initialize values from the parameters passed through
    // test suite instantiation (INSTANTIATE_TEST_SUITE_P).
    //----------------------------------------------------------
    // denotes whether x or conj(x) will be added to y:
    char conj_x = std::get<0>(GetParam());
    // vector length:
    gtint_t n = std::get<1>(GetParam());
    // stride size for x:
    gtint_t incx = std::get<2>(GetParam());
    // stride size for y:
    gtint_t incy = std::get<3>(GetParam());
    // alpha
    T alpha = std::get<4>(GetParam());
    // beta
    T beta = std::get<5>(GetParam());

    // Set the threshold for the errors:
    double thresh = 20*testinghelpers::getEpsilon<T>();

    //----------------------------------------------------------
    //     Call generic test body using those parameters
    //----------------------------------------------------------
    test_axpbyv<T>( conj_x, n, incx, incy, alpha, beta, thresh );
}

// Used to generate a test case with a sensible name.
// Beware that we cannot use fp numbers (e.g., 2.3) in the names,
// so we are only printing int(2.3). This should be enough for debugging purposes.
// If this poses an issue, please reach out.
class zaxpbyvGenericTestPrint {
public:
    std::string operator()(
        testing::TestParamInfo<std::tuple<char,gtint_t,gtint_t,gtint_t,dcomplex,dcomplex>> str) const {
        char conj      = std::get<0>(str.param);
        gtint_t n      = std::get<1>(str.param);
        gtint_t incx   = std::get<2>(str.param);
        gtint_t incy   = std::get<3>(str.param);
        dcomplex alpha = std::get<4>(str.param);
        dcomplex beta  = std::get<5>(str.param);
#ifdef TEST_BLAS
        std::string str_name = "zaxpby_";
#elif TEST_CBLAS
        std::string str_name = "cblas_zaxpby";
#else  //#elif TEST_BLIS_TYPED
        std::string str_name = "bli_zaxpbyv";
#endif
        str_name += "_" + std::to_string(n);
        str_name += "_" + std::string(&conj, 1);
        std::string incx_str = ( incx > 0) ? std::to_string(incx) : "m" + std::to_string(std::abs(incx));
        str_name += "_" + incx_str;
        std::string incy_str = ( incy > 0) ? std::to_string(incy) : "m" + std::to_string(std::abs(incy));
        str_name += "_" + incy_str;
        std::string alpha_str = ( alpha.real > 0) ? std::to_string(int(alpha.real)) : ("m" + std::to_string(int(std::abs(alpha.real))));
                    alpha_str = alpha_str + "pi" + (( alpha.imag > 0) ? std::to_string(int(alpha.imag)) : ("m" + std::to_string(int(std::abs(alpha.imag)))));
        std::string beta_str = ( beta.real > 0) ? std::to_string(int(beta.real)) : ("m" + std::to_string(int(std::abs(beta.real))));
                    beta_str = beta_str + "pi" + (( beta.imag > 0) ? std::to_string(int(beta.imag)) : ("m" + std::to_string(int(std::abs(beta.imag)))));
        str_name = str_name + "_a" + alpha_str;
        str_name = str_name + "_b" + beta_str;
        return str_name;
    }
};

// Black box testing for generic and main use of zaxpby.
INSTANTIATE_TEST_SUITE_P(
        Blackbox,
        zaxpbyvGenericTest,
        ::testing::Combine(
            ::testing::Values('n'                                            // n: use x, c: use conj(x)
#ifdef TEST_BLIS_TYPED
            , 'c'                                                            // this option is BLIS-api specific.
#endif
            ),
            ::testing::Range(gtint_t(10), gtint_t(101), 10),                 // m size of vector takes values from 10 to 100 with step size of 10.
            ::testing::Values(gtint_t(1)),                                   // stride size for x
            ::testing::Values(gtint_t(1)),                                   // stride size for y
            ::testing::Values(dcomplex{-3.0, 1.0}, dcomplex{1.0, 2.0}),      // alpha
            ::testing::Values(dcomplex{1.0, 2.0})                            // beta
        ),
        ::zaxpbyvGenericTestPrint()
    );

// Test for non-unit increments.
// Only test very few cases as sanity check.
// We can modify the values using implementantion details.
INSTANTIATE_TEST_SUITE_P(
        NonUnitPositiveIncrements,
        zaxpbyvGenericTest,
        ::testing::Combine(
            ::testing::Values('n'
#ifdef TEST_BLIS_TYPED
            , 'c'                                                            // this option is BLIS-api specific.
#endif
            ),                                                               // n: use x, c: use conj(x)
            ::testing::Range(gtint_t(10), gtint_t(31), 10),                  // m size of vector takes values from 10 to 100 with step size of 10.
            ::testing::Values(gtint_t(2)),                                   /*(gtint_t(-5), gtint_t(-17))*/// stride size for x
            ::testing::Values(gtint_t(4)),                                   /*(gtint_t(-12), gtint_t(-4))*/// stride size for y
            ::testing::Values(dcomplex{4.0, 3.1}),                           // alpha
            ::testing::Values(dcomplex{1.0, 2.0})                            // beta
        ),
        ::zaxpbyvGenericTestPrint()
    );

#ifndef TEST_BLIS_TYPED
// Test for negative increments.
// Only test very few cases as sanity check.
// We can modify the values using implementantion details.
INSTANTIATE_TEST_SUITE_P(
        NegativeIncrements,
        zaxpbyvGenericTest,
        ::testing::Combine(
            ::testing::Values('n'),                                          // n: use x, c: use conj(x)
            ::testing::Range(gtint_t(10), gtint_t(31), 10),                  // m size of vector takes values from 10 to 100 with step size of 10.
            ::testing::Values(gtint_t(11), gtint_t(-11)),                    // stride size for x
            ::testing::Values(gtint_t(-3), gtint_t(4)),                      // stride size for y
            ::testing::Values(dcomplex{4.0, 3.1}),                           // alpha
            ::testing::Values(dcomplex{1.0, -2.0})                           // beta
        ),
        ::zaxpbyvGenericTestPrint()
    );
#endif