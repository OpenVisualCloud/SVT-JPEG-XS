/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "random.h"
#include "SvtUtility.h"
#include "BitstreamReader.h"
#include "BitstreamWriter.h"
#include "PackPrecinct.h"
#include "Packing.h"

#define SILENT_OUTPUT 1
#if SILENT_OUTPUT
#define printf(...)
#endif

int8_t read_to_first_negative_bit_old(bitstream_reader_t* bitstream, uint32_t size);

static INLINE int8_t vlc_decoder_vertical_old(bitstream_reader_t* unpacker, int r, int t) {
    int treshold = MAX(r - t, 0);

    /*TODO: Add handle errors when invalid read first negative bit.*/
    int8_t x = read_to_first_negative_bit_old(unpacker, 32);

    if (x > 2 * treshold) {
        return x - treshold;
    }
    else if (x > 0) {
        if (x & 1) {
            return -((x + 1) / 2);
        }
        else {
            return x / 2;
        }
    }
    else {
        return 0;
    }
}

static INLINE int vlc_decoder_old(bitstream_reader_t* unpacker, int r, int t) {
    int treshold = MAX(r - t, 0);

    uint8_t x = 0;
    uint8_t b = 0;

    do {
        b = read_1_bit(unpacker);
        if (b) {
            x = x + 1;
        }
    } while (b && (x < 32));

    if (x >= 32) {
        printf("error while using vlc()\n");
        return -1;
    }

    if (x > 2 * treshold) {
        return x - treshold;
    }
    else if (x > 0) {
        if (x & 1) {
            return -((x + 1) / 2);
        }
        else {
            return x / 2;
        }
    }
    else {
        return 0;
    }
}

static INLINE int8_t vlc_decoder_pred_zero_old(bitstream_reader_t* unpacker) {
    return read_to_first_negative_bit_old(unpacker, 32);
}

static void unpack_pred_zero_gclis_no_significance_old(bitstream_reader_t* bitstream, precinct_band_t* b_data,
                                                       precinct_band_info_t* b_info, const int ypos) {
    const uint8_t gtli = b_data->gtli;
    const uint32_t gcli_width = b_info->gcli_width;

    uint8_t* gclis = b_data->gcli_data + ypos * gcli_width;

    for (uint32_t i = 0; i < gcli_width; i++) {
        gclis[i] = gtli + vlc_decoder_pred_zero_old(bitstream);
    }
}

static void unpack_pred_zero_gclis_significance_old(bitstream_reader_t* bitstream, precinct_band_t* b_data,
                                                    precinct_band_info_t* b_info, const int ypos) {
    const uint8_t gtli = b_data->gtli;
    const uint32_t gcli_width = b_info->gcli_width;

    uint8_t* gclis = b_data->gcli_data + ypos * gcli_width;
    uint8_t* significances = b_data->significance_data + ypos * b_info->significance_width;

    for (uint32_t group_idx = 0; group_idx < (gcli_width / SIGNIFICANCE_GROUP_SIZE); group_idx++) {
        uint8_t significance_group_is_significant = !significances[0];
        if (significance_group_is_significant) {
            for (uint8_t i = 0; i < SIGNIFICANCE_GROUP_SIZE; i++) {
                gclis[i] = gtli + vlc_decoder_pred_zero_old(bitstream);
            }
        }
        else {
            memset(gclis, gtli, SIGNIFICANCE_GROUP_SIZE);
        }
        significances++;
        gclis += SIGNIFICANCE_GROUP_SIZE;
    }
    uint8_t leftover = (gcli_width % SIGNIFICANCE_GROUP_SIZE);
    if (leftover) {
        uint8_t significance_group_is_significant = !significances[0];
        if (significance_group_is_significant) {
            for (uint8_t i = 0; i < leftover; i++) {
                gclis[i] = gtli + vlc_decoder_pred_zero_old(bitstream);
            }
        }
        else {
            memset(gclis, gtli, leftover);
        }
    }
}

static void unpack_verical_pred_gclis_no_significance_old(bitstream_reader_t* bitstream, precinct_band_t* b_data,
                                                          precinct_band_t* b_data_top, precinct_band_info_t* b_info,
                                                          const int ypos, int32_t band_max_lines) {
    const uint8_t gtli = b_data->gtli;
    const uint8_t gtli_top = (ypos == 0) ? (b_data_top->gtli) : (gtli);
    const uint8_t t = MAX(gtli_top, gtli);
    const uint32_t gcli_width = b_info->gcli_width;
    uint8_t* gclis_top = NULL;
    if (ypos == 0) {
        gclis_top = b_data_top->gcli_data + (band_max_lines - 1) * gcli_width;
    }
    else {
        gclis_top = b_data->gcli_data + (ypos - 1) * gcli_width;
    }

    uint8_t* gclis = b_data->gcli_data + ypos * gcli_width;

    for (uint32_t i = 0; i < gcli_width; i++) {
        const int m_top = MAX(gclis_top[i], t);
        int8_t delta_m = vlc_decoder_vertical_old(bitstream, m_top, gtli);
        gclis[i] = m_top + delta_m;
    }
}

static void unpack_verical_pred_gclis_significance_old(bitstream_reader_t* bitstream, precinct_band_t* b_data,
                                                       precinct_band_t* b_data_top, precinct_band_info_t* b_info, const int ypos,
                                                       int run_mode, int32_t band_max_lines) {
    const uint8_t gtli = b_data->gtli;
    const uint8_t gtli_top = (ypos == 0) ? (b_data_top->gtli) : (gtli);
    const int8_t t = MAX(gtli_top, gtli);
    const uint32_t gcli_width = b_info->gcli_width;
    uint8_t* gclis_top = NULL;
    if (ypos == 0) {
        gclis_top = b_data_top->gcli_data + (band_max_lines - 1) * gcli_width;
    }
    else {
        gclis_top = b_data->gcli_data + (ypos - 1) * gcli_width;
    }

    uint8_t* gclis = b_data->gcli_data + ypos * gcli_width;

    for (uint32_t i = 0; i < gcli_width; i++) {
        const int m_top = MAX(gclis_top[i], t);
        int8_t delta_m = 0;
        uint8_t significance_group_is_significant =
            b_data->significance_data[ypos * b_info->significance_width + (i / SIGNIFICANCE_GROUP_SIZE)];

        if (!significance_group_is_significant) {
            delta_m = vlc_decoder_vertical_old(bitstream, m_top, gtli);
        }
        else {
            if (run_mode == 0) {
                delta_m = 0;
            }
            else {
                delta_m = gtli - m_top;
            }
        }
        gclis[i] = m_top + delta_m;
    }
}

