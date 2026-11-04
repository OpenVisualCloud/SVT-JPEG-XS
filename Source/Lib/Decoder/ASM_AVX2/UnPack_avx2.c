/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "UnPack_avx2.h"
#include "unpack_common.h"
#include "SvtUtility.h"
#include "EncDec.h" /* TRUNCATION_MAX: the single load holds fifteen planes at most */

/* Shared walk for unpack_n_groups/unpack_n_groups_nosign: the two differ only in
 * whether a sign nibble is read alongside the planes. has_sign is a parameter
 * rather than two separate bodies, but both call sites below pass a compile-time
 * constant (0 or 1), so the compiler specializes this into the same two variants
 * that used to be written out by hand, with no runtime branch inside either loop -
 * the same reasoning pack_groups_masked's emit_signs parameter already relies on, on
 * the encoder side (see pack_group_helper.h). */
static INLINE void unpack_n_groups_impl(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                                        uint32_t safe_bytes, const int has_sign) {
    uint8_t* const base = r->mem;
    uint32_t nib = r->bits_used ? 1u : 0u;
    uint32_t group = 0;

    /* The vast majority of groups are empty: about eighty-seven per cent on
     * 1080p at 4 bits per pixel. Each of them used to cost a GCLI load, a
     * compare, a poorly predicted branch and a store of eight zeroes. Now the
     * output is zeroed in one pass and the walk follows the bits of a non-empty
     * mask, so an empty group costs nothing and the per-group branch is gone.
     *
     * The mask comes from a saturating subtract: subs_epu8 yields zero exactly
     * where GCLI does not exceed the truncation level. Testing that result for
     * equality with zero and inverting the mask is therefore an exact unsigned
     * comparison, which a signed compare against zero would not be: a corrupt
     * stream can put any byte value into GCLI, and AVX-512 tests it unsigned. */
    memset(buf, 0, (size_t)n_groups * GROUP_SIZE * sizeof(uint16_t));

    /* Fast path: a group's position in the stream comes from adding up lengths
     * rather than from finishing the previous group, so the group loads are
     * independent of each other. */
    const __m256i gtli_vec = _mm256_set1_epi8((char)gtli);
    const __m256i zero = _mm256_setzero_si256();
    while (group < n_groups) {
        const uint32_t chunk = MIN(n_groups - group, 32u);
        __m256i gcli_vec;
        if (chunk >= 32) {
            gcli_vec = _mm256_loadu_si256((const __m256i*)(gclis + group));
        }
        else {
            uint8_t padded[32] = {0};
            memcpy(padded, gclis + group, chunk);
            gcli_vec = _mm256_loadu_si256((const __m256i*)padded);
        }
        uint64_t todo = (uint32_t)~_mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_subs_epu8(gcli_vec, gtli_vec), zero));
        while (todo) {
            const uint32_t k = svt_first_set_bit(todo);
            const uint32_t size = (uint32_t)gclis[group + k] - gtli;
            /* A corrupt stream can carry a GCLI above the truncation maximum:
             * the unary code yields up to thirty-one. Such a group holds more
             * bit planes than the single load can take - fifteen - and the
             * shift that would extract it would be undefined, so the group is
             * left to the sequential reader below. */
            if (size > TRUNCATION_MAX || ((nib + (uint32_t)has_sign) >> 1) >= safe_bytes) {
                group += k;
                goto tail;
            }
            uint64_t out = unpack_planes_to_lanes_sse(unpack_load_nibbles(base, nib + (uint32_t)has_sign, size), gtli);
            if (has_sign) {
                out |= unpack_sign_spread[unpack_one_nibble(base, nib)];
            }
            memcpy(buf + (size_t)(group + k) * GROUP_SIZE, &out, sizeof(out));
            nib += size + (uint32_t)has_sign;
            todo &= todo - 1;
        }
        group += chunk;
    }

tail:
    r->mem = base + (nib >> 1);
    r->bits_used = (uint8_t)((nib & 1) * 4);
    buf += (size_t)group * GROUP_SIZE;

    /* End of the line is read sequentially so the load never runs past the data.
     * Zeroes need not be written: the buffer is already cleared. */
    for (; group < n_groups; group++) {
        const int32_t size = (int32_t)gclis[group] - (int32_t)gtli;
        if (size > 0) {
            uint64_t signs = 0;
            if (has_sign) {
                signs = unpack_sign_spread[read_4_bits_align4_fast(r)];
            }
            /* A corrupt stream can carry a GCLI far above the truncation maximum: the
             * unary code yields up to thirty-one, and vertical prediction can wrap it
             * to anything up to two hundred and fifty-five. The fast path above
             * already declines to touch such a group at all; this sequential reader
             * has to decline the same way, or it reads size nibbles - unbounded by
             * anything - straight past the end of whatever buffer backs it. */
            const int32_t read_planes = (size > TRUNCATION_MAX) ? TRUNCATION_MAX : size;
            uint64_t acc = 0;
            for (int32_t i = 0; i < read_planes; i++) {
                acc = (acc << 4) | read_4_bits_align4_fast(r);
            }
            const uint64_t out = unpack_planes_to_lanes_sse(acc, gtli) | signs;
            memcpy(buf, &out, sizeof(out));
        }
        buf += GROUP_SIZE;
    }
}

