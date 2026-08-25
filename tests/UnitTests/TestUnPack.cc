/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "random.h"
#include "UnPack_avx2.h"
#include "UnPack_avx512.h"
#include "Packing.h"
#include "BitstreamWriter.h"
#include "common_dsp_rtcd.h"
#include "unpack_common.h"
#include "SvtUtility.h"
#include "EncDec.h" /* TRUNCATION_MAX */

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

/* GCLI out of range is what a corrupt stream looks like: the unary code yields
 * up to thirty-one, and vertical prediction can wrap the byte to any value at
 * all. Two properties are checked here.
 *
 * The parsers have to stay inside the language. A group that large does not fit
 * the single load the vector parsers use - it holds fifteen bit planes at most -
 * and the shift that would extract it is undefined, so such a group has to be
 * recognised and left to the sequential reader. This test is the one that walks
 * that path; under the sanitizer builds it is also what proves the shift is
 * gone.
 *
 * The vector levels have to agree with each other, so that a malformed stream
 * cannot make the output depend on the processor the decoder happens to run on.
 * The C implementation is deliberately not the reference here: on out-of-range
 * GCLI it is a different algorithm (it keeps the lowest bits of every plane in
 * a separate accumulator) and it has never matched the vector paths on such
 * input, upstream included. */
static void unpack_test_gcli_out_of_range(unpack_data unpack_ref, unpack_data unpack_cmp) {
    const uint32_t gcli_size = 50;
    const uint32_t bitstream_reader_size = 80;
    const uint32_t out_buffer_size = 1024;
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);

    uint8_t* bitstream_mem = (uint8_t*)malloc(bitstream_reader_size * sizeof(uint8_t));
    uint16_t* buf_ref = (uint16_t*)malloc(out_buffer_size * sizeof(uint16_t));
    uint16_t* buf_cmp = (uint16_t*)malloc(out_buffer_size * sizeof(uint16_t));
    uint8_t* gclis = (uint8_t*)malloc(gcli_size * sizeof(uint8_t));

    uint8_t leftover_ref[MAX_BAND_LINES] = {0};
    uint8_t leftover_cmp[MAX_BAND_LINES] = {0};

    ASSERT_TRUE(NULL != bitstream_mem);
    ASSERT_TRUE(NULL != buf_ref);
    ASSERT_TRUE(NULL != buf_cmp);
    ASSERT_TRUE(NULL != gclis);

    for (uint32_t test_num = 0; test_num < 200; test_num++) {
        const uint8_t sign_flag = test_num % 2;
        const uint8_t gtli = test_num % 15;
        const uint32_t offset = rnd->Rand8() % 2 ? 0 : 4;

        for (uint32_t i = 0; i < bitstream_reader_size; i++) {
            bitstream_mem[i] = rnd->Rand8();
        }
        /* The whole byte range, not only the sixteen legal values: a difference
         * above one hundred and twenty-seven is what tells a signed group mask
         * from an unsigned one. */
        for (uint32_t i = 0; i < gcli_size; i++) {
            gclis[i] = rnd->Rand8();
        }

        for (uint32_t width = 1; width < 52; ++width) {
            bitstream_reader_t bitstream_ref;
            bitstream_reader_init(&bitstream_ref, bitstream_mem, bitstream_reader_size);
            bitstream_reader_skip_bits(&bitstream_ref, offset);
            bitstream_reader_t bitstream_cmp = bitstream_ref;

            memset((void*)buf_ref, 0, out_buffer_size * sizeof(uint16_t));
            memset((void*)buf_cmp, 0, out_buffer_size * sizeof(uint16_t));
            memset((void*)leftover_ref, 0, MAX_BAND_LINES * sizeof(uint8_t));
            memset((void*)leftover_cmp, 0, MAX_BAND_LINES * sizeof(uint8_t));

            int32_t bits_left_ref = (int32_t)bitstream_reader_get_left_bits(&bitstream_ref);
            int32_t bits_left_cmp = bits_left_ref;

            SvtJxsErrorType_t ret_ref = unpack_ref(
                &bitstream_ref, buf_ref, width, gclis, GROUP_SIZE, gtli, sign_flag, leftover_ref, &bits_left_ref);
            if (unpack_cmp == NULL) {
                continue;
            }
            SvtJxsErrorType_t ret_cmp = unpack_cmp(
                &bitstream_cmp, buf_cmp, width, gclis, GROUP_SIZE, gtli, sign_flag, leftover_cmp, &bits_left_cmp);

            /* A rejected stream promises nothing about the output, so there
             * only the verdict itself is compared. */
            ASSERT_EQ(ret_ref, ret_cmp);
            if (ret_ref == SvtJxsErrorNone) {
                ASSERT_EQ(bits_left_ref, bits_left_cmp);
                ASSERT_EQ(memcmp(buf_ref, buf_cmp, out_buffer_size * sizeof(uint16_t)), 0);
                ASSERT_EQ(memcmp(leftover_ref, leftover_cmp, MAX_BAND_LINES * sizeof(uint8_t)), 0);
            }
        }
    }

    free(bitstream_mem);
    free(buf_ref);
    free(buf_cmp);
    free(gclis);
    delete rnd;
}

