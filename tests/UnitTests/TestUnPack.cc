/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "random.h"
#include "UnPack_avx2.h"
#include "Packing.h"
#include "BitstreamWriter.h"

typedef SvtJxsErrorType_t (*unpack_data)(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis,
                                         uint32_t group_size, uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num,
                                         int32_t* precinct_bits_left);

SvtJxsErrorType_t unpack_data_old(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis, uint32_t group_size,
                                  uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num, int32_t* precinct_bits_left) {
    uint32_t idx = 0;
    const uint32_t group_num = w / group_size;
    const uint32_t leftover = w % group_size;
    if (sign_flag == 0) {
        for (uint32_t group = 0; group < group_num; group++) {
            memset(&buf[idx], 0, sizeof(uint16_t) * group_size);
            if (gclis[group] > gtli) {
                for (uint32_t i = 0; i < group_size; i++) {
                    if (--(*precinct_bits_left) < 0) {
                        return SvtJxsErrorDecoderInvalidBitstream;
                    }
                    buf[idx + i] = read_1_bit(bitstream);
                    buf[idx + i] <<= BITSTREAM_BIT_POSITION_SIGN;
                }
                for (int32_t bits = gclis[group] - 1; bits >= gtli; --bits) {
                    for (uint32_t i = 0; i < group_size; i++) {
                        if (--(*precinct_bits_left) < 0) {
                            return SvtJxsErrorDecoderInvalidBitstream;
                        }
                        buf[idx + i] |= (uint16_t)(read_1_bit(bitstream)) << bits;
                    }
                }
            }
            idx += group_size;
        }

        if (leftover) {
            if (gclis[group_num] > gtli) {
                for (uint32_t i = 0; i < group_size; i++) {
                    if (i < leftover) {
                        if (--(*precinct_bits_left) < 0) {
                            return SvtJxsErrorDecoderInvalidBitstream;
                        }
                        buf[idx + i] = read_1_bit(bitstream);
                        buf[idx + i] <<= BITSTREAM_BIT_POSITION_SIGN;
                    }
                    else {
                        if (--(*precinct_bits_left) < 0) {
                            return SvtJxsErrorDecoderInvalidBitstream;
                        }
                        read_1_bit(bitstream);
                    }
                }
                for (int32_t bits = gclis[group_num] - 1; bits >= gtli; --bits) {
                    for (uint32_t i = 0; i < group_size; i++) {
                        if (i < leftover) {
                            if (--(*precinct_bits_left) < 0) {
                                return SvtJxsErrorDecoderInvalidBitstream;
                            }
                            buf[idx + i] |= (uint16_t)(read_1_bit(bitstream)) << bits;
                        }
                        else {
                            if (--(*precinct_bits_left) < 0) {
                                return SvtJxsErrorDecoderInvalidBitstream;
                            }
                            read_1_bit(bitstream);
                        }
                    }
                }
            }
        }
    }
    else {
        for (uint32_t group = 0; group < group_num; group++) {
            memset(&buf[idx], 0, sizeof(uint16_t) * group_size);
            if (gclis[group] > gtli) {
                for (int32_t bits = gclis[group] - 1; bits >= gtli; --bits) {
                    for (uint32_t i = 0; i < group_size; i++) {
                        if (--(*precinct_bits_left) < 0) {
                            return SvtJxsErrorDecoderInvalidBitstream;
                        }
                        buf[idx + i] |= (uint16_t)(read_1_bit(bitstream)) << bits;
                    }
                }
            }
            idx += group_size;
        }
        if (leftover) {
            memset(&buf[idx], 0, sizeof(uint16_t) * group_size);
            *leftover_signs_num = 0;
            if (gclis[group_num] > gtli) {
                uint16_t tmp[10];
                memset(tmp, 0, sizeof(tmp));
                if (group_size > 10) {
                    return SvtJxsErrorDecoderInvalidBitstream;
                }
                for (int32_t bits = gclis[group_num] - 1; bits >= gtli; --bits) {
                    for (uint32_t i = 0; i < group_size; i++) {
                        if (i < leftover) {
                            if (--(*precinct_bits_left) < 0) {
                                return SvtJxsErrorDecoderInvalidBitstream;
                            }
                            buf[idx + i] |= (uint16_t)(read_1_bit(bitstream)) << bits;
                        }
                        else {
                            if (--(*precinct_bits_left) < 0) {
                                return SvtJxsErrorDecoderInvalidBitstream;
                            }
                            tmp[i] |= read_1_bit(bitstream);
                        }
                    }
                }
                for (uint32_t aa = leftover; aa < group_size; ++aa) {
                    if (tmp[aa]) {
                        (*leftover_signs_num)++;
                    }
                }
            }
        }
    }
    return SvtJxsErrorNone;
}

