// Copyright 2019 Tencent
// SPDX-License-Identifier: BSD-3-Clause

#include "tanh_x86.h"

#include "x86_activation.h"

namespace ncnn {

TanH_x86::TanH_x86()
{
#if __SSE2__
    support_packing = true;
#endif // __SSE2__
}

int TanH_x86::forward_inplace(Mat& bottom_top_blob, const Option& opt) const
{
    int w = bottom_top_blob.w;
    int h = bottom_top_blob.h;
    int d = bottom_top_blob.d;
    int channels = bottom_top_blob.c;
    int elempack = bottom_top_blob.elempack;
    int size = w * h * d * elempack;

    #pragma omp parallel for num_threads(opt.num_threads)
    for (int q = 0; q < channels; q++)
    {
        float* ptr = bottom_top_blob.channel(q);

        int i = 0;
#if __AVX512F__
        for (; i + 15 < size; i += 16)
        {
            __m512 _p = _mm512_loadu_ps(ptr);
            _p = tanh_avx512(_p);
            _mm512_storeu_ps(ptr, _p);
            ptr += 16;
        }
        if (i < size)
        {
            const unsigned int remain = size - i;
            __mmask16 _mask = (__mmask16)((1u << remain) - 1);
            __m512 _p = _mm512_maskz_loadu_ps(_mask, ptr);
            _p = tanh_avx512(_p);
            _mm512_mask_storeu_ps(ptr, _mask, _p);
        }
#else // __AVX512F__
#if __SSE2__
#if __AVX__
        for (; i + 7 < size; i += 8)
        {
            __m256 _p = _mm256_loadu_ps(ptr);
            _p = tanh_avx(_p);
            _mm256_storeu_ps(ptr, _p);
            ptr += 8;
        }
#endif // __AVX__
        for (; i + 3 < size; i += 4)
        {
            __m128 _p = _mm_loadu_ps(ptr);
            _p = tanh_sse(_p);
            _mm_storeu_ps(ptr, _p);
            ptr += 4;
        }
#endif // __SSE2__
        for (; i < size; i++)
        {
            *ptr = tanhf(*ptr);
            ptr++;
        }
#endif // __AVX512F__
    }

    return 0;
}

} // namespace ncnn