TEST(unpack_data_test, unpack_data_out_of_range_gcli) {
    /* Same condition the decoder dispatch uses: the AVX-512 directory is built
     * with -mbmi2, so the tier is only usable when the processor has BMI2 too. */
    const CPU_FLAGS required = CPU_FLAGS_AVX512F | CPU_FLAGS_BMI2;
    const bool has_avx512 = (get_cpu_flags() & required) == required;
    unpack_test_gcli_out_of_range(unpack_data_avx2, has_avx512 ? unpack_data_avx512 : NULL);
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

/* The AVX-512 parser on well-formed streams. Until now it was only reached by
 * the corrupt-stream test above, so the ordinary path - the one that actually
 * decodes pictures - had no test of its own on this tier. */
TEST(unpack_data_test, unpack_data_AVX512) {
    const CPU_FLAGS required = CPU_FLAGS_AVX512F | CPU_FLAGS_BMI2;
    if ((get_cpu_flags() & required) != required) {
        GTEST_SKIP();
    }
    unpack_test(unpack_data_avx512);
}

/* The logic shared by both vector parsers - counting the consumed bits, the end
 * of the line, handing the position back to the common reader - with the group
 * parser passed in. Both wrappers are nothing but a choice of that pair, so it
 * is checked here on its own. */
static SvtJxsErrorType_t unpack_data_common_avx2_parsers(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis,
                                                         uint32_t group_size, uint8_t gtli, uint8_t sign_flag,
                                                         uint8_t* leftover_signs_num, int32_t* precinct_bits_left) {
    return unpack_data_common(bitstream,
                              buf,
                              w,
                              gclis,
                              group_size,
                              gtli,
                              sign_flag,
                              leftover_signs_num,
                              precinct_bits_left,
                              unpack_n_groups,
                              unpack_n_groups_nosign);
}

TEST(unpack_data_test, unpack_data_common) {
    unpack_test(unpack_data_common_avx2_parsers);
}

/* Nibble n of the stream: bytes carry the high nibble first. */
static uint32_t one_nibble_ref(const uint8_t* mem, uint32_t nib) {
    return (nib & 1) ? (mem[nib >> 1] & 0xF) : (uint32_t)(mem[nib >> 1] >> 4);
}

/* The single load that takes a whole group has to give the same nibbles the
 * per-nibble reader gives, at both halves of the starting byte and at every
 * length the group can have - up to fifteen bit planes. */
TEST(unpack_common, load_nibbles_matches_one_nibble) {
    const uint32_t mem_size = 64;
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);
    uint8_t mem[64];

    for (uint32_t test_num = 0; test_num < 500; test_num++) {
        for (uint32_t i = 0; i < mem_size; i++) {
            mem[i] = rnd->Rand8();
        }
        /* the load reaches eight bytes past the start of the group */
        for (uint32_t nib = 0; nib + 15 < 2 * (mem_size - 8); nib++) {
            ASSERT_EQ(one_nibble_ref(mem, nib), unpack_one_nibble(mem, nib));
            for (uint32_t count = 1; count <= 15; count++) {
                uint64_t ref = 0;
                for (uint32_t i = 0; i < count; i++) {
                    ref = (ref << 4) | one_nibble_ref(mem, nib + i);
                }
                ASSERT_EQ(ref, unpack_load_nibbles(mem, nib, count)) << "nib " << nib << " count " << count;
            }
        }
    }
    delete rnd;
}

/* The right to make an over-reading load: eight bytes have to remain past the
 * start of the group, and a count is returned so that zero means "not at all". */
TEST(unpack_common, safe_byte_count) {
    for (uint32_t bytes_left = 0; bytes_left < 8; bytes_left++) {
        ASSERT_EQ(0u, unpack_safe_byte_count(bytes_left));
    }
    for (uint32_t bytes_left = 8; bytes_left < 100; bytes_left++) {
        ASSERT_EQ(bytes_left - 7, unpack_safe_byte_count(bytes_left));
    }
}

/* The two group parsers of a line, called by their own names rather than
 * through the wrappers. Random memory is a legitimate input here: the parser is
 * driven by the GCLI table, and both levels have to read the same nibbles out
 * of it and leave the reader in the same place.
 *
 * safe_bytes is walked from zero upwards, because it is what splits the fast
 * path from the sequential tail: at zero every group goes the slow way. */
