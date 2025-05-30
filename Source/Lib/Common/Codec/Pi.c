/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Pi.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "SvtJpegxs.h"
#include "SvtUtility.h"

void set_precinct(pi_t* pi, precinct_band_info_t* b_info, uint32_t width, uint32_t height) {
    b_info->width = width;
    b_info->height = height;
    b_info->gcli_width = DIV_ROUND_UP(b_info->width, pi->coeff_group_size);
    b_info->significance_width = DIV_ROUND_UP(b_info->gcli_width, pi->significance_group_size);
}

void calc_precinct_dimension(pi_t* pi, uint8_t init_for_encoder, int precinct_width) {
    pi->precincts_line_num = DIV_ROUND_UP(pi->height, (1 << pi->decom_v));

    if (precinct_width == 0) {
        pi->precincts_col_num = 1;
        for (uint32_t c = 0; c < pi->comps_num; c++) {
            for (uint32_t b = 0; b < pi->components[c].bands_num; b++) {
                uint32_t width = pi->components[c].bands[b].width;
                uint32_t h_b = pi->components[c].bands[b].height;
                uint32_t height_norm = pi->components[c].bands[b].height_lines_num;
                uint32_t height_last = h_b - (pi->precincts_line_num - 1) * height_norm;

                set_precinct(pi, &pi->p_info[PRECINCT_NORMAL].b_info[c][b], width, height_norm);
                set_precinct(pi, &pi->p_info[PRECINCT_NORMAL_LAST].b_info[c][b], width, height_norm);
                set_precinct(pi, &pi->p_info[PRECINCT_LAST_NORMAL].b_info[c][b], width, height_last);
                set_precinct(pi, &pi->p_info[PRECINCT_LAST].b_info[c][b], width, height_last);
            }
        }
    }
    else {
        uint32_t sx_max = 0;
        for (uint32_t c = 0; c < pi->comps_num; c++) {
            sx_max = MAX(sx_max, pi->components[c].Sx);
        }
        uint32_t cs = 8 * precinct_width * sx_max * (1 << pi->decom_h);
        pi->precincts_col_num = DIV_ROUND_UP(pi->width, cs);

        for (uint32_t c = 0; c < pi->comps_num; c++) {
            for (uint32_t b = 0; b < pi->components[c].bands_num; b++) {
                uint32_t w_b = pi->components[c].bands[b].width;
                uint32_t h_b = pi->components[c].bands[b].height;
                uint32_t w_c = pi->components[c].width;
                uint32_t s_x = pi->components[c].Sx;

                uint32_t height_norm = pi->components[c].bands[b].height_lines_num;
                uint32_t width_norm;
                if (w_b % 2) {
                    width_norm = DIV_ROUND_DOWN(cs * w_b, s_x * w_c);
                }
                else {
                    width_norm = DIV_ROUND_UP(cs * w_b, s_x * w_c);
                }

                uint32_t width_last = w_b - (pi->precincts_col_num - 1) * width_norm;
                uint32_t height_last = h_b - (pi->precincts_line_num - 1) * height_norm;

                set_precinct(pi, &pi->p_info[PRECINCT_NORMAL].b_info[c][b], width_norm, height_norm);
                set_precinct(pi, &pi->p_info[PRECINCT_NORMAL_LAST].b_info[c][b], width_last, height_norm);
                set_precinct(pi, &pi->p_info[PRECINCT_LAST_NORMAL].b_info[c][b], width_norm, height_last);
                set_precinct(pi, &pi->p_info[PRECINCT_LAST].b_info[c][b], width_last, height_last);
            }
        }
    }
    if (init_for_encoder) {
        pi->p_info[PRECINCT_NORMAL].packets_exist_num = pi->packets_num;
        pi->p_info[PRECINCT_NORMAL_LAST].packets_exist_num = pi->packets_num;

        int packets_num = 0;
        for (uint32_t packet_idx = 0; packet_idx < pi->packets_num; packet_idx++) {
            int8_t skip_packet = 1;
            for (uint32_t band_idx = pi->packets[packet_idx].band_start; band_idx < pi->packets[packet_idx].band_stop;
                 band_idx++) {
                assert(pi->global_band_info[band_idx].band_id != BAND_NOT_EXIST);
                const uint8_t b = pi->global_band_info[band_idx].band_id;
                const uint8_t c = pi->global_band_info[band_idx].comp_id;
                const uint8_t line_idx = (uint8_t)pi->packets[packet_idx].line_idx;
                if (line_idx < pi->p_info[PRECINCT_LAST_NORMAL].b_info[c][b].height) {
                    skip_packet = 0;
                    break;
                }
            }
            if (skip_packet) {
                continue;
            }
            packets_num++;
        }
        pi->p_info[PRECINCT_LAST_NORMAL].packets_exist_num = packets_num;

        //TODO: Fix calculation this case when will be supported multiple precinct per line. (maybe from PRECINCT_LAST_NORMAL?)
        pi->p_info[PRECINCT_LAST].packets_exist_num = -1;

        /*Precalculate fixed GCLI RAW packet size: packet_size_gcli_raw[]*/
        for (uint32_t type = 0; type < PRECINCT_MAX; ++type) {
            precinct_info_t* p_info = &pi->p_info[type];
            for (uint32_t packet_idx = 0; packet_idx < pi->packets_num; packet_idx++) {
                uint32_t packet_size_gcli_raw_bits = 0;
                for (uint32_t band_idx = pi->packets[packet_idx].band_start; band_idx < pi->packets[packet_idx].band_stop;
                     band_idx++) {
                    const uint32_t b = pi->global_band_info[band_idx].band_id;
                    const uint32_t c = pi->global_band_info[band_idx].comp_id;
                    const uint32_t line_idx = pi->packets[packet_idx].line_idx;
                    uint32_t height_lines = p_info->b_info[c][b].height;
                    if (line_idx < height_lines) {
                        /*Calculate RAW*/
                        packet_size_gcli_raw_bits += p_info->b_info[c][b].gcli_width * 4;
                    }
                }
                /*RAW GCLI Pack Size:*/
                p_info->packet_size_gcli_raw_bytes[packet_idx] = BITS_TO_BYTE_WITH_ALIGN(packet_size_gcli_raw_bits);
            }
        }
    }
    else {
        pi->p_info[PRECINCT_NORMAL].packets_exist_num = -1;
        pi->p_info[PRECINCT_NORMAL_LAST].packets_exist_num = -1;
        pi->p_info[PRECINCT_LAST_NORMAL].packets_exist_num = -1;
        pi->p_info[PRECINCT_LAST].packets_exist_num = -1;
    }
}