TEST(VLC, DISABLED_VariableLenghtCodingPrintMatrix) {
    const uint32_t buffer_size = 200;
    uint8_t buff[buffer_size + 10] = {0};
    bitstream_writer bitstream_write;
    bitstream_reader_t bitstream_read;

    printf("\n r:INFO t: |");
    for (int i = -31; i < 32; ++i) {
        if (i < 0) {
            printf("%3i", i);
        }
        else {
            printf("%2i ", i);
        }
    }

    for (int t = 0; t < 32; ++t) {
        printf("\n r:INFO t: |");
        for (int i = -31; i < 32; ++i) {
            if (i < 0) {
                printf("%3i", i);
            }
            else {
                printf("%2i ", i);
            }
        }
        for (int r = 0; r < 32; ++r) {
            printf("\n r:%2i t:%2i |", r, t);
            for (int i = -31; i < 32; ++i) {
                printf("%2i ", vlc_encode_get_bits(i, r, t));
            }
            printf("\n r:%2i t:%2i |", r, t);
            for (int i = -31; i < 32; ++i) {
                bitstream_writer_init(&bitstream_write, buff, buffer_size);
                bitstream_reader_init(&bitstream_read, buff, buffer_size);
                vlc_encode(&bitstream_write, i, r, t);
                int res = vlc_decoder_old(&bitstream_read, r, t);
                if (res == i) {
                    printf("OK ");
                }
                else {
                    printf("-- ");
                }
            }
        }
    }
}

TEST(VLC, VariableLenghtCoding) {
    setup_common_rtcd_internal(CPU_FLAGS_ALL);

    const uint32_t buffer_size = 200;
    uint8_t buff[buffer_size + 10] = {0};
    bitstream_writer bitstream_write;
    bitstream_reader_t bitstream_read;

    /*Variable Length Coding.
    * Params:
    * x - Coding signed value
    * r - Coding range param
    * t - Coding range param
    * To decode values r and t have to be known.
    * Value of X can get 32 different values in range [xMin, xMax] where
    * xMin = MAX(-16, MIN(t - r, 0));
    * xMax = xMin + 31
    * Range [xMin, xMax] = [xMin, -xMin] U (-xMin, xMax] = [xMin, 0] U (0, -xMin] U (-xMin, xMax]
    * Least coding size is always for value 0.
    * Each value further to the right or left in range [xMin, -xMin] takes up more ~2 bits
    * Each value further to right in range [-xMin, xMax] takes up more 1 bits
    */
    for (int r = -32; r < 32; ++r) {
        for (int t = -32; t < 32; ++t) {
            int xMin = MAX(-16, MIN(t - r, 0));
            int xMax = xMin + 31;
            for (int i = xMin; i <= xMax; ++i) {
                //Reset bitstream
                bitstream_writer_init(&bitstream_write, buff, buffer_size);
                bitstream_reader_init(&bitstream_read, buff, buffer_size);
                //Test default vlc
                vlc_encode(&bitstream_write, i, r, t);
                int result = vlc_decoder_old(&bitstream_read, r, t);
                ASSERT_EQ(i, result);

                //Test get_bits
                bitstream_writer_init(&bitstream_write, buff, buffer_size);
                bitstream_reader_init(&bitstream_read, buff, buffer_size);
                int8_t bits = vlc_encode_get_bits(i, r, t);
                vlc_encode_pack_bits(&bitstream_write, bits);
                result = vlc_decoder_old(&bitstream_read, r, t);
                ASSERT_EQ(i, result);
                if (r == t) {
                    bitstream_writer_init(&bitstream_write, buff, buffer_size);
                    bitstream_reader_init(&bitstream_read, buff, buffer_size);
                    //Test default vlc Write 2 times to cover move cases
                    vlc_encode_simple(&bitstream_write, i);
                    vlc_encode_simple(&bitstream_write, i);
                    result = vlc_decoder_old(&bitstream_read, r, t);
                    ASSERT_EQ(i, result);
                    result = vlc_decoder_old(&bitstream_read, r, t);
                    ASSERT_EQ(i, result);
                }

                //Test fast decode
                bitstream_writer_init(&bitstream_write, buff, buffer_size);
                bitstream_reader_init(&bitstream_read, buff, buffer_size);
                vlc_encode(&bitstream_write, i, r, t);
                //It can read all 32 bits on beginning so add some not used data
                bitstream_writer_add_padding_bits(&bitstream_write, 32);
                result = vlc_decoder_vertical_old(&bitstream_read, r, t);
                ASSERT_EQ(i, result);

                //Test decode from 0
                if (r == t) {
                    bitstream_writer_init(&bitstream_write, buff, buffer_size);
                    bitstream_reader_init(&bitstream_read, buff, buffer_size);
                    vlc_encode(&bitstream_write, i, r, t);
                    //It can read all 32 bits on beginning so add some not used data
                    bitstream_writer_add_padding_bits(&bitstream_write, 32);
                    result = vlc_decoder_pred_zero_old(&bitstream_read);
                    ASSERT_EQ(i, result);
                }
            }
        }
    }
}

TEST(VLC, unpack_pred_zero_gclis_no_significance_compare_old) {
    setup_common_rtcd_internal(CPU_FLAGS_ALL);

    const uint32_t buffer_size = 700;
    uint8_t buff[buffer_size + 10] = {0};
    bitstream_reader_t bitstream_read;
    bitstream_writer bitstream_write;
    const uint32_t count = 30;
    precinct_band_info_t b_info;
    b_info.gcli_width = count;
    precinct_band_t band_org;
    band_org.gtli = 3;
    band_org.gcli_data = (uint8_t*)malloc(2 * count * sizeof(band_org.gcli_data));
    precinct_band_t band_test = band_org;
    band_test.gcli_data += b_info.gcli_width;

    for (uint8_t offset = 0; offset < 8; ++offset) {
        printf("offset: %i \n", offset);
        //Generate bitstream
        bitstream_writer_init(&bitstream_write, buff, buffer_size);
        if (offset) {
            write_N_bits(&bitstream_write, 0, offset);
        }
        for (uint32_t i = 0; i < count; ++i) {
            vlc_encode_pack_bits(&bitstream_write, i);
        }
        uint32_t write_bits = bitstream_writer_get_used_bits(&bitstream_write);

        bitstream_reader_init(&bitstream_read, buff, buffer_size);
        bitstream_reader_skip_bits(&bitstream_read, offset);
        memset(band_org.gcli_data, 0, count * sizeof(band_test.gcli_data));
        unpack_pred_zero_gclis_no_significance_old(&bitstream_read, &band_org, &b_info, 0);

        bitstream_reader_init(&bitstream_read, buff, buffer_size);
        bitstream_reader_skip_bits(&bitstream_read, offset);
        int32_t buffer_size_bits = write_bits;
        memset(band_test.gcli_data, 0, count * sizeof(band_test.gcli_data));

        int32_t bits_left_before_param = buffer_size_bits;
        int32_t bits_left_before_bitstream = (int32_t)bitstream_reader_get_left_bits(&bitstream_read);
        int32_t ret = unpack_pred_zero_gclis_no_significance(&bitstream_read, &band_test, &b_info, 0, &buffer_size_bits);
        ASSERT_EQ(ret, 0);
        ASSERT_EQ(offset, buffer_size_bits);
        //Check correct decrement parameter precinct_bits_left
        ASSERT_EQ(bits_left_before_param - buffer_size_bits,
                  bits_left_before_bitstream - (int32_t)bitstream_reader_get_left_bits(&bitstream_read));

        for (uint32_t i = 0; i < count; ++i) {
            ASSERT_EQ(band_org.gcli_data[i], band_test.gcli_data[i]);
            /*if (band_org.gcli_data[i] != band_test.gcli_data[i]) {
                printf("Invalid values: %i %i\n");
            }*/
            ASSERT_EQ(band_org.gcli_data[i], i + band_org.gtli);
            /*if (band_org.gcli_data[i] != i + band_org.gtli) {
                printf("Invalid values: %i %i %i\n", i, band_org.gcli_data[i], i + band_org.gtli);
            }*/
        }
    }

    free(band_org.gcli_data);
}

