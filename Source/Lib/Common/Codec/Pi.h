/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _PICTURE_INFORMATION_COMMON_H_
#define _PICTURE_INFORMATION_COMMON_H_
#include <stdint.h>
#include "SvtJpegxs.h"

#define MAX_DECOMP_H_NUM            (5)
#define MAX_DECOMP_V_NUM            (2)
#define MAX_BANDS_PER_COMPONENT_NUM ((MAX_DECOMP_V_NUM) * (MAX_DECOMP_H_NUM))
#define MAX_BANDS_NUM               ((MAX_BANDS_PER_COMPONENT_NUM) * (MAX_COMPONENTS_NUM))
#define MAX_PACKETS_NUM             (30)
#define MAX_CAPABILITY              (16)
/*Max band lines in precinct*/
#define MAX_BAND_LINES (4)

#define BAND_NOT_EXIST (0xff)

typedef enum precinc_info_enum {
    /* Enum to iterate over different precinct dimensions*/
    PRECINCT_NORMAL = 0,      /* Indicate normal line and normal column */
    PRECINCT_NORMAL_LAST = 1, /* Indicate normal line and last column */
    PRECINCT_LAST_NORMAL = 2, /* Indicate last line and normal column */
    PRECINCT_LAST = 3,        /* Indicate last line and last column */
    PRECINCT_MAX = 4
} precinc_info_enum;

typedef struct picture_header_const {
    uint8_t marker_exist_CAP;
    uint8_t marker_exist_PIH;
    uint8_t marker_exist_CDT;
    uint8_t marker_exist_WGT;

    /*Change of this parameters required reinitializing Decoder*/
    uint32_t hdr_Ppih;           /* Profile this codestream complies to. */
    uint32_t hdr_Plev;           /* Level and sub-level to which this codestream complies. */
    int32_t hdr_width;           /* Wf: width of the image in sampling grid points*/
    int32_t hdr_height;          /* Hf: height of the image in sampling grid points*/
    uint32_t hdr_precinct_width; /* Width of a precinct in multiples of 8*max_i(Sx_i x 2^Nlx) */
    uint32_t hdr_Hsl;            /* height of a slice in precincts other than the last slice */
    uint8_t hdr_comps_num;       /* Nc: number of components in an image*/
    int32_t hdr_decom_h;         /* NL,x: maximal number of horizontal decomposition levels*/
    int32_t hdr_decom_v;         /* NL,y: maximal number of vertical decomposition levels*/
    int32_t hdr_Sd;              /* Sd: Number components with suppressed decomposition*/
    uint8_t hdr_Cpih;            /* Colour transformation to be used for inverse decorrelation */
    uint32_t hdr_Sx[MAX_COMPONENTS_NUM];
    uint32_t hdr_Sy[MAX_COMPONENTS_NUM];
    uint8_t hdr_bit_depth[MAX_COMPONENTS_NUM];
    uint8_t hdr_capability[MAX_CAPABILITY];

    /*Can be in dynamic header, but required to calculate Precinct size*/
    int32_t hdr_coeff_group_size;        /* Ng: Number of coefficients per code group */
    int32_t hdr_significance_group_size; /* Ss: Number of code groups per significance group */

    /*Can be in dynamic header, but required to calculate separate Pi_t structure*/
    uint8_t hdr_gain[MAX_BANDS_NUM]; //TODO: Move to picture_header_dynamic_t
    uint8_t hdr_priority[MAX_BANDS_NUM];
} picture_header_const_t;

