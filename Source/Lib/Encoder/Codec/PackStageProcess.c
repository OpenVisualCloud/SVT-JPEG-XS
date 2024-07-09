/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <stdlib.h>
#include <string.h>

#include "EncHandle.h"
#include "PictureControlSet.h"
#include "Encoder.h"
#include "Threads/SystemResourceManager.h"
#include "SvtLog.h"
#include "Threads/SvtObject.h"
#include "PackOut.h"
#include "PackStageProcess.h"
#include "SvtUtility.h"
#include "common_dsp_rtcd.h"
#include "QuantStageProcess.h"
#include "PreRcStageProcess.h"
#include "RateControl.h"
#include "PrecinctEnc.h"
#include "BitstreamWriter.h"
#include "PackPrecinct.h"
#include "PackHeaders.h"
#include "Codestream.h"
#include "GcStageProcess.h"
#include "PackIn.h"
#include "Threads/SvtThreads.h"

#define PRINT_BUDGET 0

/*Values for tuning Rate Control, can be dynamic in feature.*/
#define TUNING_RC_CBR_PER_PRECINCT_MOVE_PADDING_FIRST_PREC_BIGGER_NO_SIGN_LAZY_PERCENT   (10)
#define TUNING_RC_CBR_PER_PRECINCT_MOVE_PADDING_FIRST_PREC_BIGGER_WITH_SIGN_LAZY_PERCENT (20)
#define TUNING_RC_CBR_PER_SLICE_MAX_PRECINCT_BUDGET_RATE                                 (4)

typedef struct PackStageContext {
    svt_jpeg_xs_encoder_common_t* enc_common;
    Fifo_t* input_buffer_fifo_ptr;
    Fifo_t* output_buffer_fifo_ptr;
    uint32_t num_alloc_precincts_per_thread;
    precinct_enc_t* temp_precincts_in_slice;

    struct precinct_calc_dwt_buff_tmp buffers_dwt_tmp;                     //Only for profile Latency
    struct precinct_calc_dwt_buff_per_component buffers_dwt_per_component; //Only for profile Latency
} PackStageContext;

static void pack_stage_context_dctor(void_ptr p) {
    ThreadContext_t* thread_contxt_ptr = (ThreadContext_t*)p;
    if (thread_contxt_ptr->priv) {
        PackStageContext* obj = (PackStageContext*)thread_contxt_ptr->priv;
        svt_jpeg_xs_encoder_common_t* enc_common = obj->enc_common;
        pi_t* pi = &enc_common->pi;

        if (obj->temp_precincts_in_slice) {
            for (uint32_t i = 0; i < obj->num_alloc_precincts_per_thread; ++i) {
                precinct_enc_t* precincts = &obj->temp_precincts_in_slice[i];
                for (uint32_t c = 0; c < pi->comps_num; ++c) {
                    if (pi->components[c].decom_v == 0 || enc_common->cpu_profile == CPU_PROFILE_LOW_LATENCY) {
                        SVT_FREE_ALIGNED_ARRAY(precincts->coeff_buff_ptr_16bit[c]);
                    }
                    SVT_FREE_ALIGNED_ARRAY(precincts->gc_buff_ptr[c]);
                    if (enc_common->coding_significance) {
                        SVT_FREE_ALIGNED_ARRAY(precincts->gc_significance_buff_ptr[c]);
                    }
                    if (enc_common->coding_vertical_prediction_mode) {
                        SVT_FREE_ALIGNED_ARRAY(precincts->vped_bit_pack_buff_ptr[c]);
                        if (enc_common->coding_significance) {
                            SVT_FREE_ALIGNED_ARRAY(precincts->vped_significance_ptr[c]);
                        }
                    }
                }
            }
        }

        if (obj->buffers_dwt_tmp.buffer_tmp) {
            SVT_FREE(obj->buffers_dwt_tmp.buffer_tmp);
        }

        if (obj->buffers_dwt_tmp.buffer_unpacked_color_formats) {
            SVT_FREE(obj->buffers_dwt_tmp.buffer_unpacked_color_formats);
        }

        uint8_t decom_V1_exist = 0;
        uint8_t decom_V2_exist = 0;
        for (uint32_t i = 0; i < pi->comps_num; ++i) {
            if (pi->components[i].decom_v == 1) {
                decom_V1_exist = 1;
            }
            else if (pi->components[i].decom_v == 2) {
                decom_V2_exist = 1;
            }
        }

        if (enc_common->cpu_profile == CPU_PROFILE_LOW_LATENCY) {
            if (decom_V1_exist) {
                SVT_FREE(obj->buffers_dwt_tmp.V1.line_1);
                SVT_FREE(obj->buffers_dwt_tmp.V1.line_2);
                SVT_FREE(obj->buffers_dwt_tmp.V1.out_tmp_line_HF_next);
            }
            if (decom_V2_exist) {
                SVT_FREE(obj->buffers_dwt_tmp.V2.line_3);
                SVT_FREE(obj->buffers_dwt_tmp.V2.line_4);
                SVT_FREE(obj->buffers_dwt_tmp.V2.line_5);
                SVT_FREE(obj->buffers_dwt_tmp.V2.line_6);
                SVT_FREE(obj->buffers_dwt_tmp.V2.buffer_next);
            }
        }
        if (enc_common->cpu_profile == CPU_PROFILE_LOW_LATENCY) {
            buffers_components_free(&obj->buffers_dwt_per_component);
        }

        SVT_FREE(obj->temp_precincts_in_slice);
        SVT_FREE_ARRAY(obj);
    }
}