TEST(VLC, unpack_pred_zero_gclis_no_significance_invalid_Read_FF) {
    setup_common_rtcd_internal(CPU_FLAGS_ALL);

    const uint32_t buffer_size = 700;
    uint8_t buff[buffer_size + 10] = {0};

    bitstream_reader_t bitstream_read;
    const uint32_t count = 30;
    //Set FF invalid bitstream
    memset(buff, 0xFF, sizeof(buff));

    precinct_band_info_t b_info;
    b_info.gcli_width = count;
    precinct_band_t band_test;
    band_test.gtli = 3;
    band_test.gcli_data = (uint8_t*)malloc(count * sizeof(band_test.gcli_data));

    bitstream_reader_init(&bitstream_read, buff, buffer_size);
    int32_t buffer_size_bits = buffer_size * 8;
    int32_t ret = unpack_pred_zero_gclis_no_significance(&bitstream_read, &band_test, &b_info, 0, &buffer_size_bits);
    ASSERT_NE(ret, 0);
    free(band_test.gcli_data);
}

TEST(VLC, unpack_pred_zero_gclis_no_significance_11_short_bitstream_error) {
    setup_common_rtcd_internal(CPU_FLAGS_ALL);

    const uint32_t buffer_size = 700;
    uint8_t buff[buffer_size + 10] = {0};
    bitstream_reader_t bitstream_read;
    bitstream_writer bitstream_write;
    const uint32_t count = 30;
    precinct_band_info_t b_info;
    b_info.gcli_width = count;
    precinct_band_t band_test;
    band_test.gtli = 3;
    band_test.gcli_data = (uint8_t*)malloc(count * sizeof(band_test.gcli_data));

    for (uint8_t offset = 0; offset < 8; ++offset) {
        printf("offset: %i \n", offset);
        //Generate bitstream
        bitstream_writer_init(&bitstream_write, buff, buffer_size);
        if (offset) {
            write_N_bits(&bitstream_write, 0, offset);
        }
        for (uint32_t i = 0; i < count; ++i) {
            vlc_encode_pack_bits(&bitstream_write, 11); //11 to jumping per bytes boundary
        }
        uint32_t write_bits = bitstream_writer_get_used_bits(&bitstream_write);

        /*Valid bitstream.*/
        bitstream_reader_init(&bitstream_read, buff, buffer_size);
        bitstream_reader_skip_bits(&bitstream_read, offset);
        int32_t buffer_size_bits = write_bits - offset;
        memset(band_test.gcli_data, 0, count * sizeof(band_test.gcli_data));
        int32_t bits_left_before_param = buffer_size_bits;
        int32_t bits_left_before_bitstream = (int32_t)bitstream_reader_get_left_bits(&bitstream_read);
        int32_t ret = unpack_pred_zero_gclis_no_significance(&bitstream_read, &band_test, &b_info, 0, &buffer_size_bits);
        ASSERT_EQ(ret, 0);
        ASSERT_EQ(buffer_size_bits, 0);
        //Check correct decrement parameter precinct_bits_left
        ASSERT_EQ(bits_left_before_param - buffer_size_bits,
                  bits_left_before_bitstream - (int32_t)bitstream_reader_get_left_bits(&bitstream_read));
        for (uint32_t i = 0; i < count; ++i) {
            ASSERT_EQ(11 + band_test.gtli, band_test.gcli_data[i]);
        }

        /*Clipped bitstream.*/
        bitstream_reader_init(&bitstream_read, buff, buffer_size);
        bitstream_reader_skip_bits(&bitstream_read, offset);
        buffer_size_bits = write_bits - 1 - offset;
        memset(band_test.gcli_data, 0, count * sizeof(band_test.gcli_data));
        ret = unpack_pred_zero_gclis_no_significance(&bitstream_read, &band_test, &b_info, 0, &buffer_size_bits);
        ASSERT_NE(ret, 0);
        for (uint32_t i = 0; i < count - 1; ++i) {
            //All without last should be read correctly
            ASSERT_EQ(11 + band_test.gtli, band_test.gcli_data[i]);
        }
    }

    free(band_test.gcli_data);
}