static void unpack_test(unpack_data unpack_data_test) {
    const uint32_t gcli_size = 50;
    const uint32_t bitstream_reader_size = 80;
    const uint32_t out_buffer_size = 1024;
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);

    uint8_t* bitstream_mem = (uint8_t*)malloc(bitstream_reader_size * sizeof(uint8_t));
    uint16_t* buf_ref = (uint16_t*)malloc(out_buffer_size * sizeof(uint16_t));
    uint16_t* buf_mod = (uint16_t*)malloc(out_buffer_size * sizeof(uint16_t));
    uint8_t* gclis = (uint8_t*)malloc(gcli_size * sizeof(int8_t));

    uint8_t leftover_ref[MAX_BAND_LINES] = {0};
    uint8_t leftover_mod[MAX_BAND_LINES] = {0};

    ASSERT_TRUE(NULL != bitstream_mem);
    ASSERT_TRUE(NULL != buf_ref);
    ASSERT_TRUE(NULL != buf_mod);
    ASSERT_TRUE(NULL != gclis);

    for (uint32_t test_num = 0; test_num < 1000; test_num++) {
        const uint8_t sign_flag = test_num % 2;
        const uint8_t gtli = test_num % 15;
        const uint32_t offset = rnd->Rand8() % 2 ? 0 : 4;

        for (uint32_t i = 0; i < bitstream_reader_size; i++) {
            bitstream_mem[i] = rnd->Rand8();
        }

        for (uint32_t i = 0; i < gcli_size; i++) {
            gclis[i] = rnd->Rand8() % 15;
        }

        for (uint32_t width = 1; width < 52; ++width) {
            //Test cut bitstream
            int32_t bitstream_to_read = 1; //Value will be updated in LOOP when cut_bits will be negative
            for (int32_t cut_bits = -1; cut_bits <= 200 && cut_bits <= bitstream_to_read; ++cut_bits) {
                bitstream_reader_t bitstream_ref;
                bitstream_reader_init(&bitstream_ref, bitstream_mem, bitstream_reader_size);
                bitstream_reader_skip_bits(&bitstream_ref, offset);
                bitstream_reader_t bitstream_mod = bitstream_ref;

                if (cut_bits <= 0) {
                    memset((void*)buf_mod, 0, out_buffer_size * sizeof(uint16_t));
                    memset((void*)buf_ref, 0, out_buffer_size * sizeof(uint16_t));
                    memset((void*)leftover_ref, 0, MAX_BAND_LINES * sizeof(uint8_t));
                    memset((void*)leftover_mod, 0, MAX_BAND_LINES * sizeof(uint8_t));
                }

                int32_t precinct_bits_left_ref = (int32_t)bitstream_reader_get_left_bits(&bitstream_ref);
                int32_t precinct_bits_left_mod = (int32_t)bitstream_reader_get_left_bits(&bitstream_mod);
                ASSERT_EQ(precinct_bits_left_ref, precinct_bits_left_mod);
                int32_t precinct_bits_left_org = precinct_bits_left_mod;
                if (cut_bits >= 0) {
                    precinct_bits_left_ref = bitstream_to_read - cut_bits;
                    precinct_bits_left_mod = bitstream_to_read - cut_bits;
                }

                int32_t precinct_bits_left_before_call = precinct_bits_left_ref;
                int32_t ret_ref = unpack_data_old(
                    &bitstream_ref, buf_ref, width, gclis, GROUP_SIZE, gtli, sign_flag, leftover_ref, &precinct_bits_left_ref);
                int32_t ret_mod = unpack_data_test(
                    &bitstream_mod, buf_mod, width, gclis, GROUP_SIZE, gtli, sign_flag, leftover_mod, &precinct_bits_left_mod);
                ASSERT_EQ(ret_ref, ret_mod);

                if (cut_bits <= 0) {
                    ASSERT_EQ(ret_ref, 0);
                    ASSERT_EQ(precinct_bits_left_ref, precinct_bits_left_mod);

                    //Check correct decrement parameter precinct_bits_left
                    int32_t precinct_bits_left_after_ref = (int32_t)bitstream_reader_get_left_bits(&bitstream_ref);
                    int32_t precinct_bits_left_after_mod = (int32_t)bitstream_reader_get_left_bits(&bitstream_mod);
                    int32_t read_bits_real_ref = precinct_bits_left_org - precinct_bits_left_after_ref;
                    int32_t read_bits_real_mod = precinct_bits_left_org - precinct_bits_left_after_mod;
                    int32_t read_bits_return_ref = precinct_bits_left_before_call - precinct_bits_left_ref;
                    int32_t read_bits_return_mod = precinct_bits_left_before_call - precinct_bits_left_mod;
                    ASSERT_EQ(read_bits_real_ref, read_bits_return_ref);
                    ASSERT_EQ(read_bits_real_mod, read_bits_return_mod);

                    ASSERT_EQ(memcmp(buf_ref, buf_mod, out_buffer_size * sizeof(uint16_t)), 0);
                    ASSERT_EQ(memcmp(leftover_ref, leftover_mod, MAX_BAND_LINES * sizeof(uint8_t)), 0);

                    if (cut_bits < 0) {
                        //Set size of useful bitstream from first execute without cut
                        bitstream_to_read = precinct_bits_left_org - precinct_bits_left_ref;
                    }
                }
                else {
                    ASSERT_TRUE(ret_ref < 0);
                }
            }
        }
    }

    free(bitstream_mem);
    free(buf_ref);
    free(buf_mod);
    free(gclis);
    delete rnd;
}

