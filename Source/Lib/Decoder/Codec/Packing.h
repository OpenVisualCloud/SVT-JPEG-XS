/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _PACKING_H
#define _PACKING_H
#include "Pi.h"
#include "ParseHeader.h"
#include "SvtUtility.h"

#ifdef __cplusplus
extern "C" {
#endif

SvtJxsErrorType_t unpack_precinct(bitstream_reader_t* bitstream, precinct_t* prec, precinct_t* prec_top, const pi_t* pi,
                                  const picture_header_dynamic_t* picture_header_dynamic, uint32_t verbose);
SvtJxsErrorType_t unpack_data_c(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis, uint32_t group_size,
                                uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num, int32_t* precinct_bits_left);
int32_t unpack_sign(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint32_t group_size, uint8_t leftover_signs_num,
                    int32_t* precinct_bits_left);

SvtJxsErrorType_t unpack_pred_zero_gclis_no_significance(bitstream_reader_t* bitstream, precinct_band_t* b_data,
                                                         precinct_band_info_t* b_info, const int ypos,
                                                         int32_t* precinct_bits_left);
SvtJxsErrorType_t unpack_pred_zero_gclis_significance(bitstream_reader_t* bitstream, precinct_band_t* b_data,
                                                      precinct_band_info_t* b_info, const int ypos, int32_t* precinct_bits_left);
SvtJxsErrorType_t unpack_verical_pred_gclis_no_significance(bitstream_reader_t* bitstream, precinct_band_t* b_data,
                                                            precinct_band_t* b_data_top, precinct_band_info_t* b_info,
                                                            const int ypos, int32_t band_max_lines, int32_t* precinct_bits_left);
SvtJxsErrorType_t unpack_verical_pred_gclis_significance(bitstream_reader_t* bitstream, precinct_band_t* b_data,
                                                         precinct_band_t* b_data_top, precinct_band_info_t* b_info,
                                                         const int ypos, int run_mode, int32_t band_max_lines,
                                                         int32_t* precinct_bits_left);

#ifdef __cplusplus
}
#endif

#endif /* _PACKING_H */