TEST(VLC, unpack_pred_zero_gclis_significance_compare_old) {
    setup_common_rtcd_internal(CPU_FLAGS_ALL);

    const uint32_t buffer_size = 700;
    uint8_t buff[buffer_size + 10] = {0};
    bitstream_reader_t bitstream_read;
    bitstream_writer bitstream_write;
    const uint32_t count = 30;
    const uint32_t significance_groups = (count + SIGNIFICANCE_GROUP_SIZE - 1) / SIGNIFICANCE_GROUP_SIZE;
    precinct_band_info_t b_info;
    b_info.gcli_width = count;
    b_info.significance_width = significance_groups;
    precinct_band_t band_org;
    band_org.gtli = 3;
    band_org.gcli_data = (uint8_t*)malloc(2 * count * sizeof(band_org.gcli_data));
    uint8_t significance_data_array[significance_groups];
    band_org.significance_data = significance_data_array;
    precinct_band_t band_test = band_org;
    band_test.gcli_data += b_info.gcli_width;

    for (uint32_t sig_test_id = 0; sig_test_id < 4; ++sig_test_id) {
        memset(significance_data_array, 0, sizeof(significance_data_array)); //Read ALL
        if (sig_test_id == 1) {
            for (uint32_t id = 0; id < significance_groups; id += 2) {
                significance_data_array[id] = 1; //Read Odd groups
            }
        }
        else if (sig_test_id == 2) {
            for (uint32_t id = 1; id < significance_groups; id += 2) {
                significance_data_array[id] = 1; //Read Even groups
            }
        }
        else if (sig_test_id == 3) {
            memset(significance_data_array, 1, sizeof(significance_data_array)); //Read NONE
        }
        else {
            ASSERT_TRUE(sig_test_id == 0);
        }

        for (uint8_t offset = 0; offset < 8; ++offset) {
            printf("sig_test_id %u, offset: %i \n", sig_test_id, offset);
            //Generate bitstream
            bitstream_writer_init(&bitstream_write, buff, buffer_size);
            if (offset) {
                write_N_bits(&bitstream_write, 0, offset);
            }
            for (uint32_t i = 0; i < count; ++i) {
                if (significance_data_array[i / SIGNIFICANCE_GROUP_SIZE] == 0) {
                    vlc_encode_pack_bits(&bitstream_write, i);
                }
            }
            uint32_t write_bits = bitstream_writer_get_used_bits(&bitstream_write);

            bitstream_reader_init(&bitstream_read, buff, buffer_size);
            bitstream_reader_skip_bits(&bitstream_read, offset);
            memset(band_org.gcli_data, 0, count * sizeof(band_test.gcli_data));
            unpack_pred_zero_gclis_significance_old(&bitstream_read, &band_org, &b_info, 0);

            bitstream_reader_init(&bitstream_read, buff, buffer_size);
            bitstream_reader_skip_bits(&bitstream_read, offset);
            int32_t buffer_size_bits = write_bits;
            memset(band_test.gcli_data, 0, count * sizeof(band_test.gcli_data));
            int32_t bits_left_before_param = buffer_size_bits;
            int32_t bits_left_before_bitstream = (int32_t)bitstream_reader_get_left_bits(&bitstream_read);
            int32_t ret = unpack_pred_zero_gclis_significance(&bitstream_read, &band_test, &b_info, 0, &buffer_size_bits);
            ASSERT_EQ(ret, 0);
            ASSERT_EQ(offset, buffer_size_bits);
            //Check correct decrement parameter precinct_bits_left
            ASSERT_EQ(bits_left_before_param - buffer_size_bits,
                      bits_left_before_bitstream - (int32_t)bitstream_reader_get_left_bits(&bitstream_read));

            for (uint32_t i = 0; i < count; ++i) {
                ASSERT_EQ(band_org.gcli_data[i], band_test.gcli_data[i]);
                if (significance_data_array[i / SIGNIFICANCE_GROUP_SIZE] == 0) {
                    ASSERT_EQ(band_org.gcli_data[i], i + band_org.gtli);
                }
                else {
                    ASSERT_EQ(band_org.gcli_data[i], band_org.gtli);
                }
            }
        }
    }

    free(band_org.gcli_data);
}

TEST(VLC, unpack_pred_zero_gclis_significance_invalid_Read_FF) {
    setup_common_rtcd_internal(CPU_FLAGS_ALL);

    const uint32_t buffer_size = 700;
    uint8_t buff[buffer_size + 10] = {0};

    bitstream_reader_t bitstream_read;
    const uint32_t count = 30;
    const uint32_t significance_groups = (count + SIGNIFICANCE_GROUP_SIZE - 1) / SIGNIFICANCE_GROUP_SIZE;
    //Set FF invalid bitstream
    memset(buff, 0xFF, sizeof(buff));

    precinct_band_info_t b_info;
    b_info.gcli_width = count;
    b_info.significance_width = significance_groups;
    precinct_band_t band_test;
    band_test.gtli = 3;
    band_test.gcli_data = (uint8_t*)malloc(count * sizeof(band_test.gcli_data));
    uint8_t significance_data_array[significance_groups];
    band_test.significance_data = significance_data_array;
    memset(significance_data_array, 0, sizeof(significance_data_array)); //Read ALL

    bitstream_reader_init(&bitstream_read, buff, buffer_size);
    int32_t buffer_size_bits = buffer_size * 8;
    int32_t ret = unpack_pred_zero_gclis_significance(&bitstream_read, &band_test, &b_info, 0, &buffer_size_bits);
    ASSERT_NE(ret, 0);
    free(band_test.gcli_data);
}

TEST(VLC, unpack_pred_zero_gclis_significance_11_short_bitstream_error) {
    setup_common_rtcd_internal(CPU_FLAGS_ALL);

    const uint32_t buffer_size = 700;
    uint8_t buff[buffer_size + 10] = {0};
    bitstream_reader_t bitstream_read;
    bitstream_writer bitstream_write;
    const uint32_t count = 30;
    const uint32_t significance_groups = (count + SIGNIFICANCE_GROUP_SIZE - 1) / SIGNIFICANCE_GROUP_SIZE;
    precinct_band_info_t b_info;
    b_info.gcli_width = count;
    b_info.significance_width = significance_groups;
    precinct_band_t band_test;
    band_test.gtli = 3;
    band_test.gcli_data = (uint8_t*)malloc(count * sizeof(band_test.gcli_data));
    uint8_t significance_data_array[significance_groups];
    band_test.significance_data = significance_data_array;

    for (uint32_t sig_test_id = 0; sig_test_id < 4; ++sig_test_id) {
        memset(significance_data_array, 0, sizeof(significance_data_array)); //Read ALL
        if (sig_test_id == 1) {
            for (uint32_t id = 0; id < significance_groups; id += 2) {
                significance_data_array[id] = 1; //Read Odd groups
            }
        }
        else if (sig_test_id == 2) {
            for (uint32_t id = 1; id < significance_groups; id += 2) {
                significance_data_array[id] = 1; //Read Even groups
            }
        }
        else if (sig_test_id == 3) {
            memset(significance_data_array, 1, sizeof(significance_data_array)); //Read NONE
        }
        else {
            ASSERT_TRUE(sig_test_id == 0);
        }
        for (uint8_t offset = 0; offset < 8; ++offset) {
            printf("offset: %i \n", offset);
            //Generate bitstream
            bitstream_writer_init(&bitstream_write, buff, buffer_size);
            if (offset) {
                write_N_bits(&bitstream_write, 0, offset);
            }
            for (uint32_t i = 0; i < count; ++i) {
                if (significance_data_array[i / SIGNIFICANCE_GROUP_SIZE] == 0) {
                    vlc_encode_pack_bits(&bitstream_write, 11); //11 to jumping per bytes boundary
                }
            }
            uint32_t write_bits = bitstream_writer_get_used_bits(&bitstream_write);

            /*Valid bitstream.*/
            bitstream_reader_init(&bitstream_read, buff, buffer_size);
            bitstream_reader_skip_bits(&bitstream_read, offset);
            int32_t buffer_size_bits = write_bits - offset;
            memset(band_test.gcli_data, 0, count * sizeof(band_test.gcli_data));
            int32_t bits_left_before_param = buffer_size_bits;
            int32_t bits_left_before_bitstream = (int32_t)bitstream_reader_get_left_bits(&bitstream_read);
            int32_t ret = unpack_pred_zero_gclis_significance(&bitstream_read, &band_test, &b_info, 0, &buffer_size_bits);
            ASSERT_EQ(ret, 0);
            ASSERT_EQ(buffer_size_bits, 0);
            //Check correct decrement parameter precinct_bits_left
            ASSERT_EQ(bits_left_before_param - buffer_size_bits,
                      bits_left_before_bitstream - (int32_t)bitstream_reader_get_left_bits(&bitstream_read));

            for (uint32_t i = 0; i < count; ++i) {
                if (significance_data_array[i / SIGNIFICANCE_GROUP_SIZE] == 0) {
                    ASSERT_EQ(11 + band_test.gtli, band_test.gcli_data[i]);
                }
                else {
                    ASSERT_EQ(band_test.gtli, band_test.gcli_data[i]);
                }
            }

            if (sig_test_id < 3) {
                /*Clipped bitstream.*/
                bitstream_reader_init(&bitstream_read, buff, buffer_size);
                bitstream_reader_skip_bits(&bitstream_read, offset);
                buffer_size_bits = write_bits - 1 - offset;
                memset(band_test.gcli_data, 0, count * sizeof(band_test.gcli_data));
                ret = unpack_pred_zero_gclis_significance(&bitstream_read, &band_test, &b_info, 0, &buffer_size_bits);
                ASSERT_NE(ret, 0);
                for (uint32_t i = 0; i < count - SIGNIFICANCE_GROUP_SIZE; ++i) {
                    if (significance_data_array[i / SIGNIFICANCE_GROUP_SIZE] == 0) {
                        //All without last should be read correctly
                        ASSERT_EQ(11 + band_test.gtli, band_test.gcli_data[i]);
                    }
                    else {
                        ASSERT_EQ(band_test.gtli, band_test.gcli_data[i]);
                    }
                }
            }
        }
    }

    free(band_test.gcli_data);
}

