/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _PICTURE_INFORMATION_ENCODER_H_
#define _PICTURE_INFORMATION_ENCODER_H_
#include "Pi.h"

/* Information of Band */
typedef struct pi_enc_band {
    uint32_t coeff_buff_tmp_pos_offset_16bit;
    /*Encoder buffers for Group Coding*/
    uint32_t gc_buff_tmp_pos_offset;
    uint32_t gc_buff_tmp_significance_pos_offset;
    /*Encoder buffers for Vertical Prediction*/
    uint32_t vped_bit_pack_tmp_buff_offset;
    uint32_t vped_significance_tmp_buff_offset;
} pi_enc_band_t; /* Indexing [0..bands_num]*/

/* Information of Component */
typedef struct pi_enc_comp {
    /* Information of Bands */
    pi_enc_band_t bands[MAX_BANDS_PER_COMPONENT_NUM]; /* Indexing [0..bands_num]*/
} pi_enc_component_t;

/*Picture Information*/
typedef struct pi_enc {
    uint32_t coeff_buff_tmp_size_precinct[MAX_COMPONENTS_NUM];
    uint32_t gc_buff_tmp_size_precinct[MAX_COMPONENTS_NUM];
    uint32_t gc_buff_tmp_significance_size_precinct[MAX_COMPONENTS_NUM];
    uint32_t vped_bit_pack_size_precinct[MAX_COMPONENTS_NUM];
    uint32_t vped_significance_size_precinct[MAX_COMPONENTS_NUM];
    /* Information on Components */
    pi_enc_component_t components[MAX_COMPONENTS_NUM];

    /* Precalculate max values from weight tables */
    uint8_t max_refinement;   //Refinement can be less or equal to this value
    uint8_t max_quantization; //Quantization can be less or equal to this value
} pi_enc_t;

#ifdef __cplusplus
extern "C" {
#endif

int pi_compute_encoder(pi_t* pi, pi_enc_t* pi_enc, uint8_t significance_flag, uint8_t vpred_flag, uint8_t verbose);

#ifdef __cplusplus
}
#endif

#endif /*_PICTURE_INFORMATION_ENCODER_H_*/