void unpack_n_groups(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups, uint32_t safe_bytes) {
    unpack_n_groups_impl(gclis, gtli, r, buf, n_groups, safe_bytes, 1);
}

void unpack_n_groups_nosign(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                            uint32_t safe_bytes) {
    unpack_n_groups_impl(gclis, gtli, r, buf, n_groups, safe_bytes, 0);
}

/* Same as unpack_n_groups_impl, but the plane spread is PEXT (unpack_planes_to_lanes_pext)
 * instead of the nibble-split-and-movemask sequence - see that function's comment.
 * Needs BMI2; callers must gate on CPU_FLAGS_BMI2. */
TARGET_BMI2 static INLINE void unpack_n_groups_bmi2_impl(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf,
                                                         uint32_t n_groups, uint32_t safe_bytes, const int has_sign) {
    uint8_t* const base = r->mem;
    uint32_t nib = r->bits_used ? 1u : 0u;
    uint32_t group = 0;

    memset(buf, 0, (size_t)n_groups * GROUP_SIZE * sizeof(uint16_t));

    const __m256i gtli_vec = _mm256_set1_epi8((char)gtli);
    const __m256i zero = _mm256_setzero_si256();
    while (group < n_groups) {
        const uint32_t chunk = MIN(n_groups - group, 32u);
        __m256i gcli_vec;
        if (chunk >= 32) {
            gcli_vec = _mm256_loadu_si256((const __m256i*)(gclis + group));
        }
        else {
            uint8_t padded[32] = {0};
            memcpy(padded, gclis + group, chunk);
            gcli_vec = _mm256_loadu_si256((const __m256i*)padded);
        }
        uint64_t todo = (uint32_t)~_mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_subs_epu8(gcli_vec, gtli_vec), zero));
        while (todo) {
            const uint32_t k = svt_first_set_bit(todo);
            const uint32_t size = (uint32_t)gclis[group + k] - gtli;
            if (size > TRUNCATION_MAX || ((nib + (uint32_t)has_sign) >> 1) >= safe_bytes) {
                group += k;
                goto tail;
            }
            uint64_t out = unpack_planes_to_lanes_pext(unpack_load_nibbles(base, nib + (uint32_t)has_sign, size), gtli);
            if (has_sign) {
                out |= unpack_sign_spread[unpack_one_nibble(base, nib)];
            }
            memcpy(buf + (size_t)(group + k) * GROUP_SIZE, &out, sizeof(out));
            nib += size + (uint32_t)has_sign;
            todo &= todo - 1;
        }
        group += chunk;
    }

tail:
    r->mem = base + (nib >> 1);
    r->bits_used = (uint8_t)((nib & 1) * 4);
    buf += (size_t)group * GROUP_SIZE;

    for (; group < n_groups; group++) {
        const int32_t size = (int32_t)gclis[group] - (int32_t)gtli;
        if (size > 0) {
            uint64_t signs = 0;
            if (has_sign) {
                signs = unpack_sign_spread[read_4_bits_align4_fast(r)];
            }
            const int32_t read_planes = (size > TRUNCATION_MAX) ? TRUNCATION_MAX : size;
            uint64_t acc = 0;
            for (int32_t i = 0; i < read_planes; i++) {
                acc = (acc << 4) | read_4_bits_align4_fast(r);
            }
            const uint64_t out = unpack_planes_to_lanes_pext(acc, gtli) | signs;
            memcpy(buf, &out, sizeof(out));
        }
        buf += GROUP_SIZE;
    }
}

/* No TARGET_BMI2 here: neither wrapper touches a BMI2 intrinsic directly, only
 * unpack_n_groups_bmi2_impl does. Matches pack_data_groups_avx2_bmi2 on the encoder
 * side, which likewise leaves the outer wrapper un-attributed. Tagging these too
 * made the header declaration and this definition disagree on the attribute -
 * flagged in review as a GCC/Clang attribute mismatch (fatal under -Werror). */
void unpack_n_groups_bmi2(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                          uint32_t safe_bytes) {
    unpack_n_groups_bmi2_impl(gclis, gtli, r, buf, n_groups, safe_bytes, 1);
}

void unpack_n_groups_nosign_bmi2(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                                 uint32_t safe_bytes) {
    unpack_n_groups_bmi2_impl(gclis, gtli, r, buf, n_groups, safe_bytes, 0);
}