TEST(unpack_n_groups, AVX512_matches_AVX2) {
    const CPU_FLAGS required = CPU_FLAGS_AVX512F | CPU_FLAGS_BMI2;
    if ((get_cpu_flags() & required) != required) {
        GTEST_SKIP();
    }

    const uint32_t max_groups = 40;
    const uint32_t mem_size = 1024;
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);

    uint8_t* mem = (uint8_t*)malloc(mem_size);
    uint8_t* gclis = (uint8_t*)malloc(max_groups);
    uint16_t* buf_ref = (uint16_t*)malloc(max_groups * GROUP_SIZE * sizeof(uint16_t));
    uint16_t* buf_cmp = (uint16_t*)malloc(max_groups * GROUP_SIZE * sizeof(uint16_t));
    ASSERT_TRUE(mem != NULL && gclis != NULL && buf_ref != NULL && buf_cmp != NULL);

    for (uint32_t nosign = 0; nosign < 2; nosign++) {
        for (uint32_t test_num = 0; test_num < 400; test_num++) {
            for (uint32_t i = 0; i < mem_size; i++) {
                mem[i] = rnd->Rand8();
            }
            const uint32_t n_groups = 1 + rnd->Rand8() % max_groups;
            const uint8_t gtli = (uint8_t)(rnd->Rand8() % (TRUNCATION_MAX + 1));
            for (uint32_t i = 0; i < n_groups; i++) {
                gclis[i] = (uint8_t)(rnd->Rand8() % (TRUNCATION_MAX + 1));
            }
            const uint8_t start_bits_used = (uint8_t)(rnd->Rand8() % 2 ? 4 : 0);
            /* zero, a boundary and a value that lets every group take the fast
             * path; the parser never leaves the buffer either way */
            const uint32_t safe_choice = rnd->Rand8() % 3;
            const uint32_t safe_bytes = safe_choice == 0 ? 0 : (safe_choice == 1 ? 1 + rnd->Rand8() % 16 : mem_size - 8);

            memset(buf_ref, 0, max_groups * GROUP_SIZE * sizeof(uint16_t));
            memset(buf_cmp, 0, max_groups * GROUP_SIZE * sizeof(uint16_t));

            reader_short_t r_ref, r_cmp;
            r_ref.mem = mem;
            r_ref.bits_used = start_bits_used;
            r_cmp = r_ref;

            if (nosign) {
                unpack_n_groups_nosign(gclis, gtli, &r_ref, buf_ref, n_groups, safe_bytes);
                unpack_n_groups_nosign_avx512(gclis, gtli, &r_cmp, buf_cmp, n_groups, safe_bytes);
            }
            else {
                unpack_n_groups(gclis, gtli, &r_ref, buf_ref, n_groups, safe_bytes);
                unpack_n_groups_avx512(gclis, gtli, &r_cmp, buf_cmp, n_groups, safe_bytes);
            }

            ASSERT_EQ(r_ref.mem - mem, r_cmp.mem - mem);
            ASSERT_EQ(r_ref.bits_used, r_cmp.bits_used);
            ASSERT_EQ(memcmp(buf_ref, buf_cmp, n_groups * GROUP_SIZE * sizeof(uint16_t)), 0);
        }
    }

    free(mem);
    free(gclis);
    free(buf_ref);
    free(buf_cmp);
    delete rnd;
}

/* The two bit scans that replaced dispatched calls on the hot paths. Both are
 * undefined on zero and neither is asked for it. */
TEST(bit_scan, svt_first_set_bit) {
    for (uint32_t bit = 0; bit < 64; bit++) {
        const uint64_t mask = (uint64_t)1 << bit;
        ASSERT_EQ(bit, svt_first_set_bit(mask));
        /* the higher bits must not shift the answer */
        ASSERT_EQ(bit, svt_first_set_bit(mask | (mask << 1)));
        ASSERT_EQ(bit, svt_first_set_bit(~(uint64_t)0 << bit));
    }

    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);
    for (uint32_t test_num = 0; test_num < 10000; test_num++) {
        uint64_t mask = 0;
        for (uint32_t i = 0; i < 4; i++) {
            mask = (mask << 16) | rnd->Rand16();
        }
        if (mask == 0) {
            continue;
        }
        uint32_t ref = 0;
        while (!((mask >> ref) & 1)) {
            ref++;
        }
        ASSERT_EQ(ref, svt_first_set_bit(mask));
    }
    delete rnd;
}

TEST(bit_scan, vlc_leading_run) {
    for (uint32_t bit = 0; bit < 32; bit++) {
        /* the length of a unary code is the number of leading zeroes plus one */
        const uint32_t v = 1u << bit;
        ASSERT_EQ((int8_t)(32 - bit), vlc_leading_run(v));
        /* the lower bits must not shift the answer */
        ASSERT_EQ((int8_t)(32 - bit), vlc_leading_run(v | (v - 1)));
    }

    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);
    for (uint32_t test_num = 0; test_num < 10000; test_num++) {
        const uint32_t v = ((uint32_t)rnd->Rand16() << 16) | rnd->Rand16();
        if (v == 0) {
            continue;
        }
        uint32_t top = 31;
        while (!((v >> top) & 1)) {
            top--;
        }
        ASSERT_EQ((int8_t)(32 - top), vlc_leading_run(v));
    }
    delete rnd;
}