void calc_packet_inclusion(pi_t* pi) {
    assert(pi->comps_num >= pi->Sd);
    uint32_t comp_with_decomp = pi->comps_num - pi->Sd;
    uint32_t packet_num = 0;
    uint32_t band_start = 0;
    uint32_t band_stop = MAX(pi->decom_h, pi->decom_v) - MIN(pi->decom_h, pi->decom_v) + 1;

    pi->packets[packet_num].line_idx = 0;
    pi->packets[packet_num].band_start = band_start;
    pi->packets[packet_num].band_stop = band_stop * comp_with_decomp;
    packet_num++;

    for (band_start = band_stop; band_start < pi->components[0].bands_num; band_start++) {
        pi->packets[packet_num].line_idx = 0;
        pi->packets[packet_num].band_start = band_start * comp_with_decomp;
        pi->packets[packet_num].band_stop = pi->packets[packet_num].band_start;
        for (uint32_t i = band_start * comp_with_decomp; i < (band_start + 1) * comp_with_decomp; i++) {
            if (pi->global_band_info[i].band_id != BAND_NOT_EXIST) {
                pi->packets[packet_num].band_stop++;
            }
        }
        packet_num++;
    }

    for (band_start = band_stop; band_start < pi->components[0].bands_num; band_start++) {
        if (pi->components[0].bands[band_start].height_lines_num == 1) {
            continue;
        }
        pi->packets[packet_num].line_idx = 1;
        pi->packets[packet_num].band_start = band_start * comp_with_decomp;
        pi->packets[packet_num].band_stop = pi->packets[packet_num].band_start;
        for (uint32_t i = band_start * comp_with_decomp; i < (band_start + 1) * comp_with_decomp; i++) {
            if (pi->global_band_info[i].band_id != BAND_NOT_EXIST) {
                uint32_t b = pi->global_band_info[i].band_id;
                uint32_t c = pi->global_band_info[i].comp_id;
                if (pi->components[c].bands[b].height_lines_num > 1) {
                    pi->packets[packet_num].band_stop++;
                }
            }
        }
        packet_num++;
    }

    uint32_t n_bands = comp_with_decomp * pi->components[0].bands_num;
    for (uint32_t c = comp_with_decomp; c < pi->comps_num; ++c) {
        for (uint32_t line = 0; line < pi->components[c].bands[0].height_lines_num; line++) {
            pi->packets[packet_num].line_idx = line;
            pi->packets[packet_num].band_start = n_bands;
            pi->packets[packet_num].band_stop = n_bands + 1;
            packet_num++;
        }
        n_bands++;
    }

    assert(packet_num < MAX_PACKETS_NUM);
    pi->packets_num = packet_num;
}

