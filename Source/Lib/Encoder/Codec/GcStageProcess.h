/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _GC_STAGE_H_
#define _GC_STAGE_H_

#include "Definitions.h"
#include "PrecinctEnc.h"
#include "PackIn.h"

#ifdef __cplusplus
extern "C" {
#endif

/*This data need be keep between precincts*/
struct precinct_calc_dwt_buff_per_component {
    struct {
        int32_t* line_first;          //width
        int32_t* in_tmp_line_HF_prev; //width;
    } V1[MAX_COMPONENTS_NUM];
    struct {
        int32_t* line_2;          //width
        int32_t* buffer_prev;     //(width + 1);
        int32_t* buffer_on_place; //(3*width/2)
    } V2[MAX_COMPONENTS_NUM];
};

/*This data can be override between precincts*/
struct precinct_calc_dwt_buff_tmp {
    /*Size of buffer_tmp:
     * V0: 1.5 * width
     * V1: 2.5 * width
     */
    int32_t* buffer_tmp; //Last 2.5 width for horizontal and vertical internal calculations

    /*Buffer is allocated only if packed color format is used,
    * Buffer hold unpacked pixels
    * Size of buffer_unpacked_color_formats:
     * V0: 3 * width, one line per component
     * V1: 9 * width, three lines per component
     * V2: 27 * width, nine lines per component
     */
    void* buffer_unpacked_color_formats;

    /*Allocated only when the encoder-side forward RCT (Cpih=1) is enabled.
     * Holds already-NLT-scaled-and-RCT'd int32 lines for components 0,1,2, addressed by a
     * ring position (absolute image line index modulo 13), NOT by plane_buffer_in[13]'s
     * window-relative slot - see rct_scaled_lines_tag. Persists across precinct_calculate_data
     * calls so each absolute line is NLT-scaled+RCT'd at most once, instead of being
     * recomputed every time it re-appears in the sliding 13-line window.
     * Size: 3 * 13 * width int32.
     */
    int32_t* rct_scaled_lines;
    /*Parallel to rct_scaled_lines: rct_scaled_lines_tag[ring] holds the absolute image line
     * index currently cached at that ring position, or -1 if never computed. A window slot
     * is reused verbatim (no recompute) when its absolute line index already matches the tag
     * at that line's ring position; only genuinely new lines get NLT-scaled+RCT'd.
     * Absolute line indices repeat every frame (0..height-1), so rct_scaled_lines_frame below
     * tracks which frame the cache is valid for; all tags are invalidated on frame change.
     */
    int32_t rct_scaled_lines_tag[13];
    uint64_t rct_scaled_lines_frame;

    struct {
        /*Size temp total 7.5 width for one component,
         *for few component 2 width need be allocated additional.
         *2.5 is allocated common in pointer buffer_tmp;
         */

        /*Temporary buffers, pointers can be swap between component and common buffers.*/
        int32_t* line_1;               //width
        int32_t* line_2;               //width
        int32_t* out_tmp_line_HF_next; //width;
    } V1;

    struct {
        /*Size temp total (5*width + 1),
        * allocated common in pointer buffer_tmp
        */

        /*Temporary buffers, pointers can be swap between component and common buffers.*/
        int32_t* line_3;      //width
        int32_t* line_4;      //width
        int32_t* line_5;      //width
        int32_t* line_6;      //width
        int32_t* buffer_next; //(width + 1);
    } V2;
};

SvtJxsErrorType_t buffers_components_allocate(struct precinct_calc_dwt_buff_per_component* buffers_dwt_per_component, pi_t* pi);
void buffers_components_free(struct precinct_calc_dwt_buff_per_component* buffers_dwt_per_component);

void precinct_component_calculate_dwt_V1_precalculate_slice(
    struct PictureControlSet* pcs_ptr, uint32_t comp_id, uint32_t prec_idx, struct precinct_calc_dwt_buff_tmp* buffers_dwt_tmp,
    struct precinct_calc_dwt_buff_per_component* buffers_dwt_per_component, const void** plane_buffer_in,
    const int32_t** rct_lines_in);
void precinct_component_calculate_dwt_V1(struct PictureControlSet* pcs_ptr, uint32_t comp_id, uint32_t prec_idx,
                                         uint16_t* buffer_out_16bit, struct precinct_calc_dwt_buff_tmp* buffers_dwt_tmp,
                                         struct precinct_calc_dwt_buff_per_component* buffers_dwt_per_component,
                                         const void** plane_buffer_in, const int32_t** rct_lines_in);

void precinct_component_calculate_dwt_V2_precalculate_slice(
    struct PictureControlSet* pcs_ptr, uint32_t comp_id, uint32_t prec_idx, struct precinct_calc_dwt_buff_tmp* buffers_dwt_tmp,
    struct precinct_calc_dwt_buff_per_component* buffers_dwt_per_component, const void** plane_buffer_in,
    const int32_t** rct_lines_in);
void precinct_component_calculate_dwt_V2(struct PictureControlSet* pcs_ptr, uint32_t comp_id, uint32_t prec_idx,
                                         uint16_t* buffer_out_16bit, struct precinct_calc_dwt_buff_tmp* buffers_dwt_tmp,
                                         struct precinct_calc_dwt_buff_per_component* buffers_dwt_per_component,
                                         const void** plane_buffer_in, const int32_t** rct_lines_in);

void precinct_calculate_data(struct PictureControlSet* pcs_ptr, precinct_enc_t* precinct, PackInput_t* pack_input,
                             struct precinct_calc_dwt_buff_tmp* buffers_dwt_tmp,
                             struct precinct_calc_dwt_buff_per_component* buffers_dwt_per_component, uint32_t prec_idx_in_slice);
void gc_precinct_stage_scalar_c(uint8_t* gcli_data_ptr, uint16_t* coeff_data_ptr_16bit, uint32_t group_size, uint32_t width);
void gc_precinct_stage_scalar_loop_c(uint32_t line_groups_num, uint16_t* coeff_data_ptr_16bit, uint8_t* gcli_data_ptr);

void gc_precinct_sigflags_max_c(uint8_t* significance_data_max_ptr, uint8_t* gcli_data_ptr, uint32_t group_sign_size,
                                uint32_t gcli_width);

void convert_packed_to_planar_rgb_8bit_c(const void* in_rgb, void* out_comp1, void* out_comp2, void* out_comp3,
                                         uint32_t line_width);
void convert_packed_to_planar_rgb_16bit_c(const void* in_rgb, void* out_comp1, void* out_comp2, void* out_comp3,
                                          uint32_t line_width);
#ifdef __cplusplus
}
#endif
#endif /*_GC_STAGE_H_*/