SvtJxsErrorType_t unpack_data_avx2_bmi2(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis,
                                        uint32_t group_size, uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num,
                                        int32_t* precinct_bits_left) {
    return unpack_data_common(bitstream,
                              buf,
                              w,
                              gclis,
                              group_size,
                              gtli,
                              sign_flag,
                              leftover_signs_num,
                              precinct_bits_left,
                              unpack_n_groups_bmi2,
                              unpack_n_groups_nosign_bmi2);
}

/* A run of GCLI unary codes, as many as there are or as fit before byte_limit.
 *
 * The scalar reader finds the end of a code with a leading-zero count and shifts the register by it
 * before it can look for the next one, so every code waits for the previous one.
 *
 * Here the stream is first turned into the list of the positions where the codes end: in an inverted
 * byte every set bit ends one code, and a table gives the positions of the set bits of any byte, in
 * stream order. They are stored, offset by the position of the byte, straight after those of the bytes
 * before, so the only chain left from one byte to the next is the add of their number. The length of
 * every code is then the difference of two neighbouring positions, sixteen codes per subtract.
 *
 * The positions are 16-bit, counted from the first byte of a chunk of at most CHUNK codes and
 * UNARY_RUN_CHUNK_BYTES bytes, so they cannot wrap. A code longer than thirty-two bits is a corrupt
 * stream, the same condition under which the scalar reader fails. Only bytes below byte_limit are read,
 * one at a time. Returns the number of codes decoded, bit_pos is advanced past them. */
#define UNARY_RUN_CHUNK       256
#define UNARY_RUN_CHUNK_BYTES 2048

