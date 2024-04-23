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
#include "test_scalv.h"

template <typename T>
class xscalv : public ::testing::Test {};
typedef ::testing::Types<float, double> TypeParam;
TYPED_TEST_SUITE(xscalv, TypeParam);

TYPED_TEST(xscalv, zero_alpha_x_fp)
{
    using T = TypeParam;
    gtint_t n = 10, incx = 1;
    std::vector<T> x(n);
    // Initialize x with random numbers.
    testinghelpers::datagenerators::randomgenerators<T>( -10, 10, n, incx, x.data() );
    std::vector<T> x_ref(x);
    T alpha = T{0};

    testinghelpers::ref_scalv<T, T>('n', n, alpha, x_ref.data(), incx);
    //----------------------------------------------------------
    //                  Call BLIS function.
    //----------------------------------------------------------
    scalv<T>('n', n, alpha, x.data(), incx);

    //----------------------------------------------------------
    //              Compute component-wise error.
    //----------------------------------------------------------
    // Set the threshold for the errors:
    // Check gtestsuite scalv.h or netlib source code for reminder of the
    // functionality from which we estimate operation count per element
    // of output, and hence the multipler for epsilon.
    double thresh;
    if (n == 0)
        thresh = 0.0;
    else if (alpha == testinghelpers::ZERO<T>() || alpha == testinghelpers::ONE<T>())
        thresh = 0.0;
    else
        thresh = testinghelpers::getEpsilon<T>();

    //----------------------------------------------------------
    //     Call generic test body using those parameters
    //----------------------------------------------------------
    computediff<T>( n, x.data(), x_ref.data(), incx, thresh, true );
}

TYPED_TEST(xscalv, zero_alpha_x_inf)
{
    using T = TypeParam;
    gtint_t n = 10, incx = 1;
    std::vector<T> x(n);
    // Initialize x with random numbers.
    testinghelpers::datagenerators::randomgenerators<T>( -10, 10, n, incx, x.data() );
    x[3] = 1.0/0.0;
    std::vector<T> x_ref(x);
    T alpha = T{0};
    testinghelpers::ref_scalv<T, T>('n', n, alpha, x_ref.data(), incx);

    //----------------------------------------------------------
    //                  Call BLIS function.
    //----------------------------------------------------------
    scalv<T>('n', n, alpha, x.data(), incx);

    //----------------------------------------------------------
    //              Compute component-wise error.
    //----------------------------------------------------------
    // Set the threshold for the errors:
    // Check gtestsuite scalv.h or netlib source code for reminder of the
    // functionality from which we estimate operation count per element
    // of output, and hence the multipler for epsilon.
    // No adjustment applied yet for complex data.
    double thresh;
    if (n == 0)
        thresh = 0.0;
    else if (alpha == testinghelpers::ZERO<T>() || alpha == testinghelpers::ONE<T>())
        thresh = 0.0;
    else
        thresh = testinghelpers::getEpsilon<T>();

    //----------------------------------------------------------
    //     Call generic test body using those parameters
    //----------------------------------------------------------
    computediff<T>( n, x.data(), x_ref.data(), incx, thresh, true );
}