SvtJxsErrorType_t pi_compute(pi_t* pi, uint8_t init_for_encoder, uint32_t comps_num, uint32_t group_size, uint32_t sig_group_size,
                             uint32_t width, uint32_t height, uint32_t decom_h, uint32_t decom_v, uint32_t sd,
                             uint32_t sx[MAX_COMPONENTS_NUM], uint32_t sy[MAX_COMPONENTS_NUM], uint32_t precinct_width,
                             uint32_t slice_height) {
    if (sd > comps_num || decom_v > decom_h || decom_h > MAX_DECOMP_H_NUM || decom_v > MAX_DECOMP_V_NUM ||
        comps_num > MAX_COMPONENTS_NUM || comps_num < sd || slice_height == 0) {
        return SvtJxsErrorBadParameter;
    }

    pi->comps_num = comps_num;
    pi->coeff_group_size = group_size;
    pi->significance_group_size = sig_group_size;
    pi->width = width;
    pi->height = height;
    pi->decom_h = decom_h;
    pi->decom_v = decom_v;
    pi->Sd = sd;
    /*Number of components with decomposition*/
    assert(comps_num >= pi->Sd);

    uint32_t comp_with_decomp = comps_num - pi->Sd;

    /*(Hp=2^(NL,y))*/
    pi->precinct_height = 1 << pi->decom_v;

    if (slice_height < height) {
        if (slice_height % pi->precinct_height) {
            /*When more that one Slice in frame, then Slice height have to be divided by precinct height!*/
            return SvtJxsErrorBadParameter;
        }
    }

    pi->slice_height = slice_height;
    /*When (slice_height == height) AND (slice_height % pi->precinct_height) then need round up.*/
    pi->precincts_per_slice = DIV_ROUND_UP(pi->slice_height, pi->precinct_height);
    pi->slice_num = DIV_ROUND_UP(pi->height, pi->slice_height);

    pi->bands_num_exists = 0;
    for (uint32_t c = 0; c < comps_num; ++c) {
        pi_component_t* comp = &(pi->components[c]);
        /* sampling factor of component in horizontal and vertical direction*/
        if (sx[c] < 1 || sy[c] < 1) {
            return SvtJxsErrorBadParameter;
        }
        comp->Sx = sx[c];
        comp->Sy = sy[c];

        /* size of component  */
        comp->width = width / comp->Sx;
        comp->height = height / comp->Sy;
        comp->precinct_height = pi->precinct_height / comp->Sy;
        if (comp->precinct_height == 0) {
            return SvtJxsErrorBadParameter;
        }

        if (c < comp_with_decomp) {
            /* Supported formats does not change number of horizontal decomposition levels */
            comp->decom_h = pi->decom_h;
            /* Format 420 reduce number of vertical decomposition levels */
            if ((uint32_t)pi->decom_v < (comp->Sy / 2)) {
                /*Not supported for 420 when V:0 for frame*/
                return SvtJxsErrorBadParameter;
            }
            comp->decom_v = pi->decom_v - (comp->Sy / 2);
        }
        else {
            comp->decom_h = 0;
            comp->decom_v = 0;
        }

        comp->bands_num = 2 * comp->decom_v + comp->decom_h + 1;
        if (MAX_BANDS_PER_COMPONENT_NUM < comp->bands_num || comp->bands_num < 1) {
            return SvtJxsErrorBadParameter;
        }
        pi->bands_num_exists += comp->bands_num;
    }

    pi->bands_num_all = pi->components[0].bands_num * (comp_with_decomp) + pi->Sd;

    //Calculate bands topology
    uint32_t bands_num_global = 2 * decom_v + decom_h + 1;
    memset(pi->global_band_info, BAND_NOT_EXIST, sizeof(pi->global_band_info));

    for (uint32_t c = 0; c < comp_with_decomp; ++c) {
        pi_component_t* comp = &(pi->components[c]);
        assert(bands_num_global >= comp->bands_num);
        uint32_t bands_to_ignore = bands_num_global - comp->bands_num;
        /*Last 3 bands from 1 horizontal and 1 vertical transformation
          always are after missing gap of indexes*/
        uint32_t bands_at_the_end = 3 * comp->decom_v;
        uint32_t band_id = c;

        for (uint32_t i = 0; i < comp->bands_num; ++i) {
            // Calculate global level index
            comp->bands[i].band_id = band_id;
            pi->global_band_info[band_id].comp_id = c;
            pi->global_band_info[band_id].band_id = i;
            band_id += comp_with_decomp;
            //Ignore bands
            if ((bands_to_ignore > 0) && (i == (comp->bands_num - bands_at_the_end + 1 - bands_to_ignore))) {
                band_id += comp_with_decomp * bands_to_ignore;
            }
        }
    }

    for (uint32_t c = comp_with_decomp; c < comps_num; ++c) {
        pi_component_t* comp = &(pi->components[c]);
        if (1 != comp->bands_num) {
            return SvtJxsErrorBadParameter;
        }
        int band_id = comp_with_decomp * pi->components[0].bands_num + c - comp_with_decomp;
        comp->bands[0].band_id = band_id;
        pi->global_band_info[band_id].comp_id = c;
        pi->global_band_info[band_id].band_id = 0;
    }

    for (uint32_t c = 0; c < comps_num; ++c) {
        pi_component_t* comp = &(pi->components[c]);
        if ((comp->bands_num < 1) || (MAX_BANDS_PER_COMPONENT_NUM <= (comp->bands_num - 1))) {
            return SvtJxsErrorBadParameter;
        }
        uint32_t level_id_comp = comp->bands_num - 1;

        /* Set first band even when component not have more decompositions */
        comp->bands[level_id_comp].width = comp->width;
        comp->bands[level_id_comp].height = comp->height;
        comp->bands[level_id_comp].x = 0;
        comp->bands[level_id_comp].y = 0;

        for (uint32_t dvh = 0; dvh < comp->decom_v; ++dvh) {
            if (MAX_BANDS_PER_COMPONENT_NUM <= level_id_comp || 3 > level_id_comp) {
                return SvtJxsErrorBadParameter;
            }
            /* -tranform_vertical split all up down
               -tranform_horizontal split all left right all
               create 4 quarters.*/
            pi_band_t tmp = comp->bands[level_id_comp];
            uint32_t width2 = tmp.width / 2;
            uint32_t width1 = tmp.width - width2;
            uint32_t height2 = tmp.height / 2;
            uint32_t height1 = tmp.height - height2;

            comp->bands[level_id_comp].width = width2;
            comp->bands[level_id_comp].height = height2;
            comp->bands[level_id_comp].x = width1;
            comp->bands[level_id_comp].y = height1;
            comp->bands[level_id_comp - 1].width = width1;
            comp->bands[level_id_comp - 1].height = height2;
            comp->bands[level_id_comp - 1].x = 0;
            comp->bands[level_id_comp - 1].y = height1;
            comp->bands[level_id_comp - 2].width = width2;
            comp->bands[level_id_comp - 2].height = height1;
            comp->bands[level_id_comp - 2].x = width1;
            comp->bands[level_id_comp - 2].y = 0;
            comp->bands[level_id_comp - 3].width = width1;
            comp->bands[level_id_comp - 3].height = height1;
            comp->bands[level_id_comp - 3].x = 0;
            comp->bands[level_id_comp - 3].y = 0;
            level_id_comp = level_id_comp - 3;
        }
        for (uint32_t dh = comp->decom_v; dh < comp->decom_h; ++dh) {
            /*-tranform_horizontal split all left right all*/
            if (MAX_BANDS_PER_COMPONENT_NUM <= level_id_comp || 1 > level_id_comp) {
                return SvtJxsErrorBadParameter;
            }
            pi_band_t tmp = comp->bands[level_id_comp];
            uint32_t width2 = tmp.width / 2;
            uint32_t width1 = tmp.width - width2;
            uint32_t height1 = tmp.height;

            comp->bands[level_id_comp].width = width2;
            comp->bands[level_id_comp].height = height1;
            comp->bands[level_id_comp].x = width1;
            comp->bands[level_id_comp].y = 0;
            comp->bands[level_id_comp - 1].width = width1;
            comp->bands[level_id_comp - 1].height = height1;
            comp->bands[level_id_comp - 1].x = 0;
            comp->bands[level_id_comp - 1].y = 0;
            level_id_comp = level_id_comp - 1;
        }

        /*Check that any band size are not zero*/
        for (uint32_t level = 0; level < comp->bands_num; ++level) {
            if (comp->bands[level].width == 0 || comp->bands[level].height == 0) {
                return SvtJxsErrorBadParameter;
            }
        }

        uint32_t bands_2_lines = 0;
        if (comp->decom_v == 2) {
            bands_2_lines = 3;
        }
        uint32_t level = 0;
        for (; level < comp->bands_num - bands_2_lines; ++level) {
            comp->bands[level].height_lines_num = 1;
        }
        for (; level < comp->bands_num; ++level) {
            comp->bands[level].height_lines_num = 2;
        }
    }

    for (uint32_t c = comp_with_decomp; c < comps_num; ++c) {
        for (uint32_t level = 0; level < pi->components[c].bands_num; ++level) {
            if (pi->components[c].Sy < 1) {
                return SvtJxsErrorBadParameter;
            }
            /*Component without decomposition have number of lines per precinct equal to precinct height.*/
            pi->components[c].bands[level].height_lines_num = MIN((((uint32_t)1 << decom_v) >> (pi->components[c].Sy - 1)),
                                                                  pi->components[c].height);
        }
    }

    calc_packet_inclusion(pi);

    pi->use_short_header = (pi->width * pi->comps_num) < 32752;
    calc_precinct_dimension(pi, init_for_encoder, precinct_width);
    return SvtJxsErrorNone;
}