/************************************************
 * Pack Context Constructor
 ************************************************/
SvtJxsErrorType_t pack_stage_context_ctor(ThreadContext_t* thread_contxt_ptr, svt_jpeg_xs_encoder_api_prv_t* enc_api_prv,
                                          int idx) {
    SvtJxsErrorType_t error;
    PackStageContext* context_ptr;
    svt_jpeg_xs_encoder_common_t* enc_common = &enc_api_prv->enc_common;
    pi_t* pi = &enc_common->pi;
    pi_enc_t* pi_enc = &enc_common->pi_enc;

    SVT_CALLOC_ARRAY(context_ptr, 1);
    thread_contxt_ptr->priv = context_ptr;
    thread_contxt_ptr->dctor = pack_stage_context_dctor;
    context_ptr->enc_common = enc_common;
    context_ptr->temp_precincts_in_slice = NULL;

    context_ptr->input_buffer_fifo_ptr = svt_jxs_system_resource_get_consumer_fifo(enc_api_prv->pack_input_resource_ptr, idx);

    context_ptr->output_buffer_fifo_ptr = svt_jxs_system_resource_get_producer_fifo(enc_api_prv->pack_output_resource_ptr, idx);

    if (enc_common->rate_control_mode == RC_CBR_PER_PRECINCT ||
        enc_common->rate_control_mode == RC_CBR_PER_PRECINCT_MOVE_PADDING) {
        if (enc_common->coding_vertical_prediction_mode != METHOD_PRED_DISABLE) {
            //Keep actual precinct and Top precinct for VPRED
            context_ptr->num_alloc_precincts_per_thread = 2;
        }
        else {
            context_ptr->num_alloc_precincts_per_thread = 1;
        }
    }
    else {
        context_ptr->num_alloc_precincts_per_thread = pi->precincts_per_slice;
    }

    context_ptr->buffers_dwt_tmp.buffer_tmp = NULL;
    context_ptr->buffers_dwt_tmp.buffer_unpacked_color_formats = NULL;

    uint8_t decom_V0_exist = 0;
    uint8_t decom_V1_exist = 0;
    uint8_t decom_V2_exist = 0;
    for (uint32_t i = 0; i < pi->comps_num; ++i) {
        if (pi->components[i].decom_v == 0) {
            decom_V0_exist = 1;
        }
        else if (pi->components[i].decom_v == 1) {
            decom_V1_exist = 1;
        }
        else if (pi->components[i].decom_v == 2) {
            decom_V2_exist = 1;
        }
    }

    uint32_t pixels_temp_size = 0;
    uint32_t unpacked_color_format_temp_size = 0;
    if (decom_V0_exist) {
        /*Buffer tmp DWT :
         * V0 required size: (width_max * 3 / 2) * sizeof(int32_t)*/
        pixels_temp_size = MAX(pixels_temp_size, (pi->width * 3 / 2));
        unpacked_color_format_temp_size = MAX(unpacked_color_format_temp_size, 3 * pi->width);
    }
    if (enc_common->cpu_profile == CPU_PROFILE_LOW_LATENCY) {
        if (decom_V1_exist) {
            /*Allocate buffer for V1:
             *2.5 temp for Vertical and Horizontal temp
             */
            pixels_temp_size = MAX(pixels_temp_size, (pi->width * 5 / 2));
            unpacked_color_format_temp_size = MAX(unpacked_color_format_temp_size, 9 * pi->width);

            /*Allocate buffer for V1:
            *3 lines on convert input,
            *1 - previous HF
            *1 - actual HF
            *2.5 temp for Vertical and Horizontal temp
            *Summary: 7.5 line == 15/2*width
            */
            SVT_CALLOC(context_ptr->buffers_dwt_tmp.V1.line_1, 1, pi->width * sizeof(int32_t));
            SVT_CALLOC(context_ptr->buffers_dwt_tmp.V1.line_2, 1, pi->width * sizeof(int32_t));
            SVT_CALLOC(context_ptr->buffers_dwt_tmp.V1.out_tmp_line_HF_next, 1, pi->width * sizeof(int32_t));
        }
        if (decom_V2_exist) {
            /*Allocate buffer for V2 get maximum from:
             (pi->width * 5 + 1) SIZE temp for transform_V2_Hx_precinct()
             (7*width/2 + 2)  SIZE temp for transform_V2_Hx_precinct_recalc() + and 3*width to allocate additional 4 lines for precalc.
             */
            pixels_temp_size = MAX(pixels_temp_size, (pi->width * 5 + 1));
            pixels_temp_size = MAX(pixels_temp_size, (7 * pi->width / 2 + 2) + 4 * pi->width);
            unpacked_color_format_temp_size = MAX(unpacked_color_format_temp_size, 27 * pi->width);

            SVT_CALLOC(context_ptr->buffers_dwt_tmp.V2.line_3, 1, pi->width * sizeof(int32_t));
            SVT_CALLOC(context_ptr->buffers_dwt_tmp.V2.line_4, 1, pi->width * sizeof(int32_t));
            SVT_CALLOC(context_ptr->buffers_dwt_tmp.V2.line_5, 1, pi->width * sizeof(int32_t));
            SVT_CALLOC(context_ptr->buffers_dwt_tmp.V2.line_6, 1, pi->width * sizeof(int32_t));
            SVT_CALLOC(context_ptr->buffers_dwt_tmp.V2.buffer_next, 1, (pi->width + 1) * sizeof(int32_t));
        }
    }

    if (enc_common->cpu_profile == CPU_PROFILE_LOW_LATENCY) {
        error = buffers_components_allocate(&context_ptr->buffers_dwt_per_component, pi);
        if (error) {
            return error;
        }
    }

    if (pixels_temp_size > 0) {
        SVT_CALLOC(context_ptr->buffers_dwt_tmp.buffer_tmp, 1, pixels_temp_size * sizeof(int32_t));
    }

    ColourFormat_t colour_format = enc_common->colour_format;
    if (colour_format > COLOUR_FORMAT_PACKED_MIN && colour_format < COLOUR_FORMAT_PACKED_MAX &&
        unpacked_color_format_temp_size > 0) {
        uint32_t pixel_size = enc_common->bit_depth <= 8 ? sizeof(uint8_t) : sizeof(uint16_t);
        if (colour_format == COLOUR_FORMAT_PACKED_YUV444_OR_RGB) {
            //rgb24 is 1byte per pixel in component
            SVT_CALLOC(
                context_ptr->buffers_dwt_tmp.buffer_unpacked_color_formats, 1, unpacked_color_format_temp_size * pixel_size);
        }
        else {
            assert(0);
        }
    }

    SVT_CALLOC(context_ptr->temp_precincts_in_slice, context_ptr->num_alloc_precincts_per_thread, sizeof(precinct_enc_t));

    for (uint32_t i = 0; i < context_ptr->num_alloc_precincts_per_thread; ++i) {
        precinct_enc_t* precincts = &context_ptr->temp_precincts_in_slice[i];

        for (uint32_t c = 0; c < pi->comps_num; ++c) {
            if (pi->components[c].decom_v == 0 || enc_common->cpu_profile == CPU_PROFILE_LOW_LATENCY) {
                SVT_MALLOC_ALIGNED_ARRAY(precincts->coeff_buff_ptr_16bit[c], (size_t)pi_enc->coeff_buff_tmp_size_precinct[c]);
            }
            SVT_MALLOC_ALIGNED_ARRAY(precincts->gc_buff_ptr[c], (size_t)pi_enc->gc_buff_tmp_size_precinct[c]);
            if (enc_common->coding_significance) {
                SVT_MALLOC_ALIGNED_ARRAY(precincts->gc_significance_buff_ptr[c],
                                         (size_t)pi_enc->gc_buff_tmp_significance_size_precinct[c]);
            }
            if (enc_common->coding_vertical_prediction_mode) {
                SVT_MALLOC_ALIGNED_ARRAY(precincts->vped_bit_pack_buff_ptr[c], (size_t)pi_enc->vped_bit_pack_size_precinct[c]);
                if (enc_common->coding_significance) {
                    SVT_MALLOC_ALIGNED_ARRAY(precincts->vped_significance_ptr[c],
                                             (size_t)pi_enc->vped_significance_size_precinct[c]);
                }
            }
        }
    }

    return SvtJxsErrorNone;
}