// clang-format off
static const uint8_t unary_run_ends[256][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {7, 0, 0, 0, 0, 0, 0, 0},
    {6, 0, 0, 0, 0, 0, 0, 0},
    {6, 7, 0, 0, 0, 0, 0, 0},
    {5, 0, 0, 0, 0, 0, 0, 0},
    {5, 7, 0, 0, 0, 0, 0, 0},
    {5, 6, 0, 0, 0, 0, 0, 0},
    {5, 6, 7, 0, 0, 0, 0, 0},
    {4, 0, 0, 0, 0, 0, 0, 0},
    {4, 7, 0, 0, 0, 0, 0, 0},
    {4, 6, 0, 0, 0, 0, 0, 0},
    {4, 6, 7, 0, 0, 0, 0, 0},
    {4, 5, 0, 0, 0, 0, 0, 0},
    {4, 5, 7, 0, 0, 0, 0, 0},
    {4, 5, 6, 0, 0, 0, 0, 0},
    {4, 5, 6, 7, 0, 0, 0, 0},
    {3, 0, 0, 0, 0, 0, 0, 0},
    {3, 7, 0, 0, 0, 0, 0, 0},
    {3, 6, 0, 0, 0, 0, 0, 0},
    {3, 6, 7, 0, 0, 0, 0, 0},
    {3, 5, 0, 0, 0, 0, 0, 0},
    {3, 5, 7, 0, 0, 0, 0, 0},
    {3, 5, 6, 0, 0, 0, 0, 0},
    {3, 5, 6, 7, 0, 0, 0, 0},
    {3, 4, 0, 0, 0, 0, 0, 0},
    {3, 4, 7, 0, 0, 0, 0, 0},
    {3, 4, 6, 0, 0, 0, 0, 0},
    {3, 4, 6, 7, 0, 0, 0, 0},
    {3, 4, 5, 0, 0, 0, 0, 0},
    {3, 4, 5, 7, 0, 0, 0, 0},
    {3, 4, 5, 6, 0, 0, 0, 0},
    {3, 4, 5, 6, 7, 0, 0, 0},
    {2, 0, 0, 0, 0, 0, 0, 0},
    {2, 7, 0, 0, 0, 0, 0, 0},
    {2, 6, 0, 0, 0, 0, 0, 0},
    {2, 6, 7, 0, 0, 0, 0, 0},
    {2, 5, 0, 0, 0, 0, 0, 0},
    {2, 5, 7, 0, 0, 0, 0, 0},
    {2, 5, 6, 0, 0, 0, 0, 0},
    {2, 5, 6, 7, 0, 0, 0, 0},
    {2, 4, 0, 0, 0, 0, 0, 0},
    {2, 4, 7, 0, 0, 0, 0, 0},
    {2, 4, 6, 0, 0, 0, 0, 0},
    {2, 4, 6, 7, 0, 0, 0, 0},
    {2, 4, 5, 0, 0, 0, 0, 0},
    {2, 4, 5, 7, 0, 0, 0, 0},
    {2, 4, 5, 6, 0, 0, 0, 0},
    {2, 4, 5, 6, 7, 0, 0, 0},
    {2, 3, 0, 0, 0, 0, 0, 0},
    {2, 3, 7, 0, 0, 0, 0, 0},
    {2, 3, 6, 0, 0, 0, 0, 0},
    {2, 3, 6, 7, 0, 0, 0, 0},
    {2, 3, 5, 0, 0, 0, 0, 0},
    {2, 3, 5, 7, 0, 0, 0, 0},
    {2, 3, 5, 6, 0, 0, 0, 0},
    {2, 3, 5, 6, 7, 0, 0, 0},
    {2, 3, 4, 0, 0, 0, 0, 0},
    {2, 3, 4, 7, 0, 0, 0, 0},
    {2, 3, 4, 6, 0, 0, 0, 0},
    {2, 3, 4, 6, 7, 0, 0, 0},
    {2, 3, 4, 5, 0, 0, 0, 0},
    {2, 3, 4, 5, 7, 0, 0, 0},
    {2, 3, 4, 5, 6, 0, 0, 0},
    {2, 3, 4, 5, 6, 7, 0, 0},
    {1, 0, 0, 0, 0, 0, 0, 0},
    {1, 7, 0, 0, 0, 0, 0, 0},
    {1, 6, 0, 0, 0, 0, 0, 0},
    {1, 6, 7, 0, 0, 0, 0, 0},
    {1, 5, 0, 0, 0, 0, 0, 0},
    {1, 5, 7, 0, 0, 0, 0, 0},
    {1, 5, 6, 0, 0, 0, 0, 0},
    {1, 5, 6, 7, 0, 0, 0, 0},
    {1, 4, 0, 0, 0, 0, 0, 0},
    {1, 4, 7, 0, 0, 0, 0, 0},
    {1, 4, 6, 0, 0, 0, 0, 0},
    {1, 4, 6, 7, 0, 0, 0, 0},
    {1, 4, 5, 0, 0, 0, 0, 0},
    {1, 4, 5, 7, 0, 0, 0, 0},
    {1, 4, 5, 6, 0, 0, 0, 0},
    {1, 4, 5, 6, 7, 0, 0, 0},
    {1, 3, 0, 0, 0, 0, 0, 0},
    {1, 3, 7, 0, 0, 0, 0, 0},
    {1, 3, 6, 0, 0, 0, 0, 0},
    {1, 3, 6, 7, 0, 0, 0, 0},
    {1, 3, 5, 0, 0, 0, 0, 0},
    {1, 3, 5, 7, 0, 0, 0, 0},
    {1, 3, 5, 6, 0, 0, 0, 0},
    {1, 3, 5, 6, 7, 0, 0, 0},
    {1, 3, 4, 0, 0, 0, 0, 0},
    {1, 3, 4, 7, 0, 0, 0, 0},
    {1, 3, 4, 6, 0, 0, 0, 0},
    {1, 3, 4, 6, 7, 0, 0, 0},
    {1, 3, 4, 5, 0, 0, 0, 0},
    {1, 3, 4, 5, 7, 0, 0, 0},
    {1, 3, 4, 5, 6, 0, 0, 0},
    {1, 3, 4, 5, 6, 7, 0, 0},
    {1, 2, 0, 0, 0, 0, 0, 0},
    {1, 2, 7, 0, 0, 0, 0, 0},
    {1, 2, 6, 0, 0, 0, 0, 0},
    {1, 2, 6, 7, 0, 0, 0, 0},
    {1, 2, 5, 0, 0, 0, 0, 0},
    {1, 2, 5, 7, 0, 0, 0, 0},
    {1, 2, 5, 6, 0, 0, 0, 0},
    {1, 2, 5, 6, 7, 0, 0, 0},
    {1, 2, 4, 0, 0, 0, 0, 0},
    {1, 2, 4, 7, 0, 0, 0, 0},
    {1, 2, 4, 6, 0, 0, 0, 0},
    {1, 2, 4, 6, 7, 0, 0, 0},
    {1, 2, 4, 5, 0, 0, 0, 0},
    {1, 2, 4, 5, 7, 0, 0, 0},
    {1, 2, 4, 5, 6, 0, 0, 0},
    {1, 2, 4, 5, 6, 7, 0, 0},
    {1, 2, 3, 0, 0, 0, 0, 0},
    {1, 2, 3, 7, 0, 0, 0, 0},
    {1, 2, 3, 6, 0, 0, 0, 0},
    {1, 2, 3, 6, 7, 0, 0, 0},
    {1, 2, 3, 5, 0, 0, 0, 0},
    {1, 2, 3, 5, 7, 0, 0, 0},
    {1, 2, 3, 5, 6, 0, 0, 0},
    {1, 2, 3, 5, 6, 7, 0, 0},
    {1, 2, 3, 4, 0, 0, 0, 0},
    {1, 2, 3, 4, 7, 0, 0, 0},
    {1, 2, 3, 4, 6, 0, 0, 0},
    {1, 2, 3, 4, 6, 7, 0, 0},
    {1, 2, 3, 4, 5, 0, 0, 0},
    {1, 2, 3, 4, 5, 7, 0, 0},
    {1, 2, 3, 4, 5, 6, 0, 0},
    {1, 2, 3, 4, 5, 6, 7, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 7, 0, 0, 0, 0, 0, 0},
    {0, 6, 0, 0, 0, 0, 0, 0},
    {0, 6, 7, 0, 0, 0, 0, 0},
    {0, 5, 0, 0, 0, 0, 0, 0},
    {0, 5, 7, 0, 0, 0, 0, 0},
    {0, 5, 6, 0, 0, 0, 0, 0},
    {0, 5, 6, 7, 0, 0, 0, 0},
    {0, 4, 0, 0, 0, 0, 0, 0},
    {0, 4, 7, 0, 0, 0, 0, 0},
    {0, 4, 6, 0, 0, 0, 0, 0},
    {0, 4, 6, 7, 0, 0, 0, 0},
    {0, 4, 5, 0, 0, 0, 0, 0},
    {0, 4, 5, 7, 0, 0, 0, 0},
    {0, 4, 5, 6, 0, 0, 0, 0},
    {0, 4, 5, 6, 7, 0, 0, 0},
    {0, 3, 0, 0, 0, 0, 0, 0},
    {0, 3, 7, 0, 0, 0, 0, 0},
    {0, 3, 6, 0, 0, 0, 0, 0},
    {0, 3, 6, 7, 0, 0, 0, 0},
    {0, 3, 5, 0, 0, 0, 0, 0},
    {0, 3, 5, 7, 0, 0, 0, 0},
    {0, 3, 5, 6, 0, 0, 0, 0},
    {0, 3, 5, 6, 7, 0, 0, 0},
    {0, 3, 4, 0, 0, 0, 0, 0},
    {0, 3, 4, 7, 0, 0, 0, 0},
    {0, 3, 4, 6, 0, 0, 0, 0},
    {0, 3, 4, 6, 7, 0, 0, 0},
    {0, 3, 4, 5, 0, 0, 0, 0},
    {0, 3, 4, 5, 7, 0, 0, 0},
    {0, 3, 4, 5, 6, 0, 0, 0},
    {0, 3, 4, 5, 6, 7, 0, 0},
    {0, 2, 0, 0, 0, 0, 0, 0},
    {0, 2, 7, 0, 0, 0, 0, 0},
    {0, 2, 6, 0, 0, 0, 0, 0},
    {0, 2, 6, 7, 0, 0, 0, 0},
    {0, 2, 5, 0, 0, 0, 0, 0},
    {0, 2, 5, 7, 0, 0, 0, 0},
    {0, 2, 5, 6, 0, 0, 0, 0},
    {0, 2, 5, 6, 7, 0, 0, 0},
    {0, 2, 4, 0, 0, 0, 0, 0},
    {0, 2, 4, 7, 0, 0, 0, 0},
    {0, 2, 4, 6, 0, 0, 0, 0},
    {0, 2, 4, 6, 7, 0, 0, 0},
    {0, 2, 4, 5, 0, 0, 0, 0},
    {0, 2, 4, 5, 7, 0, 0, 0},
    {0, 2, 4, 5, 6, 0, 0, 0},
    {0, 2, 4, 5, 6, 7, 0, 0},
    {0, 2, 3, 0, 0, 0, 0, 0},
    {0, 2, 3, 7, 0, 0, 0, 0},
    {0, 2, 3, 6, 0, 0, 0, 0},
    {0, 2, 3, 6, 7, 0, 0, 0},
    {0, 2, 3, 5, 0, 0, 0, 0},
    {0, 2, 3, 5, 7, 0, 0, 0},
    {0, 2, 3, 5, 6, 0, 0, 0},
    {0, 2, 3, 5, 6, 7, 0, 0},
    {0, 2, 3, 4, 0, 0, 0, 0},
    {0, 2, 3, 4, 7, 0, 0, 0},
    {0, 2, 3, 4, 6, 0, 0, 0},
    {0, 2, 3, 4, 6, 7, 0, 0},
    {0, 2, 3, 4, 5, 0, 0, 0},
    {0, 2, 3, 4, 5, 7, 0, 0},
    {0, 2, 3, 4, 5, 6, 0, 0},
    {0, 2, 3, 4, 5, 6, 7, 0},
    {0, 1, 0, 0, 0, 0, 0, 0},
    {0, 1, 7, 0, 0, 0, 0, 0},
    {0, 1, 6, 0, 0, 0, 0, 0},
    {0, 1, 6, 7, 0, 0, 0, 0},
    {0, 1, 5, 0, 0, 0, 0, 0},
    {0, 1, 5, 7, 0, 0, 0, 0},
    {0, 1, 5, 6, 0, 0, 0, 0},
    {0, 1, 5, 6, 7, 0, 0, 0},
    {0, 1, 4, 0, 0, 0, 0, 0},
    {0, 1, 4, 7, 0, 0, 0, 0},
    {0, 1, 4, 6, 0, 0, 0, 0},
    {0, 1, 4, 6, 7, 0, 0, 0},
    {0, 1, 4, 5, 0, 0, 0, 0},
    {0, 1, 4, 5, 7, 0, 0, 0},
    {0, 1, 4, 5, 6, 0, 0, 0},
    {0, 1, 4, 5, 6, 7, 0, 0},
    {0, 1, 3, 0, 0, 0, 0, 0},
    {0, 1, 3, 7, 0, 0, 0, 0},
    {0, 1, 3, 6, 0, 0, 0, 0},
    {0, 1, 3, 6, 7, 0, 0, 0},
    {0, 1, 3, 5, 0, 0, 0, 0},
    {0, 1, 3, 5, 7, 0, 0, 0},
    {0, 1, 3, 5, 6, 0, 0, 0},
    {0, 1, 3, 5, 6, 7, 0, 0},
    {0, 1, 3, 4, 0, 0, 0, 0},
    {0, 1, 3, 4, 7, 0, 0, 0},
    {0, 1, 3, 4, 6, 0, 0, 0},
    {0, 1, 3, 4, 6, 7, 0, 0},
    {0, 1, 3, 4, 5, 0, 0, 0},
    {0, 1, 3, 4, 5, 7, 0, 0},
    {0, 1, 3, 4, 5, 6, 0, 0},
    {0, 1, 3, 4, 5, 6, 7, 0},
    {0, 1, 2, 0, 0, 0, 0, 0},
    {0, 1, 2, 7, 0, 0, 0, 0},
    {0, 1, 2, 6, 0, 0, 0, 0},
    {0, 1, 2, 6, 7, 0, 0, 0},
    {0, 1, 2, 5, 0, 0, 0, 0},
    {0, 1, 2, 5, 7, 0, 0, 0},
    {0, 1, 2, 5, 6, 0, 0, 0},
    {0, 1, 2, 5, 6, 7, 0, 0},
    {0, 1, 2, 4, 0, 0, 0, 0},
    {0, 1, 2, 4, 7, 0, 0, 0},
    {0, 1, 2, 4, 6, 0, 0, 0},
    {0, 1, 2, 4, 6, 7, 0, 0},
    {0, 1, 2, 4, 5, 0, 0, 0},
    {0, 1, 2, 4, 5, 7, 0, 0},
    {0, 1, 2, 4, 5, 6, 0, 0},
    {0, 1, 2, 4, 5, 6, 7, 0},
    {0, 1, 2, 3, 0, 0, 0, 0},
    {0, 1, 2, 3, 7, 0, 0, 0},
    {0, 1, 2, 3, 6, 0, 0, 0},
    {0, 1, 2, 3, 6, 7, 0, 0},
    {0, 1, 2, 3, 5, 0, 0, 0},
    {0, 1, 2, 3, 5, 7, 0, 0},
    {0, 1, 2, 3, 5, 6, 0, 0},
    {0, 1, 2, 3, 5, 6, 7, 0},
    {0, 1, 2, 3, 4, 0, 0, 0},
    {0, 1, 2, 3, 4, 7, 0, 0},
    {0, 1, 2, 3, 4, 6, 0, 0},
    {0, 1, 2, 3, 4, 6, 7, 0},
    {0, 1, 2, 3, 4, 5, 0, 0},
    {0, 1, 2, 3, 4, 5, 7, 0},
    {0, 1, 2, 3, 4, 5, 6, 0},
    {0, 1, 2, 3, 4, 5, 6, 7},
};

