/*******************************************************************************
* Copyright 2016-2018 Intel Corporation
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

#ifndef CPU_JIT_AVX512_CORE_X8S8S32X_CONV_KERNEL_HPP
#define CPU_JIT_AVX512_CORE_X8S8S32X_CONV_KERNEL_HPP

#include "c_types_map.hpp"
#include "memory_tracking.hpp"

#include "cpu_memory.hpp"

#include "jit_generator.hpp"
#include "jit_primitive_conf.hpp"
#include "jit_uni_eltwise.hpp"

namespace mkldnn {
namespace impl {
namespace cpu {

struct jit_avx512_core_x8s8s32x_fwd_kernel : public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_x8s8s32x_conv_fwd_ker_t)

    enum { STATE_FIRST_DST_LOAD = 0x1U };

    jit_avx512_core_x8s8s32x_fwd_kernel(jit_conv_conf_t ajcp,
            const primitive_attr_t &attr) : jcp(ajcp), attr_(attr),
            eltwise_injector_(nullptr)
    {
        if (jcp.with_eltwise)
            eltwise_injector_ = new jit_uni_eltwise_injector_f32<avx512_common>(
                this, jcp.eltwise);

        generate();
        jit_ker = (void (*)(jit_conv_call_s *))getCode();
    }

    ~jit_avx512_core_x8s8s32x_fwd_kernel() {
        delete eltwise_injector_;
    }

    static bool post_ops_ok(jit_conv_conf_t &jcp,
            const primitive_attr_t &attr);
    static status_t init_conf(jit_conv_conf_t &jcp,
            const convolution_desc_t &cd,
            cpu_memory_t::pd_t &src_pd,
            cpu_memory_t::pd_t &weights_pd,
            cpu_memory_t::pd_t &dst_pd,
            cpu_memory_t::pd_t &bias_pd,
            const primitive_attr_t &attr,
            int nthreads);
    static void init_scratchpad(memory_tracking::registrar_t &scratchpad,
            const jit_conv_conf_t &jcp, const primitive_attr_t &attr);

    jit_conv_conf_t jcp;
    const primitive_attr_t &attr_;
    void (*jit_ker)(jit_conv_call_s *);

private:
    jit_uni_eltwise_injector_f32<avx512_common> *eltwise_injector_;

    using reg64_t = const Xbyak::Reg64;
    using zmm_t = const Xbyak::Zmm;
    using xmm_t = const Xbyak::Xmm;
    enum {
        typesize = sizeof(float),
        ker_reg_base_idx = 28,
        ker_dw_reg_base_idx = 30,
    };
    typedef enum {
        no_last_block,
        last_ic_block,
        last_sp_block,
    } ic_block_t;

    /* data regs */
    reg64_t reg_ptr_scales = rax;
    reg64_t reg_inp = r8;
    reg64_t reg_ker = r9;
    reg64_t reg_out = r10;
    reg64_t aux_reg_inp = r11;
    reg64_t reg_ptr_sum_scale = r11;
    reg64_t aux_reg_ker = r12;
    reg64_t reg_compensation = r14;
    /* counter regs */
    reg64_t reg_bias_alpha = abi_not_param1;
    reg64_t reg_oi = rbx;
    reg64_t reg_bias = rdx;
    reg64_t reg_oc_blocks = rsi;
    reg64_t reg_owb = aux_reg_ker;
    reg64_t reg_scratch = reg_compensation;
    reg64_t reg_kj = reg_ptr_scales;
    reg64_t reg_overflow = reg_ptr_scales;
    reg64_t reg_icb = reg_bias;

    Xbyak::Opmask ktail_mask = Xbyak::Opmask(2);
    Xbyak::Opmask kblend_mask = Xbyak::Opmask(3);

    /* used during bias section of store_output */
    zmm_t zmm_permute = zmm_t(29); // only for fast path
    zmm_t zmm_comp = zmm_t(30); // only for signed input
    zmm_t zmm_bias = zmm_t(31);
    /* used during post_op sum section of store_output */
    zmm_t zmm_prev_dst = zmm_t(31);
    /* used during write-out section of store_output */
    zmm_t zmm_zero = zmm_t(31);

    /* used in compute_ker (but set during prepare_output) */
    zmm_t zmm_shift = zmm_t(30); // only for signed input
    /* used in compute_ker (but only for pre-VNNI machines) */
    zmm_t zmm_tmp = zmm_t(28);
    zmm_t zmm_one = zmm_t(29); // set at start of kernel
    /* used in compute_ker_dw */
    zmm_t zmm_zero_blend = zmm_t(28); // only for fast path
    zmm_t zmm_src = zmm_t(30);
    /* used in compute_ker and compute_ker_dw */
    zmm_t zmm_wei = zmm_t(31);

    zmm_t zmm_out(int i_ur, int i_oc) {
        int idx = i_ur + i_oc * jcp.ur_w;
        assert(idx < (jcp.is_depthwise
                    ? ker_dw_reg_base_idx : ker_reg_base_idx));
        return zmm_t(idx);
    }
    xmm_t xmm_out(int i_ur, int i_oc) {
        int idx = i_ur + i_oc * jcp.ur_w;
        assert(idx < (jcp.is_depthwise
                    ? ker_dw_reg_base_idx : ker_reg_base_idx));
        return xmm_t(idx);
    }
    zmm_t zmm_inp(int i_ic, int nb_x_blocking) {
        int idx = i_ic + nb_x_blocking * jcp.ur_w;
        assert(idx < 31);
        return zmm_t(idx);
    }
    zmm_t zmm_bias_alpha() {
        return zmm_t(jcp.nb_oc_blocking * jcp.ur_w);
    }
    xmm_t xmm_bias_alpha() {
        return xmm_t(jcp.nb_oc_blocking * jcp.ur_w);
    }
    int get_ow_start(int ki, int pad_l) {
        return nstl::max(0,
                utils::div_up(pad_l - ki * (jcp.dilate_w + 1), jcp.stride_w));
    }
    int get_ow_end(int ur_w, int ki, int pad_r) {
        return ur_w - nstl::max(0, utils::div_up(pad_r
                                                   - (jcp.kw - 1 - ki)
                                                           * (jcp.dilate_w + 1),
                                           jcp.stride_w));
    }

    bool maybe_eltwise(int position);
    void prepare_output(int ur_w);
    void store_output(int ur_w, bool last_oc_block_flag);
    void compute_ker_dw(
            int ur_w, int pad_l, int pad_r, ic_block_t last_ic_block_flag);
    void compute_ker(int ur_w, int pad_l, int pad_r,
            ic_block_t last_ic_block_flag, bool h_padded = false);
    void compute_eltwise(int ur_w);
    void kh_loop(int ur_w, int pad_l, int pad_r, ic_block_t last_ic_block_flag);
    void icb_loop(
            int ur_w, int pad_l, int pad_r, bool is_last_spatial_block);
    void generate();
    void cvt2ps(data_type_t type_in, zmm_t zmm_in, const Xbyak::Operand &op,
        bool mask_flag);
};

}
}
}

#endif