static void slice_init_bitstream(bitstream_writer_t* bitstream, PictureControlSet* pcs_ptr, PackInput_t* pack_input) {
    void* buf = pcs_ptr->enc_input.bitstream.buffer + pack_input->out_bytes_begin;
    bitstream_writer_init(bitstream, buf, pack_input->out_bytes_end - pack_input->out_bytes_begin);
}

static SvtJxsErrorType_t process_precinct(PictureControlSet* pcs_ptr, svt_jpeg_xs_encoder_common_t* enc_common, pi_t* pi,
                                          uint32_t slice_idx, uint32_t prec_idx, uint32_t prec_idx_in_slice,
                                          PackInput_t* pack_input, precinct_enc_t* precinct_top, precinct_enc_t* precinct,
                                          uint32_t budget_bytes, bitstream_writer_t* bitstream,
                                          struct precinct_calc_dwt_buff_tmp* buffers_dwt_tmp,
                                          struct precinct_calc_dwt_buff_per_component* buffers_dwt_per_component,
                                          uint32_t prec_num, uint32_t* budget_bytes_padding_left) {
    SvtJxsErrorType_t error = 0;
    precinc_info_enum type = PRECINCT_NORMAL;
    if (prec_idx + 1 >= enc_common->pi.precincts_line_num) {
        type = PRECINCT_LAST_NORMAL;
    }
    precinct_enc_init(pcs_ptr, pi, prec_idx, type, precinct_top, precinct);
    precinct_calculate_data(pcs_ptr, precinct, pack_input, buffers_dwt_tmp, buffers_dwt_per_component, prec_idx_in_slice);

    rate_control_init_precinct(pcs_ptr, precinct, enc_common->coding_signs_handling);
    error = rate_control_precinct(
        pcs_ptr, precinct, budget_bytes, enc_common->coding_vertical_prediction_mode, enc_common->coding_signs_handling);

    if (enc_common->rate_control_mode == RC_CBR_PER_PRECINCT_MOVE_PADDING) {
        /* Possibility of using unfilled data in precinct in next precinct by modifying the budget
         * Average, 3% of the precinct area is empty, and the next precinct can benefit from less quantization
         * by using up the area of the previous precinct (padding_bits).
         * But the transfer space should be average between precinct,
         * so that the first precinct in the slice is not of the worst quality.
         * Also can remove empty data from bitstream for non CBR coding, but file size will change.
         */
        if (prec_idx_in_slice + 1 < prec_num) {
            precinct->pack_total_bytes -= precinct->pack_padding_bytes;
            *budget_bytes_padding_left = precinct->pack_padding_bytes;
            precinct->pack_padding_bytes = 0;
            if (enc_common->coding_signs_handling == SIGN_HANDLING_STRATEGY_FAST) {
                precinct->pack_signs_handling_cut_precing = 1;
            }
        }
    }

    if (error) {
#ifndef NDEBUG
        fprintf(stderr, "Error calculate RC for precinct: %i\n", prec_idx);
#endif
        return error;
    }

    precinct_quantization(pcs_ptr, pi, precinct);

    if (prec_idx_in_slice == 0) {
        write_slice_header(bitstream, slice_idx);
    }

    error = pack_precinct(bitstream, pi, precinct, enc_common->coding_signs_handling);

    if (enc_common->rate_control_mode == RC_CBR_PER_PRECINCT_MOVE_PADDING) {
        if (enc_common->coding_signs_handling == SIGN_HANDLING_STRATEGY_FAST) {
            if (prec_idx_in_slice + 1 < prec_num) {
                assert(precinct->pack_signs_handling_cut_precing);
                *budget_bytes_padding_left += precinct->pack_signs_handling_fast_retrieve_bytes;
                //Revert bitstream!
                //bitstream->offset -= precinct->pack_signs_handling_fast_retrieve_bytes;
            }
        }
    }

    return error;
}

