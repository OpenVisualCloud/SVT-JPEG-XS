/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include "random.h"
#include "SvtUtility.h"
#include "BitstreamReader.h"
#include "BitstreamWriter.h"

/*
 * Read number of set bits until find bit zero.
 * Return negative value when not found bit zero before reaching the max value.
 * Inverse mem_set_to_first_negative_bit(): (1 << (val + 1)) - 2);
 */
int8_t read_to_first_negative_safe(bitstream_reader_t* bitstream, uint32_t size) {
    assert(bitstream->bits_used < 8);
    uint8_t const* mem = bitstream->mem;
    //TODO: Use MSB to optimize
    for (uint32_t i = 0; i < size; ++i) {
        int bit;
        bit = mem[bitstream->offset] & (1 << (7 - bitstream->bits_used));
        (bitstream->bits_used)++;
        if (bitstream->bits_used > 7) {
            ++(bitstream->offset);
            bitstream->bits_used = 0;
        }
        if (!bit) {
            return i;
        }
    }
    return -1;
}

int8_t read_to_first_negative_bit_old(bitstream_reader_t* bitstream, uint32_t size) {
    assert(bitstream->bits_used < 8);
    if (size < 32 || size > ((bitstream->size - bitstream->offset - !!bitstream->bits_used) << 3)) {
        return read_to_first_negative_safe(bitstream, size);
    }

    uint8_t const* mem = bitstream->mem + bitstream->offset;

    uint32_t val = ((uint32_t)mem[0]) << 24;
    val |= mem[1] << 16;
    val |= mem[2] << 8;
    val |= mem[3];

    if (bitstream->bits_used) {
        val <<= bitstream->bits_used;
        val |= mem[4] >> (8 - bitstream->bits_used);
    }

    val = ~val;
    // If val==0 then there is no "0" in the next 32 bits, it also mean that bitstream is invalid
    if (val == 0) {
        // Adding 32bits or 4 bytes that we read from bitstream
        bitstream->offset += 4;
        return -1;
    }

    uint8_t res = 32 - svt_log2_32(val);

    bitstream->offset += (bitstream->bits_used + res) / 8;
    bitstream->bits_used = (bitstream->bits_used + res) % 8;

    res -= 1;
    return res;
}

TEST(BitstreamReadWrite, read_to_first_negative_bit) {
    setup_common_rtcd_internal(get_cpu_flags());
    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(8, false);

    const uint32_t buffer_size = 200;
    uint8_t buff[buffer_size + 10] = {0};

    bitstream_reader_t reader_ref, reader_mod;
    bitstream_reader_t bitstream;

    bitstream.size = buffer_size + 10;
    bitstream.mem = buff;

    for (bitstream.offset = 0; bitstream.offset < buffer_size; bitstream.offset++) {
        uint8_t val = 0;
        for (bitstream.bits_used = 0; bitstream.bits_used < 7; bitstream.bits_used++) {
            // 210 is a magic number to generate bitstream with much more bits "1" than "0"
            uint8_t bit = rnd->Rand8() > 210 ? 1 : 0;
            val |= bit << (bitstream.bits_used);
        }
        buff[bitstream.offset] = ~val;
    }

    for (bitstream.offset = 0; bitstream.offset < buffer_size; bitstream.offset++) {
        for (bitstream.bits_used = 0; bitstream.bits_used < 7; bitstream.bits_used++) {
            for (uint32_t max_bits = 0; max_bits <= 32 && max_bits <= (buffer_size - bitstream.offset); ++max_bits) {
                reader_ref = reader_mod = bitstream;

                int8_t res_ref = read_to_first_negative_safe(&reader_ref, max_bits);
                int8_t res_mod = read_to_first_negative_bit_old(&reader_mod, max_bits);
                if (res_ref != res_mod) {
                    ASSERT_EQ(res_ref, res_mod);
                }
            }
        }
    }
    delete rnd;
}