static const uint8_t unary_run_ends_num[256] = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,
};
// clang-format on

uint32_t unpack_gclis_unary_run_avx2(const uint8_t* mem, uint32_t* bit_pos, uint32_t byte_limit, uint8_t* out, uint32_t count,
                                     int8_t gtli, int32_t* error) {
    /* ends[0] is the end of the code before the chunk, then come the ones of the chunk. The last byte
     * may add up to seven positions past what is taken, and every store writes eight. */
    uint16_t ends[1 + UNARY_RUN_CHUNK + 16];
    uint16_t* const chunk_ends = ends + 1;
    const __m256i max_length = _mm256_set1_epi16(32);
    const __m256i value_offset = _mm256_set1_epi16((int16_t)(gtli - 1));
    uint32_t pos = *bit_pos;
    uint32_t done = 0;
    __m256i too_long = _mm256_setzero_si256();
    int32_t err = 0;
    while (done < count) {
        const uint32_t want = MIN(count - done, UNARY_RUN_CHUNK);
        const uint32_t base = pos >> 3;
        const uint32_t end = MIN(byte_limit, base + UNARY_RUN_CHUNK_BYTES);
        if (base >= end) {
            break;
        }
        /* The bits of the first byte already used end no code. Before them is position -1 modulo
         * 2^16, which the 16-bit subtract below takes as it should. */
        ends[0] = (uint16_t)((pos & 7) - 1);
        uint32_t byte_value = (uint8_t)~mem[base] & (0xFFu >> (pos & 7));
        _mm_storeu_si128((__m128i*)chunk_ends, _mm_cvtepu8_epi16(_mm_loadl_epi64((const __m128i*)unary_run_ends[byte_value])));
        uint32_t n = unary_run_ends_num[byte_value];
        for (uint32_t byte = base + 1; n < want && byte < end; byte++) {
            byte_value = (uint8_t)~mem[byte];
            const __m128i ends_byte = _mm_cvtepu8_epi16(_mm_loadl_epi64((const __m128i*)unary_run_ends[byte_value]));
            _mm_storeu_si128((__m128i*)(chunk_ends + n), _mm_add_epi16(ends_byte, _mm_set1_epi16((int16_t)((byte - base) * 8))));
            n += unary_run_ends_num[byte_value];
        }
        const uint32_t m = MIN(n, want);
        if (!m) {
            break;
        }
        uint32_t k = 0;
        for (; k + 16 <= m; k += 16) {
            const __m256i length = _mm256_sub_epi16(_mm256_loadu_si256((const __m256i*)(chunk_ends + k)),
                                                    _mm256_loadu_si256((const __m256i*)(ends + k)));
            too_long = _mm256_or_si256(too_long, _mm256_cmpgt_epi16(length, max_length));
            const __m256i value = _mm256_add_epi16(length, value_offset);
            _mm_storeu_si128((__m128i*)(out + done + k),
                             _mm_packus_epi16(_mm256_castsi256_si128(value), _mm256_extracti128_si256(value, 1)));
        }
        for (; k < m; k++) {
            const int32_t length = (uint16_t)(chunk_ends[k] - ends[k]);
            err |= length > 32;
            out[done + k] = (uint8_t)(gtli + length - 1);
        }
        pos = base * 8 + chunk_ends[m - 1] + 1;
        done += m;
        if (m < want) {
            break;
        }
    }
    *bit_pos = pos;
    *error = err | !_mm256_testz_si256(too_long, too_long);
    return done;
}