static SvtJxsErrorType_t process_slice(PictureControlSet* pcs_ptr, svt_jpeg_xs_encoder_common_t* enc_common, pi_t* pi,
                                       PackInput_t* pack_input, precinct_enc_t* precincts, uint32_t prec_num,
                                       uint32_t prec_first_idx, struct precinct_calc_dwt_buff_tmp* buffers_dwt_tmp,
                                       struct precinct_calc_dwt_buff_per_component* buffers_dwt_per_component,
                                       bitstream_writer_t* bitstream) {
    assert(enc_common->rate_control_mode == RC_CBR_PER_SLICE_COMMON_QUANT ||
           enc_common->rate_control_mode == RC_CBR_PER_SLICE_COMMON_QUANT_MAX_RATE);

    /*RC Budget per slice. Separate loops for DWT, RC, QUANTIZATION and PACK.*/
    SvtJxsErrorType_t error = 0;
    precinct_enc_t* precinct_top = NULL;
    precinct_enc_t* precinct = NULL;

    /*LOOP: Init and DWT*/
    for (uint32_t i = 0; i < prec_num; i++) {
        precinct = &precincts[i];
        if (enc_common->coding_vertical_prediction_mode != METHOD_PRED_DISABLE && i > 0) {
            precinct_top = &precincts[i - 1];
        }
        uint32_t prec_idx_global = prec_first_idx + i;
        precinc_info_enum type = PRECINCT_NORMAL;
        if (prec_idx_global + 1 >= enc_common->pi.precincts_line_num) {
            type = PRECINCT_LAST_NORMAL;
        }
        precinct_enc_init(pcs_ptr, pi, prec_idx_global, type, precinct_top, precinct);
        precinct_calculate_data(pcs_ptr, precinct, pack_input, buffers_dwt_tmp, buffers_dwt_per_component, i);
        rate_control_init_precinct(pcs_ptr, precinct, enc_common->coding_signs_handling);
    }

    /*LOOP: RC*/
    /*Precalculate common Quantization and Refinement for all precincts*/
    uint32_t slice_budget_bytes = pack_input->slice_budget_bytes;
    error = rate_control_slice_quantization_fast_no_vpred_no_sign_full(
        pcs_ptr, precincts, prec_num, slice_budget_bytes, enc_common->coding_signs_handling);
    if (error) {
#ifndef NDEBUG
        fprintf(stderr, "Error calculate RC for slice: %i\n", pack_input->slice_idx);
#endif
        return error;
    }
    else {
#ifndef NDEBUG
        /*Check correct bytes distribution*/
        uint32_t total_bytes = 0;
        for (uint32_t i = 0; i < prec_num; i++) {
            total_bytes += precincts[i].pack_total_bytes;
        }
        assert(total_bytes == slice_budget_bytes);
#endif
        if (enc_common->rate_control_mode == RC_CBR_PER_SLICE_COMMON_QUANT_MAX_RATE) {
            /*Update max rate*/
            uint32_t max_precinct_size = 0;
            for (uint32_t i = 0; i < prec_num; i++) {
                uint32_t data_size = precincts[i].pack_total_bytes - precincts[i].pack_padding_bytes;
                if (data_size > max_precinct_size) {
                    max_precinct_size = data_size;
                }
            }
            uint32_t min_precinct_size = max_precinct_size / TUNING_RC_CBR_PER_SLICE_MAX_PRECINCT_BUDGET_RATE;
            uint32_t budget_to_get = 0;
            for (uint32_t i = 0; i < prec_num; i++) {
                uint32_t data_size = precincts[i].pack_total_bytes - precincts[i].pack_padding_bytes;
                if (data_size < min_precinct_size) {
                    uint32_t add = min_precinct_size - data_size;
                    precincts[i].pack_total_bytes += add;
                    budget_to_get += add;
                }
            }
            if (budget_to_get) {
                //Get from padding from last precinct
                if (precincts[prec_num - 1].pack_padding_bytes >= budget_to_get) {
                    precincts[prec_num - 1].pack_padding_bytes -= budget_to_get;
                    precincts[prec_num - 1].pack_total_bytes -= budget_to_get;
                    budget_to_get = 0;
                }
                else {
                    budget_to_get -= precincts[prec_num - 1].pack_padding_bytes;
                    precincts[prec_num - 1].pack_total_bytes -= precincts[prec_num - 1].pack_padding_bytes;
                    precincts[prec_num - 1].pack_padding_bytes = 0;
                    //Very slow if happens then can be calculated faster by precalculation
                    while (budget_to_get) {
                        for (uint32_t i = 0; i < prec_num && budget_to_get; i++) {
                            budget_to_get--;
                            precincts[i].pack_total_bytes--;
                        }
                    }
                }
            }
#ifndef NDEBUG
            /*Check correct bytes distribution*/
            uint32_t total_bytes = 0;
            for (uint32_t i = 0; i < prec_num; i++) {
                total_bytes += precincts[i].pack_total_bytes;
            }
            assert(total_bytes == slice_budget_bytes);
#endif
        }

        /*Distribution padding from last precinct*/
        if (precincts[prec_num - 1].pack_padding_bytes > 0) {
            /*For some features add more bits for first precinct*/
            //TODO: Add also when adding not exist
            uint32_t add_first_precint = 0;
            if (enc_common->coding_signs_handling == SIGN_HANDLING_STRATEGY_FAST) {
                add_first_precint = (precincts[0].pack_total_bytes *
                                     TUNING_RC_CBR_PER_PRECINCT_MOVE_PADDING_FIRST_PREC_BIGGER_WITH_SIGN_LAZY_PERCENT) /
                    100;
            }
            else {
                add_first_precint = (precincts[0].pack_total_bytes *
                                     TUNING_RC_CBR_PER_PRECINCT_MOVE_PADDING_FIRST_PREC_BIGGER_NO_SIGN_LAZY_PERCENT) /
                    100;
            }
            add_first_precint = MIN(add_first_precint, precincts[prec_num - 1].pack_padding_bytes);
            precincts[prec_num - 1].pack_padding_bytes -= add_first_precint;
            precincts[prec_num - 1].pack_total_bytes -= add_first_precint;
            precincts[0].pack_total_bytes += add_first_precint;

#ifndef NDEBUG
            /*Check correct bytes distribution*/
            {
                uint32_t total_bytes = 0;
                for (uint32_t i = 0; i < prec_num; i++) {
                    total_bytes += precincts[i].pack_total_bytes;
                }
                assert(total_bytes == slice_budget_bytes);
            }
#endif

            if (precincts[prec_num - 1].pack_padding_bytes) {
                uint32_t left_bytes = precincts[prec_num - 1].pack_padding_bytes;
                precincts[prec_num - 1].pack_total_bytes -= precincts[prec_num - 1].pack_padding_bytes;
                precincts[prec_num - 1].pack_padding_bytes = 0;
                uint32_t min_budget_per_prec_bytes = left_bytes / prec_num;
                uint32_t left_budget_bytes = left_bytes - min_budget_per_prec_bytes * prec_num;
                for (uint32_t i = 0; i < prec_num; i++) {
                    precincts[i].pack_total_bytes += min_budget_per_prec_bytes;
                    if (i < left_budget_bytes) {
                        precincts[i].pack_total_bytes++;
                    }
                }
            }
        }
    }

#ifndef NDEBUG
    /*Check correct bytes distribution*/
    {
        uint32_t total_bytes = 0;
        for (uint32_t i = 0; i < prec_num; i++) {
            total_bytes += precincts[i].pack_total_bytes;
        }
        assert(total_bytes == slice_budget_bytes);
    }
#endif

#if PRINT_BUDGET
    for (uint32_t i = 0; i < prec_num; i++) {
        printf("slice_idx: %u prec_idx: %u Quantization: %u Refinement: %u LeftBytes %u Budget: %u\n",
               pack_input->slice_idx,
               prec_first_idx + i,
               precincts[i].pack_quantization,
               precincts[i].pack_refinement,
               precincts[i].pack_padding_bytes,
               precincts[i].pack_total_bytes);
    }
#endif

    /*LOOP: QUANTIZATION and PACK.
     *This loop can be easy separate but keep together can reduce CACHE flush in CPU.*/

    write_slice_header(bitstream, pack_input->slice_idx);
    uint32_t budget_padding_left_bytes = 0; /*Move padding budget between precincts.*/
    for (uint32_t i = 0; i < prec_num; i++) {
        precinct = &precincts[i];
        /*Recalculate Precinct for budget*/
        //TODO: Can optimize by use precalculated: precinct->pack_quantization in rate_control_find_best_refinement_binary_search()
        error = rate_control_precinct(pcs_ptr,
                                      precinct,
                                      precinct->pack_total_bytes + budget_padding_left_bytes,
                                      enc_common->coding_vertical_prediction_mode,
                                      enc_common->coding_signs_handling);
        if (error) {
#ifndef NDEBUG
            fprintf(stderr, "Error pack  precinct: %i\n", prec_first_idx + i);
#endif
            return error;
        }
        if (i + 1 < prec_num) {
            /*Move padding to next precinct*/
            budget_padding_left_bytes = precinct->pack_padding_bytes;
            precinct->pack_total_bytes -= precinct->pack_padding_bytes;
            precinct->pack_padding_bytes = 0;
            if (enc_common->coding_signs_handling == SIGN_HANDLING_STRATEGY_FAST) {
                precinct->pack_signs_handling_cut_precing = 1;
            }
        }

#if PRINT_BUDGET
        printf("slice_idx: %u prec_idx: %u Quantization: %u Refinement: %u LeftBytes %u Budget: %u\n",
               pack_input->slice_idx,
               prec_first_idx + i,
               precincts[i].pack_quantization,
               precincts[i].pack_refinement,
               precincts[i].pack_padding_bytes,
               precincts[i].pack_total_bytes);
#endif

        precinct_quantization(pcs_ptr, pi, &precincts[i]);
        error = pack_precinct(bitstream, pi, &precincts[i], enc_common->coding_signs_handling);
        if (error) {
#ifndef NDEBUG
            fprintf(stderr, "Error pack  precinct: %i\n", prec_first_idx + i);
#endif
            return error;
        }
        if (i + 1 < prec_num) {
            if (enc_common->coding_signs_handling == SIGN_HANDLING_STRATEGY_FAST) {
                assert(precinct->pack_signs_handling_cut_precing);
                budget_padding_left_bytes += precinct->pack_signs_handling_fast_retrieve_bytes;
            }
        }
    }

    return error;
}