static int8_t test_vpred_vlc(int8_t value_vlc, precinct_band_t* b_data, precinct_band_t* b_data_top, const int ypos,
                             const int i) {
    /*VLC decode vertical.*/
    int8_t ttt = MAX(b_data->gtli, b_data->gtli);
    if (ypos == 0) {
        ttt = MAX(b_data_top->gtli, b_data->gtli);
    }
    int m_top = MAX(b_data_top->gcli_data[i], ttt);
    if (ypos) {
        m_top = MAX(b_data->gcli_data[i], ttt);
    }
    int8_t delta_m = 0;
    int treshold = MAX(m_top - b_data->gtli, 0);

    if (value_vlc > 2 * treshold) {
        delta_m = value_vlc - treshold;
    }
    else if (value_vlc > 0) {
        if (value_vlc & 1) {
            delta_m = -((value_vlc + 1) / 2);
        }
        else {
            delta_m = value_vlc / 2;
        }
    }
    return m_top + delta_m;
}

static int8_t test_vpred_run_mode(int run_mode, precinct_band_t* b_data, precinct_band_t* b_data_top, const int ypos, int i) {
    /*VLC decode vertical.*/
    int8_t ttt = MAX(b_data->gtli, b_data->gtli);
    if (ypos == 0) {
        ttt = MAX(b_data_top->gtli, b_data->gtli);
    }
    int m_top = MAX(b_data_top->gcli_data[i], ttt);
    if (ypos) {
        m_top = MAX(b_data->gcli_data[i], ttt);
    }
    int8_t delta_m = 0;
    if (run_mode == 0) {
        delta_m = 0;
    }
    else {
        delta_m = b_data->gtli - m_top;
    }
    return m_top + delta_m;
}

TEST(VLC, unpack_verical_pred_gclis_no_significance_compare_old) {
    setup_common_rtcd_internal(CPU_FLAGS_ALL);

    const uint32_t buffer_size = 700;
    uint8_t buff[buffer_size + 10] = {0};
    bitstream_reader_t bitstream_read;
    bitstream_writer bitstream_write;
    const uint32_t count = 30;
    precinct_band_info_t b_info;
    b_info.gcli_width = count;
    precinct_band_t band_top;
    band_top.gcli_data = (uint8_t*)malloc(count * sizeof(band_top.gcli_data));
    band_top.gtli = 4;
    for (uint32_t i = 0; i < count; ++i) {
        band_top.gcli_data[i] = count - i;
    }
    precinct_band_t band_org;
    band_org.gtli = 3;
    band_org.gcli_data = (uint8_t*)malloc(4 * count * sizeof(band_org.gcli_data));
    precinct_band_t band_test = band_org;
    band_test.gcli_data += 2 * b_info.gcli_width;

    for (int ypos = 0; ypos < 2; ++ypos) {
        for (uint8_t offset = 0; offset < 8; ++offset) {
            printf("offset: %i \n", offset);
            //Generate bitstream
            bitstream_writer_init(&bitstream_write, buff, buffer_size);
            if (offset) {
                write_N_bits(&bitstream_write, 0, offset);
            }
            for (uint32_t i = 0; i < count; ++i) {
                vlc_encode_pack_bits(&bitstream_write, i);
            }
            uint32_t write_bits = bitstream_writer_get_used_bits(&bitstream_write);

            bitstream_reader_init(&bitstream_read, buff, buffer_size);
            bitstream_reader_skip_bits(&bitstream_read, offset);
            memset(band_org.gcli_data, 0, 2 * count * sizeof(band_test.gcli_data));
            unpack_verical_pred_gclis_no_significance_old(&bitstream_read, &band_org, &band_top, &b_info, ypos, 1);

            bitstream_reader_init(&bitstream_read, buff, buffer_size);
            bitstream_reader_skip_bits(&bitstream_read, offset);
            int32_t buffer_size_bits = write_bits;
            memset(band_test.gcli_data, 0, 2 * count * sizeof(band_test.gcli_data));
            int32_t bits_left_before_param = buffer_size_bits;
            int32_t bits_left_before_bitstream = (int32_t)bitstream_reader_get_left_bits(&bitstream_read);
            int32_t ret = unpack_verical_pred_gclis_no_significance(
                &bitstream_read, &band_test, &band_top, &b_info, ypos, 1, &buffer_size_bits);
            ASSERT_EQ(ret, 0);
            ASSERT_EQ(offset, buffer_size_bits);
            //Check correct decrement parameter precinct_bits_left
            ASSERT_EQ(bits_left_before_param - buffer_size_bits,
                      bits_left_before_bitstream - (int32_t)bitstream_reader_get_left_bits(&bitstream_read));

            for (uint32_t i = 0; i < count; ++i) {
                ASSERT_EQ((band_org.gcli_data + ypos * b_info.gcli_width)[i],
                          (band_test.gcli_data + ypos * b_info.gcli_width)[i]);
                /*VLC decode vertical*/
                int8_t gclis_calc = test_vpred_vlc(i, &band_org, &band_top, ypos, i);
                ASSERT_EQ((band_org.gcli_data + ypos * b_info.gcli_width)[i], gclis_calc);
            }
        }
    }

    free(band_org.gcli_data);
    free(band_top.gcli_data);
}

