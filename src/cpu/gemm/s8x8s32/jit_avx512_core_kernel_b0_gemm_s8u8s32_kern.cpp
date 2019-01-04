/*******************************************************************************
* Copyright 2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "jit_generator.hpp"
#include "common.hpp"

namespace mkldnn {
namespace impl {
namespace cpu {

jit_avx512_core_kernel_b0_gemm_s8u8s32_kern::jit_avx512_core_kernel_b0_gemm_s8u8s32_kern(): jit_generator(nullptr, GEMM_CODE_SIZE)
{
    if (mayiuse(avx512_core_vnni)) {
#include   KERNEL_B0_AVX512_VNNI
    } else {
#include   KERNEL_B0_AVX512
    }
}

}
}
}