void* pack_stage_kernel(void* input_ptr) {
    ThreadContext_t* enc_contxt_ptr = (ThreadContext_t*)input_ptr;
    PackStageContext* context_ptr = (PackStageContext*)enc_contxt_ptr->priv;

    PictureControlSet* pcs_ptr;
    ObjectWrapper_t *input_wrapper_ptr, *output_wrapper_ptr;

    for (;;) {
        // Get the Next svt Input Buffer [BLOCKING]
        SVT_GET_FULL_OBJECT(context_ptr->input_buffer_fifo_ptr, &input_wrapper_ptr);
        PackInput_t* pack_input = (PackInput_t*)input_wrapper_ptr->object_ptr;
        ObjectWrapper_t* pcs_wrapper_ptr = pack_input->pcs_wrapper_ptr;
        pcs_ptr = (PictureControlSet*)pcs_wrapper_ptr->object_ptr;

#ifdef FLAG_DEADLOCK_DETECT
        printf("07[%s:%i] frame: %03li slice: %03d\n", __func__, __LINE__, (size_t)pcs_ptr->frame_number, pack_input->slice_idx);
#endif

        pi_t* pi = &pcs_ptr->enc_common->pi;
        svt_jpeg_xs_encoder_common_t* enc_common = pcs_ptr->enc_common;
        SvtJxsErrorType_t error = 0;

        /*Write Slice header*/
        bitstream_writer_t bitstream;
        slice_init_bitstream(&bitstream, pcs_ptr, pack_input);

        /*RC and Quantization*/
        uint32_t prec_first_idx = pi->precincts_per_slice * pack_input->slice_idx;
        uint32_t prec_num = pi->precincts_per_slice;
        int32_t last_slice = (pack_input->slice_idx == enc_common->pi.slice_num - 1);
        if (last_slice) {
            prec_num = pi->precincts_line_num - prec_first_idx;
        }
        uint32_t min_budget_per_prec_bytes = pack_input->slice_budget_bytes / prec_num;
        uint32_t left_budget_bytes = pack_input->slice_budget_bytes - min_budget_per_prec_bytes * prec_num;
        /* Budget if not divide by precincts number then distribution size for upper precinct
         * Example Budget for 4 precincts:
         * 45/4 = 11 Left 1 Budgets: 12 11 11 11
         * 46/4 = 11 Left 2 Budgets: 12 12 11 11
         * 47/4 = 11 Left 3 Budgets: 12 12 12 11
         * 48/4 = 12 Left 0 Budgets: 12 12 12 12
         */

        precinct_enc_t* precincts = context_ptr->temp_precincts_in_slice;

        /*Calculate Slice*/
        if (enc_common->rate_control_mode == RC_CBR_PER_PRECINCT ||
            enc_common->rate_control_mode == RC_CBR_PER_PRECINCT_MOVE_PADDING) {
            /*RC Budget per precinct. One loop for DWT, RC, and PACK.*/
            precinct_enc_t* precinct_top = NULL;
            precinct_enc_t* precinct = NULL;
            uint8_t precinct_index = 0;
            if (enc_common->coding_vertical_prediction_mode == METHOD_PRED_DISABLE) {
                precinct = &precincts[precinct_index];
            }

            uint32_t first_budget_per_prec_bytes = min_budget_per_prec_bytes;

            if (enc_common->rate_control_mode == RC_CBR_PER_PRECINCT_MOVE_PADDING) {
                if (enc_common->coding_signs_handling == SIGN_HANDLING_STRATEGY_FAST) {
                    first_budget_per_prec_bytes = first_budget_per_prec_bytes *
                        (100 + TUNING_RC_CBR_PER_PRECINCT_MOVE_PADDING_FIRST_PREC_BIGGER_WITH_SIGN_LAZY_PERCENT) / 100;
                }
                else {
                    first_budget_per_prec_bytes = first_budget_per_prec_bytes *
                        (100 + TUNING_RC_CBR_PER_PRECINCT_MOVE_PADDING_FIRST_PREC_BIGGER_NO_SIGN_LAZY_PERCENT) / 100;
                }
                first_budget_per_prec_bytes = MIN(pack_input->slice_budget_bytes, first_budget_per_prec_bytes);
                if (prec_num > 1) {
                    uint32_t left_after_first = pack_input->slice_budget_bytes - first_budget_per_prec_bytes;
                    min_budget_per_prec_bytes = left_after_first / (prec_num - 1);
                    left_budget_bytes = pack_input->slice_budget_bytes - min_budget_per_prec_bytes * (prec_num - 1) -
                        first_budget_per_prec_bytes;
                }
                else {
                    left_budget_bytes = 0;
                }
            }
            assert(pack_input->slice_budget_bytes ==
                   first_budget_per_prec_bytes + (prec_num - 1) * min_budget_per_prec_bytes + left_budget_bytes);

            uint32_t budget_padding_left_bytes = 0; /*Move padding budget between precincts.*/
            for (uint32_t i = 0; i < prec_num; i++) {
                uint32_t budget_bytes = (i == 0) ? first_budget_per_prec_bytes : min_budget_per_prec_bytes;
                if (i < left_budget_bytes) {
                    budget_bytes++;
                }
                budget_bytes += budget_padding_left_bytes;
#if PRINT_BUDGET
                printf("Slice: %u Prec %u size bytes: %u\n", pack_input->slice_idx, i, budget_bytes);
#endif
                if (enc_common->coding_vertical_prediction_mode != METHOD_PRED_DISABLE) {
                    precinct_top = precinct;               //Set top for next precinct
                    precinct = &precincts[precinct_index]; //Get one of the two items to take turns
                    precinct_index = (precinct_index + 1) % context_ptr->num_alloc_precincts_per_thread;
                }
                error = process_precinct(pcs_ptr,
                                         enc_common,
                                         pi,
                                         pack_input->slice_idx,
                                         prec_first_idx + i,
                                         i,
                                         pack_input,
                                         precinct_top,
                                         precinct,
                                         budget_bytes,
                                         &bitstream,
                                         &context_ptr->buffers_dwt_tmp,
                                         &context_ptr->buffers_dwt_per_component,
                                         prec_num,
                                         &budget_padding_left_bytes);
                if (error) {
#ifndef NDEBUG
                    fprintf(stderr, "err happen when pack prec\n");
#endif
                    break;
                }
            }
        }
        else {
            error = process_slice(pcs_ptr,
                                  enc_common,
                                  pi,
                                  pack_input,
                                  precincts,
                                  prec_num,
                                  prec_first_idx,
                                  &context_ptr->buffers_dwt_tmp,
                                  &context_ptr->buffers_dwt_per_component,
                                  &bitstream);
#ifndef NDEBUG
            if (error) {
                fprintf(stderr, "Error calculate RC or pack for slice: %i\n", pack_input->slice_idx);
            }
#endif
        }

#ifndef NDEBUG
        if (error == SvtJxsErrorNone) {
            uint32_t used_bytes = bitstream_writer_get_used_bytes(&bitstream);
            uint32_t used_bytes_expected = pack_input->out_bytes_end - pack_input->out_bytes_begin;
            if (used_bytes_expected != used_bytes) {
                fprintf(stderr,
                        "Error pack slice: %i, Expected write to bitstream: %u Get: %u\n",
                        pack_input->slice_idx,
                        used_bytes_expected,
                        used_bytes);
                assert(0);
            }
        }
#endif
        assert((error != SvtJxsErrorNone) ||
               (bitstream_writer_get_used_bytes(&bitstream) == pack_input->out_bytes_end - pack_input->out_bytes_begin));

        //Write End of Bitstream
        if (error == SvtJxsErrorNone && pack_input->write_tail) {
            assert(pack_input->tail_bytes_begin == pack_input->out_bytes_end);
            void* buf = pcs_ptr->enc_input.bitstream.buffer + pack_input->tail_bytes_begin;
            bitstream_writer_t bitstream;
            bitstream_writer_init(&bitstream, buf, CODESTREAM_SIZE_BYTES);
            write_tail(&bitstream);
        }

        SvtJxsErrorType_t err = svt_jxs_get_empty_object(context_ptr->output_buffer_fifo_ptr, &output_wrapper_ptr);
        if (err != SvtJxsErrorNone || output_wrapper_ptr == NULL) {
            continue;
        }
#ifdef FLAG_DEADLOCK_DETECT
        printf("07[%s:%i] frame: %03li slice: %03d\n", __func__, __LINE__, (size_t)pcs_ptr->frame_number, pack_input->slice_idx);
#endif
        PackOutput* pack_out = (PackOutput*)output_wrapper_ptr->object_ptr;
        pack_out->pcs_wrapper_ptr = pcs_wrapper_ptr;
        pack_out->slice_idx = pack_input->slice_idx;
        pack_out->slice_error = error;
#ifdef FLAG_DEADLOCK_DETECT
        printf("07[%s:%i] frame: %03li slice: %03d\n", __func__, __LINE__, (size_t)pcs_ptr->frame_number, pack_out->slice_idx);
#endif
        svt_jxs_post_full_object(output_wrapper_ptr);

        svt_jxs_release_object(input_wrapper_ptr);
    }
    return NULL;
}