TEST(VLC, unpack_verical_pred_gclis_no_significance_invalid_Read_FF) {
    setup_common_rtcd_internal(CPU_FLAGS_ALL);

    const uint32_t buffer_size = 700;
    uint8_t buff[buffer_size + 10] = {0};

    bitstream_reader_t bitstream_read;
    const uint32_t count = 30;
    precinct_band_t band_top;
    band_top.gtli = 4;
    band_top.gcli_data = (uint8_t*)malloc(2 * count * sizeof(band_top.gcli_data));
    for (uint32_t i = 0; i < 2 * count; ++i) {
        band_top.gcli_data[i] = 2 * count - i;
    }

    //Set FF invalid bitstream
    memset(buff, 0xFF, sizeof(buff));

    precinct_band_info_t b_info;
    b_info.gcli_width = count;
    precinct_band_t band_test;
    band_test.gtli = 3;
    band_test.gcli_data = (uint8_t*)malloc(2 * count * sizeof(band_test.gcli_data));

    for (int ypos = 0; ypos < 2; ++ypos) {
        bitstream_reader_init(&bitstream_read, buff, buffer_size);
        int32_t buffer_size_bits = buffer_size * 8;
        int32_t ret = unpack_verical_pred_gclis_no_significance(
            &bitstream_read, &band_test, &band_top, &b_info, ypos, 1, &buffer_size_bits);
        ASSERT_NE(ret, 0);
    }

    free(band_test.gcli_data);
    free(band_top.gcli_data);
}

TEST(VLC, unpack_verical_pred_gclis_no_significance_11_short_bitstream_error) {
    setup_common_rtcd_internal(CPU_FLAGS_ALL);

    const uint32_t buffer_size = 700;
    uint8_t buff[buffer_size + 10] = {0};
    bitstream_reader_t bitstream_read;
    bitstream_writer bitstream_write;
    const uint32_t count = 30;
    precinct_band_t band_top;
    band_top.gtli = 4;
    band_top.gcli_data = (uint8_t*)malloc(2 * count * sizeof(band_top.gcli_data));
    for (uint32_t i = 0; i < 2 * count; ++i) {
        band_top.gcli_data[i] = 2 * count - i;
    }
    precinct_band_info_t b_info;
    b_info.gcli_width = count;
    precinct_band_t band_org;
    band_org.gtli = 3;
    band_org.gcli_data = (uint8_t*)malloc(2 * count * sizeof(band_org.gcli_data));
    int32_t value = 11;

    for (int ypos = 0; ypos < 2; ++ypos) {
        for (uint8_t offset = 0; offset < 8; ++offset) {
            printf("offset: %i \n", offset);
            //Generate bitstream
            bitstream_writer_init(&bitstream_write, buff, buffer_size);
            if (offset) {
                write_N_bits(&bitstream_write, 0, offset);
            }
            for (uint32_t i = 0; i < count; ++i) {
                vlc_encode_pack_bits(&bitstream_write, value); //11 to jumping per bytes boundary
            }
            uint32_t write_bits = bitstream_writer_get_used_bits(&bitstream_write);

            /*Valid bitstream.*/
            bitstream_reader_init(&bitstream_read, buff, buffer_size);
            bitstream_reader_skip_bits(&bitstream_read, offset);
            int32_t buffer_size_bits = write_bits - offset;
            memset(band_org.gcli_data, 0, 2 * count * sizeof(band_org.gcli_data));
            int32_t bits_left_before_param = buffer_size_bits;
            int32_t bits_left_before_bitstream = (int32_t)bitstream_reader_get_left_bits(&bitstream_read);
            int32_t ret = unpack_verical_pred_gclis_no_significance(
                &bitstream_read, &band_org, &band_top, &b_info, ypos, 1, &buffer_size_bits);
            ASSERT_EQ(ret, 0);
            ASSERT_EQ(buffer_size_bits, 0);
            //Check correct decrement parameter precinct_bits_left
            ASSERT_EQ(bits_left_before_param - buffer_size_bits,
                      bits_left_before_bitstream - (int32_t)bitstream_reader_get_left_bits(&bitstream_read));

            for (uint32_t i = 0; i < count; ++i) {
                /*VLC decode vertical*/
                int8_t gclis_calc = test_vpred_vlc(value, &band_org, &band_top, ypos, i);
                ASSERT_EQ((band_org.gcli_data + ypos * b_info.gcli_width)[i], gclis_calc);
            }

            /*Clipped bitstream.*/
            bitstream_reader_init(&bitstream_read, buff, buffer_size);
            bitstream_reader_skip_bits(&bitstream_read, offset);
            buffer_size_bits = write_bits - 1 - offset;
            memset(band_org.gcli_data, 0, 2 * count * sizeof(band_org.gcli_data));
            ret = unpack_verical_pred_gclis_no_significance(
                &bitstream_read, &band_org, &band_top, &b_info, ypos, 1, &buffer_size_bits);
            ASSERT_NE(ret, 0);
            for (uint32_t i = 0; i < count - 1; ++i) {
                //All without last should be read correctly
                /*VLC decode vertical*/
                int8_t gclis_calc = test_vpred_vlc(value, &band_org, &band_top, ypos, i);
                ASSERT_EQ((band_org.gcli_data + ypos * b_info.gcli_width)[i], gclis_calc);
            }
        }
    }

    free(band_org.gcli_data);
    free(band_top.gcli_data);
}

