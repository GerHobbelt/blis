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
#include "test_her.h"

class zherTest :
        public ::testing::TestWithParam<std::tuple<char,
                                                   char,
                                                   char,
                                                   gtint_t,
                                                   double,
                                                   gtint_t,
                                                   gtint_t>> {};

TEST_P(zherTest, RandomData)
{
    using T = dcomplex;
    //----------------------------------------------------------
    // Initialize values from the parameters passed through
    // test suite instantiation (INSTANTIATE_TEST_SUITE_P).
    //----------------------------------------------------------
    // matrix storage format(row major, column major)
    char storage = std::get<0>(GetParam());
    // denotes whether matrix a is u,l
    char uploa = std::get<1>(GetParam());
    // denotes whether vector x is n,c
    char conjx = std::get<2>(GetParam());
    // matrix size n
    gtint_t n  = std::get<3>(GetParam());
    // specifies alpha value
    double alpha = std::get<4>(GetParam());
    // stride size for x:
    gtint_t incx = std::get<5>(GetParam());
    // lda increment.
    // If increment is zero, then the array size matches the matrix size.
    // If increment are nonnegative, the array size is bigger than the matrix size.
    gtint_t lda_inc = std::get<6>(GetParam());

    // Set the threshold for the errors:
    double thresh = 4*std::sqrt(n)*testinghelpers::getEpsilon<T>();

    //----------------------------------------------------------
    //     Call test body using these parameters
    //----------------------------------------------------------
    test_her<T, double>( storage, uploa, conjx, n, alpha, incx, lda_inc, thresh );
}

class zherTestPrint {
public:
    std::string operator()(
        testing::TestParamInfo<std::tuple<char,char,char,gtint_t,double,gtint_t,gtint_t>> str) const {
        char sfm       = std::get<0>(str.param);
        char uploa     = std::get<1>(str.param);
        char conjx     = std::get<2>(str.param);
        gtint_t n      = std::get<3>(str.param);
        double alpha   = std::get<4>(str.param);
        gtint_t incx   = std::get<5>(str.param);
        gtint_t ld_inc = std::get<6>(str.param);
#ifdef TEST_BLAS
        std::string str_name = "zher_";
#elif TEST_CBLAS
        std::string str_name = "cblas_zher";
#else  //#elif TEST_BLIS_TYPED
        std::string str_name = "blis_zher";
#endif
        str_name    = str_name + "_" + sfm;
        str_name    = str_name + "_" + uploa+conjx;
        str_name    = str_name + "_" + std::to_string(n);
        std::string incx_str = ( incx > 0) ? std::to_string(incx) : "m" + std::to_string(std::abs(incx));
        str_name    = str_name + "_" + incx_str;
        std::string alpha_str = ( alpha > 0) ? std::to_string(int(alpha)) : ("m" + std::to_string(int(std::abs(alpha))));
        str_name    = str_name + "_a" + alpha_str;
        str_name    = str_name + "_" + std::to_string(ld_inc);
        return str_name;
    }
};

// Black box testing.
INSTANTIATE_TEST_SUITE_P(
        Blackbox,
        zherTest,
        ::testing::Combine(
            ::testing::Values('c'
#ifndef TEST_BLAS
            ,'r'
#endif
            ),                                                               // storage format
            ::testing::Values('u','l'),                                      // uploa
            ::testing::Values('n'),                                          // conjx
            ::testing::Range(gtint_t(10), gtint_t(31), 10),                  // n
            ::testing::Values(1.0),                                          // alpha
            ::testing::Values(gtint_t(1)),                                   // stride size for x
            ::testing::Values(gtint_t(0), gtint_t(2))                        // increment to the leading dim of a
        ),
        ::zherTestPrint()
    );