TEST(BitstreamReadWrite, read_4_bits) {
    uint8_t tests[] = {0b00000000,  //1
                       0b10101010,  //2
                       0b01010101,  //3
                       0b11001100,  //4
                       0b00110011,  //5
                       0b11101110,  //6
                       0b01110111,  //7
                       0b00111011,  //8
                       0b00011101,  //9
                       0b11111111}; //10
    /*00000000101010100101010111001100001100111110111001110111001110110001110111111111*/

    bitstream_reader_t bitstream;

    bitstream.offset = 0;
    bitstream.bits_used = 0;
    bitstream.size = sizeof(tests);
    bitstream.mem = tests;

    //Cover bit used 0 and 4
    ASSERT_EQ(0, read_4_bits(&bitstream));  //0000
    ASSERT_EQ(0, read_4_bits(&bitstream));  //0000
    ASSERT_EQ(10, read_4_bits(&bitstream)); //1010
    ASSERT_EQ(10, read_4_bits(&bitstream)); //1010
    ASSERT_EQ(5, read_4_bits(&bitstream));  //0101
    ASSERT_EQ(5, read_4_bits(&bitstream));  //0101
    ASSERT_EQ(12, read_4_bits(&bitstream)); //1100
    ASSERT_EQ(12, read_4_bits(&bitstream)); //1100
    ASSERT_EQ(3, read_4_bits(&bitstream));  //0011
    ASSERT_EQ(3, read_4_bits(&bitstream));  //0011
    ASSERT_EQ(14, read_4_bits(&bitstream)); //1110
    ASSERT_EQ(14, read_4_bits(&bitstream)); //1110
    ASSERT_EQ(7, read_4_bits(&bitstream));  //0111
    ASSERT_EQ(7, read_4_bits(&bitstream));  //0111

    bitstream.offset = 0;
    bitstream.bits_used = 1;
    //Cover bit used 1, 5
    ASSERT_EQ(0, read_4_bits(&bitstream));  //0000
    ASSERT_EQ(1, read_4_bits(&bitstream));  //0001
    ASSERT_EQ(5, read_4_bits(&bitstream));  //0101
    ASSERT_EQ(4, read_4_bits(&bitstream));  //0100
    ASSERT_EQ(10, read_4_bits(&bitstream)); //1010
    ASSERT_EQ(11, read_4_bits(&bitstream)); //1011
    ASSERT_EQ(9, read_4_bits(&bitstream));  //1001
    ASSERT_EQ(8, read_4_bits(&bitstream));  //1000
    ASSERT_EQ(6, read_4_bits(&bitstream));  //0110
    ASSERT_EQ(7, read_4_bits(&bitstream));  //0111
    ASSERT_EQ(13, read_4_bits(&bitstream)); //1101
    ASSERT_EQ(12, read_4_bits(&bitstream)); //1100
    ASSERT_EQ(14, read_4_bits(&bitstream)); //1110
    ASSERT_EQ(14, read_4_bits(&bitstream)); //1110 ...01110110001110111111111

    bitstream.offset = 0;
    bitstream.bits_used = 2;
    //Cover bit used 2, 6
    ASSERT_EQ(0, read_4_bits(&bitstream));  //0000
    ASSERT_EQ(2, read_4_bits(&bitstream));  //0010
    ASSERT_EQ(10, read_4_bits(&bitstream)); //1010
    ASSERT_EQ(9, read_4_bits(&bitstream));  //1001
    ASSERT_EQ(5, read_4_bits(&bitstream));  //0101
    ASSERT_EQ(7, read_4_bits(&bitstream));  //0111
    ASSERT_EQ(3, read_4_bits(&bitstream));  //0011
    ASSERT_EQ(0, read_4_bits(&bitstream));  //0000
    ASSERT_EQ(12, read_4_bits(&bitstream)); //1100
    ASSERT_EQ(15, read_4_bits(&bitstream)); //1111
    ASSERT_EQ(11, read_4_bits(&bitstream)); //1011
    ASSERT_EQ(9, read_4_bits(&bitstream));  //1001
    ASSERT_EQ(13, read_4_bits(&bitstream)); //1101
    ASSERT_EQ(12, read_4_bits(&bitstream)); //1100 ...1110110001110111111111

    bitstream.offset = 0;
    bitstream.bits_used = 3;
    //Cover bit used 3, 7
    ASSERT_EQ(0, read_4_bits(&bitstream));  //0000
    ASSERT_EQ(5, read_4_bits(&bitstream));  //0101
    ASSERT_EQ(5, read_4_bits(&bitstream));  //0101
    ASSERT_EQ(2, read_4_bits(&bitstream));  //0010
    ASSERT_EQ(10, read_4_bits(&bitstream)); //1010
    ASSERT_EQ(14, read_4_bits(&bitstream)); //1110
    ASSERT_EQ(6, read_4_bits(&bitstream));  //0110
    ASSERT_EQ(1, read_4_bits(&bitstream));  //0001
    ASSERT_EQ(9, read_4_bits(&bitstream));  //1001
    ASSERT_EQ(15, read_4_bits(&bitstream)); //1111
    ASSERT_EQ(7, read_4_bits(&bitstream));  //0111
    ASSERT_EQ(3, read_4_bits(&bitstream));  //0011
    ASSERT_EQ(11, read_4_bits(&bitstream)); //1011
    ASSERT_EQ(9, read_4_bits(&bitstream));  //1001 ...110110001110111111111
}