void pi_show_bands(const pi_t* const pi, int gain_priorities) {
    fflush(stderr);
    fflush(stdout);
    printf("\n====================================================================\n");
    //Dump topology:
    if (gain_priorities) {
        printf("Print bands priorities:\n");
    }
    else {
        printf("Print bands topology:\n");
    }

    printf("Num bands: %i\n", pi->bands_num_all);
    for (uint32_t c = 0; c < pi->comps_num; ++c) {
        const pi_component_t* const comp = &pi->components[c];
        printf("Component[%i]: [%ix%i] DV: %i DH: %i Num Bands: %i\n",
               c,
               comp->width,
               comp->height,
               comp->decom_v,
               comp->decom_h,
               comp->bands_num);

        printf("Level [Band]: \n");
        for (uint32_t i = 0; i < comp->bands_num; ++i) {
            printf("    %2i [%2i] pos: %3i,%3i [%3ix%3i] lines: %i\n",
                   i,
                   comp->bands[i].band_id,
                   comp->bands[i].x,
                   comp->bands[i].y,
                   comp->bands[i].width,
                   comp->bands[i].height,
                   comp->bands[i].height_lines_num);
        }
        printf("\n");

        int lineSize = comp->bands[0].height;
        int scale_column = pi->width / 180 /*Size of console scale width*/;
        if (gain_priorities) {
            scale_column = pi->width / 270; /*Size of console scale width*/
            ;
        }
        if (scale_column <= 0)
            scale_column = 1;
        printf("Topology: \n");
        for (uint32_t line = 0; line < comp->height / lineSize; line++) {
            printf("|");
            for (uint32_t i = 0; i < comp->bands_num; ++i) {
                uint32_t band_line = comp->bands[i].y / lineSize;
                uint32_t band_column_main = comp->bands[i].x / comp->bands[comp->bands_num - 1].width;
                uint32_t band_lines = comp->bands[i].height_lines_num;
                uint32_t band_line_number = line - band_line + 1;
                uint32_t band_columns = comp->bands[i].width / scale_column;
                if (line >= band_line && line < band_line + band_lines) {
                    char buffer[1024];
                    int tt;
                    if (gain_priorities) {
                        if (band_line_number == 1) {
                            tt = snprintf(buffer, 1023, "%i", comp->bands[i].priority);
                            //tt = snprintf(buffer, 1023, "%i/%i", comp->bands[i].gain, comp->bands[i].priority);
                            //tt = snprintf(buffer, 1023, "%i", comp->bands[i].gain);
                        }
                        else {
                            tt = snprintf(buffer, 1023, " ");
                        }
                    }
                    else {
                        if (band_lines > 1) {
                            tt = snprintf(buffer, 1023, "%2i '%i", comp->bands[i].band_id, band_line_number);
                        }
                        else {
                            tt = snprintf(buffer, 1023, "%2i", comp->bands[i].band_id);
                        }
                    }
                    band_columns = band_columns - tt - 1; //left 1 on | char
                    if (band_column_main > 0 && pi->components[c].width >= pi->components[0].width) {
                        if (gain_priorities) {
                            band_columns = 4 * band_columns / 8;
                        }
                        else {
                            band_columns = 5 * band_columns / 8;
                        }
                        printf("~");
                    }
                    uint32_t cc = 0;
                    for (; cc < band_columns / 2; ++cc) {
                        printf(".");
                    }
                    printf("%s", buffer);
                    for (; cc < band_columns; ++cc) {
                        printf(".");
                    }
                    printf("|");
                }
            }
            printf("\n");
        }
        printf("\n");
    }

    printf("Bands global MAPS: BAND_ID[COMPONENT:BAND_IN_COMPONENT]:\n");
    for (uint32_t i = 0; i < pi->bands_num_all; ++i) {
        if (pi->global_band_info[i].comp_id != BAND_NOT_EXIST) {
            printf(" %2i[%1i:%2i]", i, pi->global_band_info[i].comp_id, pi->global_band_info[i].band_id);
        }
        else {
            printf(" %2i[--]", i);
        }
    }
    printf("\n====================================================================\n\n");
    fflush(stdout);
}

