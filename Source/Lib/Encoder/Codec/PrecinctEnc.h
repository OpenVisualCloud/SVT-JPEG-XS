/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __PRECINCT_ENCODER_H__
#define __PRECINCT_ENCODER_H__
#include "Pi.h"
#include "RateControlCacheType.h"

typedef enum CodingMethodBand {
    METHOD_ZERO_SIGNIFICANCE_DISABLE = 0,
    METHOD_VPRED_SIGNIFICANCE_DISABLE = 1,
    METHOD_ZERO_SIGNIFICANCE_ENABLE = 2,
    METHOD_VPRED_SIGNIFICANCE_ENABLE = 3,
    METHOD_SIZE = 4,
    METHOD_INVALID
} CodingMethodBand;

typedef enum VerticalPredictionMode {
    METHOD_PRED_DISABLE = 0,
    METHOD_PRED_ZERO_RESIDUAL = 1,
    METHOD_PRED_ZERO_COEFFICIENTS = 2,
    METHOD_PRED_SIZE,
    METHOD_PRED_INVALID = -1,
} VerticalPredictionMode;

typedef enum SignHandlingStrategy {
    SIGN_HANDLING_STRATEGY_OFF = 0,
    SIGN_HANDLING_STRATEGY_FAST = 1,
    SIGN_HANDLING_STRATEGY_FULL = 2,
} SignHandlingStrategy;

#define RC_BAND_CACHE_SIZE 2 /*Number of cached results per band to not recalculate that same gtli per band*/

typedef struct precinct_enc {
    struct precinct_enc* precinct_top;
    precinct_info_t* p_info; /*Pointer to specific precinct*/
    uint32_t prec_idx;

    struct band_data_enc {
        uint8_t gtli; /* GTLI: Greatest Trimmed Line Index for band in precinct */

        struct line_data_enc_common {
            uint16_t* coeff_data_ptr_16bit;
            uint8_t* gcli_data_ptr; /* GCLI buffer data for band in precinct, one element correspond to <group_size> */
                                    /* of coefficient data. Can be reduced to line * band->gcli_width; */
            uint8_t* significance_data_max_ptr; /*Max GCli value for group*/
            rc_cache_band_line_t rc_cache_line;
        } lines_common[MAX_BAND_LINES];

        struct band_data_enc_cache {
            uint8_t gtli_rc_last_calculated; /*Cache gtli last calculate to not recalculate !*/
            CodingMethodBand pack_method;    /*CodingMethodBand*/
            struct line_data_enc_cache {
                uint8_t* vpred_bits_pack;
                uint8_t* vpred_significance;

                uint32_t pack_size_data_bits;
                uint32_t pack_size_gcli_bits;
                uint32_t pack_size_significance_bits;
                uint32_t packet_size_signs_handling_bits;
            } lines[MAX_BAND_LINES];
        } cache_buffers[RC_BAND_CACHE_SIZE];
        struct band_data_enc_cache* cache_actual; /*Pointer to cache_buffers[cache_index]*/
        /* Index of cache with current solution, other indexes is older.
         * For RC_BAND_CACHE_SIZE == 2 there is only current value and older value,
          * so do not have add calculate age of cached solution.*/
        uint32_t cache_index;
    } bands[MAX_COMPONENTS_NUM][MAX_BANDS_PER_COMPONENT_NUM];
    /*For some RC modes with Vertical Prediction when change precinct need recalculate next precinct.*/
    uint8_t need_recalculate_next_precinct;

    /*Precinct pack parameters*/
    uint32_t pack_quantization;
    uint32_t pack_refinement;

    uint32_t pack_padding_bytes;
    uint32_t pack_total_bytes;
    uint32_t pack_signs_handling_fast_retrieve_bytes; //Padding on end of precinct used only with SIGN_HANDLING_STRATEGY_FAST
    uint32_t pack_signs_handling_cut_precing;         //Remove pack_signs_handling_fast_retrieve_bytes during pack

    uint8_t packet_methods_raw[MAX_PACKETS_NUM];
    uint32_t packet_size_significance_bytes[MAX_PACKETS_NUM];
    uint32_t packet_size_gcli_bytes[MAX_PACKETS_NUM];
    uint32_t packet_size_data_bytes[MAX_PACKETS_NUM];
    uint32_t packet_size_signs_handling_bytes[MAX_PACKETS_NUM];

    /*Buffers to keep all data in precinct (bands: 0,1,2,3,3',4,4',5,5')*/
    uint16_t* coeff_buff_ptr_16bit[MAX_COMPONENTS_NUM]; //Requires for Profile: Latency
    uint8_t* gc_buff_ptr[MAX_COMPONENTS_NUM];
    uint8_t* gc_significance_buff_ptr[MAX_COMPONENTS_NUM];
    uint8_t* vped_bit_pack_buff_ptr[MAX_COMPONENTS_NUM];
    uint8_t* vped_significance_ptr[MAX_COMPONENTS_NUM];

} precinct_enc_t;

#ifdef __cplusplus
extern "C" {
#endif

struct PictureControlSet;
void precinct_enc_init(struct PictureControlSet* pcs_ptr, pi_t* pi, uint32_t prec_idx, precinc_info_enum type,
                       precinct_enc_t* precinct_top, precinct_enc_t* out_precinct);

#ifdef __cplusplus
}
#endif
#endif /*__PRECINCT_ENCODER_H__*/
