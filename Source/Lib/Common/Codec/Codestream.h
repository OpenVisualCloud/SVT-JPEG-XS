/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __CODE_STREAM_FORMAT_H__
#define __CODE_STREAM_FORMAT_H__

#define CODESTREAM_SOC (0xff10) /*Start of codestream                               -Mandatory*/
#define CODESTREAM_EOC (0xff11) /*End of codestream                                 -Mandatory*/
#define CODESTREAM_PIH (0xff12) /*End of codestream                                 -Mandatory*/
#define CODESTREAM_CDT (0xff13) /*Component table                                   -Mandatory*/
#define CODESTREAM_WGT (0xff14) /*Weights table                                     -Mandatory*/
#define CODESTREAM_COM (0xff15) /*Extension marker                                  -Optional*/
#define CODESTREAM_NLT (0xff16) /*Nonlinearity marker                               -Optional*/
#define CODESTREAM_CWD (0xff17) /*Component-dependent wavelet decomposition marker  -Optional*/
#define CODESTREAM_CTS (0xff18) /*Colour transformation specification marker        -Mandatory if Cpih=3, ignore otherwise*/
#define CODESTREAM_CRG (0xff19) /*Component registration marker                     -Optional, mandatory if Cpih=3*/
#define CODESTREAM_SLH (0xff20) /*Slice header marker                               -Mandatory*/
#define CODESTREAM_CAP (0xff50) /*Capabilities Marker                               -Mandatory*/

#define CODESTREAM_SIZE_BYTES          (2)
#define PICTURE_HEADER_SIZE_BYTES      (26)
#define SLICE_HEADER_SIZE_BYTES        (6) /*Bits: 16 + 16 + 16*/
#define PRECINCT_HEADER_SIZE_BYTES     (5) /*Bits: 24 + 8 + 8*/
#define PACKET_HEADER_LONG_SIZE_BYTES  (7) /*Bits: 1 + 20 + 20 + 15*/
#define PACKET_HEADER_SHORT_SIZE_BYTES (5) /*Bits: 1 + 15 + 13 + 11*/

#define BITSTREAM_BIT_POSITION_SIGN (15)
#define BITSTREAM_MASK_SIGN         ((uint16_t)1 << (BITSTREAM_BIT_POSITION_SIGN))

#define PRECINCT_MAX_BYTES_SIZE (1048575) /*Lprc MAX: 2^20 -1*/

#endif /*__CODE_STREAM_FORMAT_H__*/