SvtJxsErrorType_t format_get_sampling_factory(ColourFormat_t format, uint32_t* out_comp_num, uint32_t* out_sx, uint32_t* out_sy,
                                              uint32_t verbose) {
    switch (format) {
    case COLOUR_FORMAT_PLANAR_YUV422: {
        out_sx[0] = 1;
        out_sy[0] = out_sy[1] = out_sy[2] = 1;
        out_sx[1] = out_sx[2] = 2;
        *out_comp_num = 3;
        break;
    }
    case COLOUR_FORMAT_PLANAR_YUV420: {
        out_sx[0] = 1;
        out_sy[0] = 1;
        out_sy[1] = out_sy[2] = 2;
        out_sx[1] = out_sx[2] = 2;
        *out_comp_num = 3;
        break;
    }
    case COLOUR_FORMAT_PLANAR_YUV444_OR_RGB:
    case COLOUR_FORMAT_PACKED_YUV444_OR_RGB: {
        out_sx[0] = out_sx[1] = out_sx[2] = 1;
        out_sy[0] = out_sy[1] = out_sy[2] = 1;
        *out_comp_num = 3;
        break;
    }
    case COLOUR_FORMAT_PLANAR_4_COMPONENTS: {
        out_sx[0] = out_sx[1] = out_sx[2] = out_sx[3] = 1;
        out_sy[0] = out_sy[1] = out_sy[2] = out_sy[3] = 1;
        *out_comp_num = 4;
        break;
    }
    case COLOUR_FORMAT_GRAY: {
        out_sx[0] = 1;
        out_sy[0] = 1;
        *out_comp_num = 1;
        break;
    }
    default:
        if (verbose >= VERBOSE_ERRORS) {
            printf("Unsupported format\n");
        }
        return SvtJxsErrorBadParameter;
    }
    return SvtJxsErrorNone;
}