static void read_nbits(bitstream_reader_t* bitstream, uint32_t* val, uint8_t nbits) {
    *val = 0;
    for (uint8_t i = 0; i < nbits; ++i) {
        *val = (*val << 1) | read_1_bit(bitstream);
    }
}

TEST(BitstreamReadWrite, update_N_bits) {
    const uint32_t bitstream_reader_size = 30 * 30;

    uint8_t* bitstream_mem = (uint8_t*)malloc(bitstream_reader_size * sizeof(uint8_t));
    ASSERT_TRUE(NULL != bitstream_mem);

    for (uint32_t offset = 0; offset < 10; offset++) {
        bitstream_writer_t bitstream_writer;
        bitstream_writer_init(&bitstream_writer, bitstream_mem, bitstream_reader_size);
        write_N_bits(&bitstream_writer, 0x0, offset);

        for (uint32_t test = 1; test < 30; ++test) {
            write_N_bits(&bitstream_writer, test, test);
        }

        bitstream_reader_t bitstream_reader;
        bitstream_reader_init(&bitstream_reader, bitstream_mem, bitstream_reader_size);
        bitstream_reader_skip_bits(&bitstream_reader, offset);
        for (uint32_t test = 1; test < 30; ++test) {
            uint32_t val;
            read_nbits(&bitstream_reader, &val, test);
            ASSERT_EQ(val, test);
        }

        for (uint32_t mask_offset = 0; mask_offset < 2; ++mask_offset) {
            //Update XOR mask
            uint32_t mask_xor = 0xAAAAAAAA; //...101010
            mask_xor = mask_xor >> mask_offset;
            uint32_t offset_bits = offset;
            for (uint32_t test = 1; test < 30; ++test) {
                uint32_t val = test ^ mask_xor;
                val = val & ((1 << test) - 1);
                update_N_bits(&bitstream_writer, offset_bits, val, test);
                offset_bits += test;
            }

            bitstream_reader_init(&bitstream_reader, bitstream_mem, bitstream_reader_size);
            bitstream_reader_skip_bits(&bitstream_reader, offset);
            for (uint32_t test = 1; test < 30; ++test) {
                uint32_t val;
                read_nbits(&bitstream_reader, &val, test);
                val = val ^ mask_xor; //Revert XOR
                val = val & ((1 << test) - 1);
                ASSERT_EQ(val, test);
            }
        }
    }
    free(bitstream_mem);
}