typedef struct picture_header_dynamic {
    uint8_t marker_exist_CTS;
    uint8_t marker_exist_CRG;

    uint32_t hdr_Lcod;       /* Size of the entire codestream in bytes from SOC to EOC, including all markers */
    uint8_t hdr_Bw;          /* Nominal bit precision of the wavelet coefficients */
    uint8_t hdr_Fq;          /* Number of fractional bits in the wavelet coefficients*/
    uint8_t hdr_Br;          /* Number of bits to encode a bit-plane count in raw*/
    uint8_t hdr_Fslc;        /* Slice coding mode */
    uint8_t hdr_Ppoc;        /* Progression order of bands within precincts */
    uint8_t hdr_Lh;          /* Long header enforcement flag */
    uint8_t hdr_Rl;          /* Raw-mode selection per packet flag */
    uint8_t hdr_Qpih;        /* Inverse quantizer type */
    uint8_t hdr_Fs;          /* Sign handling strategy */
    uint8_t hdr_Rm;          /* Run mode */
    uint8_t hdr_Tnlt;        /* Type of the non-linearity*/
    uint8_t hdr_Tnlt_sigma;  /* Quadratic non-linearity, sign bit of the DC offset*/
    uint16_t hdr_Tnlt_alpha; /* Quadratic non-linearity, remaining bits of the DC offset*/
    uint32_t hdr_Tnlt_t1;    /* Extended non-linearity, Upper threshold for region 1 */
    uint32_t hdr_Tnlt_t2;    /* Extended non-linearity, Upper threshold for region 2 */
    uint8_t hdr_Tnlt_e;      /* Extended non-linearity, Exponent of the linear slope in region 2 */
    uint8_t hdr_Cf;          /* Colour transformation, Size and extent of the transformation */
    uint8_t hdr_Cf_e1;       /* Colour transformation, Exponent of first chroma component */
    uint8_t hdr_Cf_e2;       /* Colour transformation, Exponent of second chroma component */

    uint16_t hdr_Xcrg[MAX_COMPONENTS_NUM];
    uint16_t hdr_Ycrg[MAX_COMPONENTS_NUM];
} picture_header_dynamic_t;

typedef struct precinct_band_info {
    uint32_t width;
    uint32_t gcli_width;
    uint32_t significance_width;
    uint32_t height; /*Lines number in band.*/
} precinct_band_info_t;

typedef struct precinct_info {
    int32_t packets_exist_num; /* Encoder only: Number of existing packages in specific precinct. */
                               /* For iteration all packages use pi::packets_num */
    precinct_band_info_t b_info[MAX_COMPONENTS_NUM][MAX_BANDS_PER_COMPONENT_NUM];
    uint32_t packet_size_gcli_raw_bytes[MAX_PACKETS_NUM]; /*Precalculate fixed GCLI RAW packet size, used in Encoder*/
} precinct_info_t;

typedef struct pi_packet_inclusion {
    // Packet indexing [band_start .. band_stop-1]
    // Indexing refers to global_band_info[]
    uint32_t band_start;
    uint32_t band_stop;
    uint32_t line_idx;
} pi_packet_inclusion_t;

/* Information of Band */
typedef struct pi_band {
    uint32_t band_id;          /* Band ID in picture*/
    uint32_t width;            /* Width for band in precinct and picture*/
    uint32_t height;           /* Height for band in picture*/
    uint32_t height_lines_num; /* Height for band in Precinct*/
    uint32_t x;                /* Position X*/
    uint32_t y;                /* Position Y*/
    uint8_t gain;
    uint8_t priority;
} pi_band_t;

/* Information of Component */
typedef struct pi_component {
    uint32_t Sx;              /* Sx: sampling factor of component in horizontal direction --*/
    uint32_t Sy;              /* Sy: sampling factor of component in vertical direction   ||*/
    uint32_t width;           /* Wc[i]: width of component i in samples  */
    uint32_t height;          /* Hc[i]: height of component i in samples */
    uint32_t decom_h;         /* N'L,x: maximal number of horizontal decomposition levels*/
    uint32_t decom_v;         /* N'L,y: maximal number of vertical decomposition levels*/
    uint32_t precinct_height; /* Height of a precinct in lines, for component*/
    uint32_t bands_num;       /* Nb: number of bands per component*/
    /* Information of Bands */
    pi_band_t bands[MAX_BANDS_PER_COMPONENT_NUM]; /* Indexing [0..bands_num]*/
} pi_component_t;