TEST(VLC, unpack_verical_pred_gclis_significance_compare_old) {
    setup_common_rtcd_internal(CPU_FLAGS_ALL);

    const uint32_t buffer_size = 700;
    uint8_t buff[buffer_size + 10] = {0};
    bitstream_reader_t bitstream_read;
    bitstream_writer bitstream_write;
    const uint32_t count = 30;
    const uint32_t significance_groups = (count + SIGNIFICANCE_GROUP_SIZE - 1) / SIGNIFICANCE_GROUP_SIZE;
    precinct_band_info_t b_info;
    b_info.gcli_width = count;
    b_info.significance_width = significance_groups;
    precinct_band_t band_top;
    band_top.gcli_data = (uint8_t*)malloc(count * sizeof(band_top.gcli_data));
    band_top.gtli = 4;
    for (uint32_t i = 0; i < count; ++i) {
        band_top.gcli_data[i] = count - i;
    }
    precinct_band_t band_org;
    band_org.gtli = 3;
    band_org.gcli_data = (uint8_t*)malloc(4 * count * sizeof(band_org.gcli_data));
    uint8_t significance_data_array[2 * significance_groups];
    band_org.significance_data = significance_data_array;
    precinct_band_t band_test = band_org;
    band_test.gcli_data += 2 * b_info.gcli_width;

    memset(significance_data_array, 0, sizeof(significance_data_array)); //Read ALL

    for (uint32_t sig_test_id = 0; sig_test_id < 4; ++sig_test_id) {
        memset(significance_data_array, 0, sizeof(significance_data_array)); //Read ALL
        if (sig_test_id == 1) {
            for (uint32_t id = 0; id < 2 * significance_groups; id += 2) {
                significance_data_array[id] = 1; //Read Odd groups
            }
        }
        else if (sig_test_id == 2) {
            for (uint32_t id = 1; id < 2 * significance_groups; id += 2) {
                significance_data_array[id] = 1; //Read Even groups
            }
        }
        else if (sig_test_id == 3) {
            memset(significance_data_array, 1, sizeof(significance_data_array)); //Read NONE
        }
        else {
            ASSERT_TRUE(sig_test_id == 0);
        }

        for (int revert_values = 0; revert_values < 2; ++revert_values) {
            for (int run_mode = 0; run_mode < 2; ++run_mode) {
                for (int ypos = 0; ypos < 2; ++ypos) {
                    for (uint8_t offset = 0; offset < 8; ++offset) {
                        printf("offset: %i \n", offset);
                        //Generate bitstream
                        bitstream_writer_init(&bitstream_write, buff, buffer_size);
                        if (offset) {
                            write_N_bits(&bitstream_write, 0, offset);
                        }
                        for (uint32_t i = 0; i < count; ++i) {
                            if (significance_data_array[i / SIGNIFICANCE_GROUP_SIZE] == 0) {
                                if (revert_values) {
                                    vlc_encode_pack_bits(&bitstream_write, count - i);
                                }
                                else {
                                    vlc_encode_pack_bits(&bitstream_write, i);
                                }
                            }
                        }
                        uint32_t write_bits = bitstream_writer_get_used_bits(&bitstream_write);

                        bitstream_reader_init(&bitstream_read, buff, buffer_size);
                        bitstream_reader_skip_bits(&bitstream_read, offset);
                        memset(band_org.gcli_data, 0, 2 * count * sizeof(band_test.gcli_data));
                        unpack_verical_pred_gclis_significance_old(
                            &bitstream_read, &band_org, &band_top, &b_info, ypos, run_mode, 1);

                        bitstream_reader_init(&bitstream_read, buff, buffer_size);
                        bitstream_reader_skip_bits(&bitstream_read, offset);
                        int32_t buffer_size_bits = write_bits;
                        memset(band_test.gcli_data, 0, 2 * count * sizeof(band_test.gcli_data));
                        int32_t bits_left_before_param = buffer_size_bits;
                        int32_t bits_left_before_bitstream = (int32_t)bitstream_reader_get_left_bits(&bitstream_read);
                        int32_t ret = unpack_verical_pred_gclis_significance(
                            &bitstream_read, &band_test, &band_top, &b_info, ypos, run_mode, 1, &buffer_size_bits);
                        ASSERT_EQ(ret, 0);
                        ASSERT_EQ(offset, buffer_size_bits);
                        //Check correct decrement parameter precinct_bits_left
                        ASSERT_EQ(bits_left_before_param - buffer_size_bits,
                                  bits_left_before_bitstream - (int32_t)bitstream_reader_get_left_bits(&bitstream_read));

                        for (uint32_t i = 0; i < count; ++i) {
                            ASSERT_EQ((band_org.gcli_data + ypos * b_info.gcli_width)[i],
                                      (band_test.gcli_data + ypos * b_info.gcli_width)[i]);
                            if (significance_data_array[i / SIGNIFICANCE_GROUP_SIZE] == 0) {
                                /*VLC decode vertical*/
                                int8_t value = i;
                                if (revert_values) {
                                    value = count - i;
                                }
                                int8_t gclis_calc = test_vpred_vlc(value, &band_org, &band_top, ypos, i);
                                ASSERT_EQ((band_org.gcli_data + ypos * b_info.gcli_width)[i], gclis_calc);
                            }
                            else {
                                int8_t gclis_calc = test_vpred_run_mode(run_mode, &band_org, &band_top, ypos, i);
                                ASSERT_EQ((band_org.gcli_data + ypos * b_info.gcli_width)[i], gclis_calc);
                            }
                        }
                    }
                }
            }
        }
    }

    free(band_org.gcli_data);
    free(band_top.gcli_data);
}

TEST(VLC, unpack_verical_pred_gclis_significance_invalid_Read_FF) {
    setup_common_rtcd_internal(CPU_FLAGS_ALL);

    const uint32_t buffer_size = 700;
    uint8_t buff[buffer_size + 10] = {0};

    bitstream_reader_t bitstream_read;
    const uint32_t count = 30;
    const uint32_t significance_groups = (count + SIGNIFICANCE_GROUP_SIZE - 1) / SIGNIFICANCE_GROUP_SIZE;
    precinct_band_t band_top;
    band_top.gtli = 4;
    band_top.gcli_data = (uint8_t*)malloc(2 * count * sizeof(band_top.gcli_data));
    for (uint32_t i = 0; i < 2 * count; ++i) {
        band_top.gcli_data[i] = 2 * count - i;
    }

    //Set FF invalid bitstream
    memset(buff, 0xFF, sizeof(buff));

    precinct_band_info_t b_info;
    b_info.gcli_width = count;
    b_info.significance_width = significance_groups;
    precinct_band_t band_test;
    band_test.gtli = 3;
    band_test.gcli_data = (uint8_t*)malloc(2 * count * sizeof(band_test.gcli_data));
    uint8_t significance_data_array[2 * significance_groups];
    band_test.significance_data = significance_data_array;

    for (uint32_t sig_test_id = 0; sig_test_id < 3; ++sig_test_id) {
        memset(significance_data_array, 0, sizeof(significance_data_array)); //Read ALL
        if (sig_test_id == 1) {
            for (uint32_t id = 0; id < 2 * significance_groups; id += 2) {
                significance_data_array[id] = 1; //Read Odd groups
            }
        }
        else if (sig_test_id == 2) {
            for (uint32_t id = 1; id < 2 * significance_groups; id += 2) {
                significance_data_array[id] = 1; //Read Even groups
            }
        }
        else {
            ASSERT_TRUE(sig_test_id == 0);
        }
        for (int run_mode = 0; run_mode < 2; ++run_mode) {
            for (int ypos = 0; ypos < 2; ++ypos) {
                bitstream_reader_init(&bitstream_read, buff, buffer_size);
                int32_t buffer_size_bits = buffer_size * 8;
                int32_t ret = unpack_verical_pred_gclis_significance(
                    &bitstream_read, &band_test, &band_top, &b_info, ypos, run_mode, 1, &buffer_size_bits);
                ASSERT_NE(ret, 0);
            }
        }
    }

    free(band_test.gcli_data);
    free(band_top.gcli_data);
}

