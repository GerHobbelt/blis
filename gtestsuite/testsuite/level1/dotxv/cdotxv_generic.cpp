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
#include "test_dotxv.h"

class cdotxvGenericTest :
        public ::testing::TestWithParam<std::tuple<gtint_t, char, char, gtint_t, gtint_t, scomplex, scomplex>> {};

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(cdotxvGenericTest);

// Tests using random integers as vector elements.
TEST_P( cdotxvGenericTest, RandomData )
{
    using T = scomplex;
    //----------------------------------------------------------
    // Initialize values from the parameters passed through
    // test suite instantiation (INSTANTIATE_TEST_SUITE_P).
    //----------------------------------------------------------
    // vector length:
    gtint_t n = std::get<0>(GetParam());
    // denotes whether vec x is n,c
    char conj_x = std::get<1>(GetParam());
    // denotes whether vec y is n,c
    char conj_y = std::get<2>(GetParam());
    // stride size for x:
    gtint_t incx = std::get<3>(GetParam());
    // stride size for y:
    gtint_t incy = std::get<4>(GetParam());
    // alpha
    T alpha = std::get<5>(GetParam());
    // beta
    T beta  = std::get<6>(GetParam());

    // Set the threshold for the errors:
    // Check gtestsuite dotxv.h (no netlib version) for reminder of the
    // functionality from which we estimate operation count per element
    // of output, and hence the multipler for epsilon.
    // No adjustment applied yet for complex data.
    double thresh;
    if (n == 0)
        thresh = 0.0;
    else if (alpha == testinghelpers::ZERO<T>())
    {
        // Like SCALV (for one element)
        if (beta == testinghelpers::ZERO<T>() || beta == testinghelpers::ONE<T>())
            thresh = 0.0;
        else
            thresh = testinghelpers::getEpsilon<T>();
    }
    else if (beta == testinghelpers::ZERO<T>())
    {
        // Like DOTV but with alpha scaling
        if (alpha == testinghelpers::ONE<T>())
            thresh = (2*n)*testinghelpers::getEpsilon<T>();
        else
            thresh = (3*n)*testinghelpers::getEpsilon<T>();
    }
    else if (beta == testinghelpers::ONE<T>())
    {
        if (alpha == testinghelpers::ONE<T>())
            thresh = (2*n+1)*testinghelpers::getEpsilon<T>();
        else
            thresh = (3*n+1)*testinghelpers::getEpsilon<T>();
    }
    else if (alpha == testinghelpers::ONE<T>())
        thresh = (2*n+2)*testinghelpers::getEpsilon<T>();
    else
        thresh = (3*n+2)*testinghelpers::getEpsilon<T>();

    //----------------------------------------------------------
    //     Call generic test body using those parameters
    //----------------------------------------------------------
    test_dotxv<T>( n, conj_x, conj_y, alpha, incx, incy, beta, thresh );
}

// Used to generate a test case with a sensible name.
// Beware that we cannot use fp numbers (e.g., 2.3) in the names,
// so we are only printing int(2.3). This should be enough for debugging purposes.
// If this poses an issue, please reach out.
class cdotxvGenericTestPrint {
public:
    std::string operator()(
        testing::TestParamInfo<std::tuple<gtint_t,char,char,gtint_t,gtint_t,scomplex,scomplex>> str) const {
        gtint_t n      = std::get<0>(str.param);
        char conjx     = std::get<1>(str.param);
        char conjy     = std::get<2>(str.param);
        gtint_t incx   = std::get<3>(str.param);
        gtint_t incy   = std::get<4>(str.param);
        scomplex alpha = std::get<5>(str.param);
        scomplex beta  = std::get<6>(str.param);
        std::string str_name = "bli_cdotxv";
        str_name += "_n_" + std::to_string(n);
        str_name += "_" + std::string(&conjx, 1);
        str_name += "_" + std::string(&conjy, 1);
        str_name += "_incx_" + testinghelpers::get_value_string(incx);
        str_name += "_incy_" + testinghelpers::get_value_string(incy);
        str_name += "_alpha_" + testinghelpers::get_value_string(alpha);
        str_name += "_beta_" + testinghelpers::get_value_string(beta);
        return str_name;
    }
};

#ifdef TEST_BLIS_TYPED
// Black box testing for generic and main use of cdotxv.
INSTANTIATE_TEST_SUITE_P(
        Blackbox,
        cdotxvGenericTest,
        ::testing::Combine(
            ::testing::Range(gtint_t(10), gtint_t(101), 10),                 // m size of vector takes values from 10 to 100 with step size of 10.
            ::testing::Values('n', 'c'),                                     // n: use x, c: use conj(x)
            ::testing::Values('n', 'c'),                                     // n: use y, c: use conj(y)
            ::testing::Values(gtint_t(1)),                                   // stride size for x
            ::testing::Values(gtint_t(1)),                                   // stride size for y
            ::testing::Values(scomplex{1.0, -1.0}),                          // alpha
            ::testing::Values(scomplex{-1.0, 1.0})                           // beta
        ),
        ::cdotxvGenericTestPrint()
    );

// Black box testing for generic and main use of cdotxv.
INSTANTIATE_TEST_SUITE_P(
        SmallSizesBlackbox,
        cdotxvGenericTest,
        ::testing::Combine(
            ::testing::Range(gtint_t(1), gtint_t(11), 1),                    // m size of vector takes values from 10 to 100 with step size of 10.
            ::testing::Values('n', 'c'),                                     // n: use x, c: use conj(x)
            ::testing::Values('n', 'c'),                                     // n: use y, c: use conj(y)
            ::testing::Values(gtint_t(1)),                                   // stride size for x
            ::testing::Values(gtint_t(1)),                                   // stride size for y
            ::testing::Values(scomplex{1.0, -1.0}),                          // alpha
            ::testing::Values(scomplex{-1.0, 1.0})                           // beta
        ),
        ::cdotxvGenericTestPrint()
    );

// Test for non-unit increments.
// Only test very few cases as sanity check.
// We can modify the values using implementantion details.
INSTANTIATE_TEST_SUITE_P(
        NonUnitIncrements,
        cdotxvGenericTest,
        ::testing::Combine(
            ::testing::Values(gtint_t(3), gtint_t(30), gtint_t(112)),        // m size of vector
            ::testing::Values('n', 'c'),                                     // n: use x, c: use conj(x)
            ::testing::Values('n', 'c'),                                     // n: use y, c: use conj(y)
            ::testing::Values(gtint_t(2), gtint_t(11)),                      // stride size for x
            ::testing::Values(gtint_t(3), gtint_t(33)),                      // stride size for y
            ::testing::Values(scomplex{1.0, -1.0}),                          // alpha
            ::testing::Values(scomplex{-1.0, 1.0})                           // beta
        ),
        ::cdotxvGenericTestPrint()
    );
#endif