SvtJxsErrorType_t unpack_data_common(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis,
                                     uint32_t group_size, uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num,
                                     int32_t* precinct_bits_left, unpack_groups_fn groups_sign, unpack_groups_fn groups_nosign) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);
    assert((bitstream->bits_used != 0) || (bitstream->bits_used != 4));
    const uint32_t group_num = w / GROUP_SIZE;
    const uint32_t leftover = w % GROUP_SIZE;
    const __m128i gtli_const = _mm_set1_epi8(gtli);
    const uint32_t safe_bytes = unpack_safe_byte_count(bitstream->size > bitstream->offset ? bitstream->size - bitstream->offset
                                                                                           : 0);

    if (sign_flag == 0) {
        //Calculate how many bits will be used from bitstream to avoid reading out of memory allocation
        {
            uint8_t* gclis_ptr = gclis;
            const __m256i zero = _mm256_setzero_si256();
            __m256i sum_epi32 = zero;
            for (uint32_t group = 0; group < (group_num / 16); group++) {
                __m128i bits_epu8 = _mm_subs_epu8(_mm_loadu_si128((__m128i*)gclis_ptr), gtli_const);
                /* Unsigned widening: subs_epu8 is an unsigned saturating subtract, so on a
                 * corrupt stream the byte can be anything up to 255. Widening it as signed
                 * would make such a group subtract from the budget instead of adding to it,
                 * and the bitstream would not be rejected. */
                __m256i bits_epu16 = _mm256_cvtepu8_epi16(bits_epu8);
                __m256i signs = _mm256_cmpgt_epi16(bits_epu16, zero);
                signs = _mm256_srli_epi16(signs, 15);
                /* Cost of one group: its planes plus one nibble of signs. At most 256, so
                 * the per-group value still fits a 16-bit lane; the running total does not
                 * and is widened before it is added. */
                __m256i group_bits = _mm256_add_epi16(bits_epu16, signs);
                sum_epi32 = _mm256_add_epi32(sum_epi32, _mm256_unpacklo_epi16(group_bits, zero));
                sum_epi32 = _mm256_add_epi32(sum_epi32, _mm256_unpackhi_epi16(group_bits, zero));
                gclis_ptr += 16;
            }
            uint32_t bits_sum = unpack_budget_hsum_epi32(sum_epi32);
            for (uint32_t group = 0; group < (group_num % 16) + !!leftover; group++) {
                if (gclis_ptr[group] > gtli) {
                    bits_sum += (gclis_ptr[group] - gtli) + 1;
                }
            }
            /* The cost is compared before it is subtracted, so that the widened sum
             * cannot be lost again in the last step: a line whose nibble count exceeds
             * what int32_t holds is rejected rather than wrapped into a small debit. */
            if (*precinct_bits_left < 0 || (uint64_t)bits_sum * 4 > (uint64_t)*precinct_bits_left) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }
            *precinct_bits_left -= (int32_t)(bits_sum * 4);
        }
        reader_short_t reader;
        reader.mem = (uint8_t*)(bitstream->mem) + bitstream->offset;
        reader.bits_used = bitstream->bits_used;
        groups_sign(gclis, gtli, &reader, buf, group_num, safe_bytes);
        if (leftover) {
            buf += group_num * GROUP_SIZE;
            gclis += group_num;
            uint16_t buf_tmp[GROUP_SIZE];
            groups_sign(gclis, gtli, &reader, buf_tmp, 1, 0);
            memcpy(buf, buf_tmp, sizeof(uint16_t) * (leftover));
        }
        bitstream->offset = (uint32_t)(reader.mem - bitstream->mem);
        bitstream->bits_used = reader.bits_used;
    }
    else {
        //Calculate how many bits will be used from bitstream to avoid reading out of memory allocation
        {
            uint8_t* gclis_ptr = gclis;
            const __m256i zero = _mm256_setzero_si256();
            __m256i sum_epi32 = zero;
            for (uint32_t group = 0; group < (group_num / 16); group++) {
                __m128i bits_epu8 = _mm_subs_epu8(_mm_loadu_si128((__m128i*)gclis_ptr), gtli_const);
                /* Unsigned widening, and 32-bit accumulation, for the same reasons as in
                 * the branch above. */
                __m256i group_bits = _mm256_cvtepu8_epi16(bits_epu8);
                sum_epi32 = _mm256_add_epi32(sum_epi32, _mm256_unpacklo_epi16(group_bits, zero));
                sum_epi32 = _mm256_add_epi32(sum_epi32, _mm256_unpackhi_epi16(group_bits, zero));
                gclis_ptr += 16;
            }
            uint32_t bits_sum = unpack_budget_hsum_epi32(sum_epi32);
            for (uint32_t group = 0; group < (group_num % 16) + !!leftover; group++) {
                if (gclis_ptr[group] > gtli) {
                    bits_sum += (gclis_ptr[group] - gtli);
                }
            }
            /* The cost is compared before it is subtracted, so that the widened sum
             * cannot be lost again in the last step: a line whose nibble count exceeds
             * what int32_t holds is rejected rather than wrapped into a small debit. */
            if (*precinct_bits_left < 0 || (uint64_t)bits_sum * 4 > (uint64_t)*precinct_bits_left) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }
            *precinct_bits_left -= (int32_t)(bits_sum * 4);
        }
        reader_short_t reader;
        reader.mem = (uint8_t*)(bitstream->mem) + bitstream->offset;
        reader.bits_used = bitstream->bits_used;
        groups_nosign(gclis, gtli, &reader, buf, group_num, safe_bytes);
        if (leftover) {
            buf += group_num * GROUP_SIZE;
            gclis += group_num;
            uint16_t buf_tmp[GROUP_SIZE];
            groups_nosign(gclis, gtli, &reader, buf_tmp, 1, 0);
            *leftover_signs_num = 0;
            for (uint32_t leftover_id = leftover; leftover_id < GROUP_SIZE; leftover_id++) {
                *leftover_signs_num += !!buf_tmp[leftover_id];
            }
            memcpy(buf, buf_tmp, sizeof(uint16_t) * (leftover));
        }
        bitstream->offset = (uint32_t)(reader.mem - bitstream->mem);
        bitstream->bits_used = reader.bits_used;
    }

    return SvtJxsErrorNone;
}

SvtJxsErrorType_t unpack_data_avx2(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis, uint32_t group_size,
                                   uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num, int32_t* precinct_bits_left) {
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

/* See unpack_raw_gclis_bytes_c(). Each byte is widened to a 16-bit lane, which then holds the high
 * nibble in its low byte and the low nibble in its high byte - the two GCLIs in stream order. */
void unpack_raw_gclis_bytes_avx2(const uint8_t* mem, uint8_t* gclis, uint32_t bytes) {
    const __m256i low_nibble = _mm256_set1_epi16(0x0F);
    uint32_t i = 0;
    for (; i + 16 <= bytes; i += 16) {
        const __m256i v = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i*)(mem + i)));
        const __m256i out = _mm256_or_si256(_mm256_srli_epi16(v, 4), _mm256_slli_epi16(_mm256_and_si256(v, low_nibble), 8));
        _mm256_storeu_si256((__m256i*)(gclis + 2 * i), out);
    }
    for (; i < bytes; i++) {
        gclis[2 * i] = mem[i] >> 4;
        gclis[2 * i + 1] = mem[i] & 0xF;
    }
}