/*Picture Information*/
typedef struct pi {
    uint32_t comps_num;               /* Nc: number of components in an image*/
    uint32_t width;                   /* Wf: width of the image in sampling grid points*/
    uint32_t height;                  /* Hf: height of the image in sampling grid points*/
    uint32_t decom_h;                 /* NL,x: maximal number of horizontal decomposition levels*/
    uint32_t decom_v;                 /* NL,y: maximal number of vertical decomposition levels*/
    uint32_t bands_num_all;           /* NL: number of bands in the wavelet decomposition */
                                      /* of the image (wavelet filter types times components) */
    uint32_t bands_num_exists;        /* number of bands exists in the wavelet */
                                      /* SUM(components[*].bands_num) == bands_num_all - ignore_bands */
    uint32_t precinct_height;         /* Hp: (Hp=2^(NL,y)) height of a precinct in lines*/
    uint32_t coeff_group_size;        /* Number of coefficients per code group */
    uint32_t significance_group_size; /* Number of code groups per significance group */
                                      /* Support for Bayer format, ignore part of components */
    uint32_t Sd;                      /* Sd: Number components with suppressed decomposition */
    uint32_t slice_height;            /* Height of slice, have to be multiple of precinct height */
    uint32_t precincts_per_slice;     /* Hsl: height of a slice in precincts*/
    uint32_t precincts_col_num;       /* number Np,y of precincts per column*/
    uint32_t precincts_line_num;      /* The number Np,x Number or precincts per frame.*/
    uint32_t slice_num;               /* Number of slices in frame.*/
    int32_t use_short_header;

    /* Information on Components */
    pi_component_t components[MAX_COMPONENTS_NUM];

    struct {
        /* Ordering bands in component Sample 420 H:5 V:2
         * BAND:   0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29
         * Comp:   0 1 2 0 1 2 0 1 2 0  1  2  0  1  2  0  1  2  0  1  2  0  1  2  0  1  2  0  1  2
         * b.in c. 0 0 0 1 1 1 2 2 2 3  3  3  4  4  4  5  5  5  6  -  -  7  -  -  8  6  6  9  7  7
         * When band not exist then value is equal to BAND_NOT_EXIST*/
        uint8_t comp_id;
        uint8_t band_id;               /* Index of band in component comp_id*/
    } global_band_info[MAX_BANDS_NUM]; /* Indexing of global band [0 .. bands_num_all-1]*/

    uint32_t packets_num;
    pi_packet_inclusion_t packets[MAX_PACKETS_NUM];

    precinct_info_t p_info[PRECINCT_MAX];
} pi_t;

typedef struct precinct_band {
    uint16_t* coeff_data;                           /* Coefficient data for band in precinct */
    uint8_t leftover_signs_to_read[MAX_BAND_LINES]; /* Number of signs we need to read from bitstream when Fs flag is set to 1*/

    /* Dequantization parameters */
    /* GCLI: Greatest common/coded line index */
    uint8_t gtli;       /* Greatest Trimmed Line Index for band in precinct */
    uint8_t* gcli_data; /* GCLI buffer data for band in precinct, one element correspond to <group_size> of coefficient data */

    uint8_t* significance_data;
} precinct_band_t;

typedef struct precinct {
    //uint32_t global_x;              /* X position of precinct in image; relative to image-space */
    //uint32_t global_y;              /* Y position of precinct in image; relative to image-space */
    //uint32_t global_width;          /* Width  of precinct in image, relative to image-space */
    //uint32_t global_height;         /* Height of precinct in image, relative to image-space */
    precinct_band_t bands[MAX_COMPONENTS_NUM][MAX_BANDS_PER_COMPONENT_NUM];
    precinct_info_t* p_info;
} precinct_t;

#define BITS_ALIGN_TO_BYTE(nbits)      (((nbits) + 7) & (~(7)))
#define BITS_TO_BYTE_WITH_ALIGN(nbits) (((nbits) + 7) >> 3)

#ifdef __cplusplus
extern "C" {
#endif

SvtJxsErrorType_t pi_compute(pi_t* pi, uint8_t init_for_encoder, uint32_t comps_num, uint32_t group_size, uint32_t sig_group_size,
                             uint32_t width, uint32_t height, uint32_t decom_h, uint32_t decom_v, uint32_t sd,
                             uint32_t sx[MAX_COMPONENTS_NUM], uint32_t sy[MAX_COMPONENTS_NUM], uint32_t precinct_width,
                             uint32_t slice_height);

SvtJxsErrorType_t pi_update_proxy_mode(pi_t* pi, proxy_mode_t proxy_mode, uint32_t verbose);

void pi_show_bands(const pi_t* const pi, int gain_priorities);

SvtJxsErrorType_t format_get_sampling_factory(ColourFormat_t format, uint32_t* out_comp_num, uint32_t* out_sx, uint32_t* out_sy,
                                              uint32_t verbose);

#ifdef __cplusplus
}
#endif

#endif /*_PICTURE_INFORMATION_COMMON_H_*/