SvtJxsErrorType_t pi_update_proxy_mode(pi_t* pi, proxy_mode_t proxy_mode, uint32_t verbose) {
    if (proxy_mode == proxy_mode_full) {
        return SvtJxsErrorNone;
    }

    if (proxy_mode >= proxy_mode_max) {
        return SvtJxsErrorBadParameter;
    }

    uint8_t proxy_subsampling = 0;

    if (proxy_mode == proxy_mode_half) {
        proxy_subsampling = 1;
        if (pi->decom_v == 1) {
            pi->packets_num = 1;
        }
        else {
            pi->packets_num = 4;
        }
    }
    if (proxy_mode == proxy_mode_quarter) {
        proxy_subsampling = 2;
        pi->packets_num = 1;
    }

    if (proxy_subsampling > pi->decom_v || proxy_subsampling > pi->decom_h) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr,
                    "Cannot use proxy-mode=%d for stream with decomp_v=%d decomp_h=%d\n",
                    proxy_mode,
                    pi->decom_v,
                    pi->decom_h);
        }
        return SvtJxsErrorBadParameter;
    }

    pi->decom_v -= proxy_subsampling;
    pi->decom_h -= proxy_subsampling;
    pi->width = DIV_ROUND_UP(pi->width, 1 << proxy_subsampling);
    pi->height = DIV_ROUND_UP(pi->height, 1 << proxy_subsampling);

    for (uint32_t c = 0; c < pi->comps_num; c++) {
        if (proxy_subsampling > pi->components[c].decom_v || proxy_subsampling > pi->components[c].decom_h) {
            if (verbose >= VERBOSE_ERRORS) {
                fprintf(
                    stderr, "Cannot use proxy-mode=%d for component=%d decomp_v=%d\n", proxy_mode, c, pi->components[c].decom_v);
            }
            return SvtJxsErrorBadParameter;
        }

        pi->components[c].width = DIV_ROUND_UP(pi->components[c].width, 1 << proxy_subsampling);
        pi->components[c].height = DIV_ROUND_UP(pi->components[c].height, 1 << proxy_subsampling);
        pi->components[c].precinct_height >>= proxy_subsampling;
        pi->components[c].decom_v -= proxy_subsampling;
        pi->components[c].decom_h -= proxy_subsampling;
        pi->components[c].bands_num = 2 * pi->components[c].decom_v + pi->components[c].decom_h + 1;
    }

    return SvtJxsErrorNone;
}