TEST(VLC, unpack_verical_pred_gclis_significance_11_short_bitstream_error) {
    setup_common_rtcd_internal(CPU_FLAGS_ALL);

    const uint32_t buffer_size = 700;
    uint8_t buff[buffer_size + 10] = {0};
    bitstream_reader_t bitstream_read;
    bitstream_writer bitstream_write;
    const uint32_t count = 30;
    const uint32_t significance_groups = (count + SIGNIFICANCE_GROUP_SIZE - 1) / SIGNIFICANCE_GROUP_SIZE;
    precinct_band_t band_top;
    band_top.gtli = 4;
    band_top.gcli_data = (uint8_t*)malloc(2 * count * sizeof(band_top.gcli_data));
    for (uint32_t i = 0; i < 2 * count; ++i) {
        band_top.gcli_data[i] = 2 * count - i;
    }
    precinct_band_info_t b_info;
    b_info.gcli_width = count;
    b_info.significance_width = significance_groups;
    precinct_band_t band_org;
    band_org.gtli = 3;
    band_org.gcli_data = (uint8_t*)malloc(2 * count * sizeof(band_org.gcli_data));
    uint8_t significance_data_array[2 * significance_groups];
    band_org.significance_data = significance_data_array;
    int32_t value = 11;

    for (uint32_t sig_test_id = 0; sig_test_id < 4; ++sig_test_id) {
        memset(significance_data_array, 0, sizeof(significance_data_array)); //Read ALL
        if (sig_test_id == 1) {
            for (uint32_t id = 0; id < 2 * significance_groups; id += 2) {
                significance_data_array[id] = 1; //Read Odd groups
            }
        }
        else if (sig_test_id == 2) {
            for (uint32_t id = 1; id < 2 * significance_groups; id += 2) {
                significance_data_array[id] = 1; //Read Even groups
            }
        }
        else if (sig_test_id == 3) {
            memset(significance_data_array, 1, sizeof(significance_data_array)); //Read NONE
        }
        else {
            ASSERT_TRUE(sig_test_id == 0);
        }
        for (int run_mode = 0; run_mode < 2; ++run_mode) {
            for (int ypos = 0; ypos < 2; ++ypos) {
                for (uint8_t offset = 0; offset < 8; ++offset) {
                    printf("offset: %i \n", offset);
                    //Generate bitstream
                    bitstream_writer_init(&bitstream_write, buff, buffer_size);
                    if (offset) {
                        write_N_bits(&bitstream_write, 0, offset);
                    }
                    for (uint32_t i = 0; i < count; ++i) {
                        if (significance_data_array[i / SIGNIFICANCE_GROUP_SIZE] == 0) {
                            vlc_encode_pack_bits(&bitstream_write, value); //11 to jumping per bytes boundary
                        }
                    }
                    uint32_t write_bits = bitstream_writer_get_used_bits(&bitstream_write);

                    /*Valid bitstream.*/
                    bitstream_reader_init(&bitstream_read, buff, buffer_size);
                    bitstream_reader_skip_bits(&bitstream_read, offset);
                    int32_t buffer_size_bits = write_bits - offset;
                    memset(band_org.gcli_data, 0, 2 * count * sizeof(band_org.gcli_data));
                    int32_t bits_left_before_param = buffer_size_bits;
                    int32_t bits_left_before_bitstream = (int32_t)bitstream_reader_get_left_bits(&bitstream_read);
                    int32_t ret = unpack_verical_pred_gclis_significance(
                        &bitstream_read, &band_org, &band_top, &b_info, ypos, run_mode, 1, &buffer_size_bits);
                    ASSERT_EQ(ret, 0);
                    ASSERT_EQ(buffer_size_bits, 0);
                    //Check correct decrement parameter precinct_bits_left
                    ASSERT_EQ(bits_left_before_param - buffer_size_bits,
                              bits_left_before_bitstream - (int32_t)bitstream_reader_get_left_bits(&bitstream_read));

                    for (uint32_t i = 0; i < count; ++i) {
                        if (significance_data_array[i / SIGNIFICANCE_GROUP_SIZE] == 0) {
                            /*VLC decode vertical*/
                            int8_t gclis_calc = test_vpred_vlc(value, &band_org, &band_top, ypos, i);
                            ASSERT_EQ((band_org.gcli_data + ypos * b_info.gcli_width)[i], gclis_calc);
                        }
                        else {
                            int8_t gclis_calc = test_vpred_run_mode(run_mode, &band_org, &band_top, ypos, i);
                            ASSERT_EQ((band_org.gcli_data + ypos * b_info.gcli_width)[i], gclis_calc);
                        }
                    }

                    if (sig_test_id < 3) {
                        /*Clipped bitstream.*/
                        bitstream_reader_init(&bitstream_read, buff, buffer_size);
                        bitstream_reader_skip_bits(&bitstream_read, offset);
                        buffer_size_bits = write_bits - 1 - offset;
                        memset(band_org.gcli_data, 0, 2 * count * sizeof(band_org.gcli_data));
                        ret = unpack_verical_pred_gclis_significance(
                            &bitstream_read, &band_org, &band_top, &b_info, ypos, run_mode, 1, &buffer_size_bits);
                        ASSERT_NE(ret, 0);
                        for (uint32_t i = 0; i < count - SIGNIFICANCE_GROUP_SIZE; ++i) {
                            //All without last should be read correctly
                            /*VLC decode vertical*/
                            if (significance_data_array[i / SIGNIFICANCE_GROUP_SIZE] == 0) {
                                /*VLC decode vertical*/
                                int8_t gclis_calc = test_vpred_vlc(value, &band_org, &band_top, ypos, i);
                                ASSERT_EQ((band_org.gcli_data + ypos * b_info.gcli_width)[i], gclis_calc);
                            }
                            else {
                                int8_t gclis_calc = test_vpred_run_mode(run_mode, &band_org, &band_top, ypos, i);
                                ASSERT_EQ((band_org.gcli_data + ypos * b_info.gcli_width)[i], gclis_calc);
                            }
                        }
                    }
                }
            }
        }
    }

    free(band_org.gcli_data);
    free(band_top.gcli_data);
}