TEST(unpack_data_test, unpack_data_C) {
    unpack_test(unpack_data_c);
}

TEST(unpack_data_test, unpack_data_AVX2) {
    unpack_test(unpack_data_avx2);
}

TEST(unpack_data_test, unpack_sign) {
    const uint32_t bitstream_reader_size = 80;
    const uint32_t out_buffer_size = 1024;
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);

    uint8_t* bitstream_mem = (uint8_t*)malloc(bitstream_reader_size * sizeof(uint8_t));
    uint16_t* buf = (uint16_t*)malloc(out_buffer_size * sizeof(uint16_t));
    uint16_t* buf_expected = (uint16_t*)malloc(out_buffer_size * sizeof(uint16_t));

    ASSERT_TRUE(NULL != bitstream_mem);
    ASSERT_TRUE(NULL != buf);
    for (uint8_t leftover_signs_num = 0; leftover_signs_num < 5; ++leftover_signs_num) {
        for (uint32_t width = 1; width < 52; ++width) {
            for (uint32_t offset = 0; offset < 10; offset++) {
                int32_t bits_writed = 0;
                {
                    bitstream_writer_t bitstream_writer;
                    bitstream_writer_init(&bitstream_writer, bitstream_mem, bitstream_reader_size);
                    write_N_bits(&bitstream_writer, 0x0, offset);
                    for (uint32_t i = 0; i < width; ++i) {
                        buf_expected[i] = i;
                        if (i > 0) {
                            uint8_t flag_sign = rnd->Rand8() % 2;
                            buf_expected[i] |= (flag_sign << BITSTREAM_BIT_POSITION_SIGN);
                            write_1_bit(&bitstream_writer, flag_sign);
                            bits_writed++;
                        }
                    }
                    write_N_bits(&bitstream_writer, 0x0, leftover_signs_num);
                    bits_writed += leftover_signs_num;
                }

                //Test cut bitstream
                ASSERT_TRUE(-1 <= bits_writed);
                for (int32_t cut_bits = -1; cut_bits <= bits_writed; ++cut_bits) {
                    for (uint32_t i = 0; i < width; ++i) {
                        buf[i] = i;
                    }
                    bitstream_reader_t bitstream_reader;
                    bitstream_reader_init(&bitstream_reader, bitstream_mem, bitstream_reader_size);
                    bitstream_reader_skip_bits(&bitstream_reader, offset);

                    int32_t precinct_bits_left = (int32_t)bitstream_reader_get_left_bits(&bitstream_reader);
                    int32_t precinct_bits_left_org = precinct_bits_left;
                    if (cut_bits >= 0) {
                        precinct_bits_left = bits_writed - cut_bits;
                    }

                    int32_t precinct_bits_left_before_call = precinct_bits_left;
                    int32_t ret = unpack_sign(&bitstream_reader, buf, width, GROUP_SIZE, leftover_signs_num, &precinct_bits_left);

                    if (cut_bits <= 0 ||
                        (leftover_signs_num && (width % GROUP_SIZE == 0) && (bits_writed - cut_bits >= ((int32_t)width - 1)))) {
                        ASSERT_EQ(ret, 0);
                        //Check correct decrement parameter precinct_bits_left
                        int32_t precinct_bits_left_after = (int32_t)bitstream_reader_get_left_bits(&bitstream_reader);
                        int32_t read_bits_real = precinct_bits_left_org - precinct_bits_left_after;
                        int32_t read_bits_return = precinct_bits_left_before_call - precinct_bits_left;
                        ASSERT_EQ(read_bits_real, read_bits_return);

                        if (cut_bits < 0) {
                            if (width % GROUP_SIZE) {
                                ASSERT_EQ(precinct_bits_left_org - bits_writed, precinct_bits_left);
                            }
                            else {
                                ASSERT_EQ(leftover_signs_num, precinct_bits_left - (precinct_bits_left_org - bits_writed));
                            }
                        }
                        else {
                            if (width % GROUP_SIZE) {
                                ASSERT_EQ(0, precinct_bits_left);
                            }
                            else {
                                ASSERT_TRUE(leftover_signs_num >= precinct_bits_left);
                            }
                        }
                        ASSERT_EQ(memcmp(buf, buf_expected, width * sizeof(uint16_t)), 0);
                    }
                    else {
                        ASSERT_TRUE(ret < 0);
                    }
                }
            }
        }
    }

    free(bitstream_mem);
    free(buf);
    free(buf_expected);
    delete rnd;
}
