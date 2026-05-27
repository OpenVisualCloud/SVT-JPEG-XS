---
name: run-jpegxs-tests
description: 'Build and run SVT-JPEG-XS unit tests with sanitizers. Use when: running tests, ASan, MSan, TSan, UBSan, IntSan, building with clang, validating fixes, memory safety, data races, undefined behavior, integer overflow.'
argument-hint: 'Sanitizer type (address|memory|thread|undefined|integer) and build type (debug|release)'
---

# Run JPEG-XS Tests

## Parameters

- **sanitizer**: One of `address`, `memory`, `thread`, `undefined`, `integer` (default: `address`)
- **build type**: `debug` or `release` (default: `debug`)
- **filter**: Optional gtest filter pattern (default: run all tests)

## Procedure

1. Build with the chosen sanitizer:

   ```bash
   cd Build/linux
   ./build.sh cc=clang cxx=clang++ sanitizer=<sanitizer> <build_type> test
   ```

2. Run tests:

   ```bash
   ../../Bin/<Debug|Release>/SvtJpegxsUnitTests [--gtest_filter="<filter>"]
   ```

3. Check output for:
   - `[  PASSED  ]` — all tests passed
   - `AddressSanitizer` / `MemorySanitizer` / `ThreadSanitizer` / `UndefinedBehaviorSanitizer` — sanitizer errors
   - `[  FAILED  ]` — test failures

## Sanitizer Notes

| Sanitizer | Detects | Flag |
| --------- | ------- | ---- |
| `address` | Heap/stack buffer overflow, use-after-free, double-free, leaks | `-DSANITIZER=address` |
| `memory` | Uninitialized memory reads | `-DSANITIZER=memory` |
| `thread` | Data races, deadlocks | `-DSANITIZER=thread` |
| `undefined` | Integer overflow, null deref, misaligned access, UB | `-DSANITIZER=undefined` |
| `integer` | Unsigned overflow, implicit truncation, sign changes | `-DSANITIZER=integer` |

## Integer Sanitizer (`integer`) Details

The `integer` sanitizer enables `-fsanitize=integer` which includes:

- `unsigned-integer-overflow` — unsigned arithmetic that wraps
- `implicit-integer-sign-change` — implicit conversions that change sign
- `implicit-integer-truncation` — implicit conversions that lose data

### Known False Positives

The integer sanitizer reports 4 errors from **GCC libstdc++ headers** (not project code):

- `bits/basic_string.h` — `npos` comparison uses intentional unsigned underflow
- `bits/random.tcc` (×2) — Mersenne Twister uses deliberate unsigned wrapping
- `bits/uniform_int_dist.h` — rejection sampling uses unsigned negation

These are correct standard library behavior (C/C++ defines unsigned overflow as wrapping) and cannot be fixed in project code.

### Common Fix Patterns

- **Left-shift of negative values**: Use `LSHIFT32(val, s)` macro (defined in `Source/Lib/Common/Codec/Definitions.h`)
- **Unsigned subtraction underflow**: Cast to signed type or use wider intermediate: `(int32_t)(a - b)`
- **Implicit truncation**: Add explicit cast: `(uint16_t)(expr)`
- **Unsigned overflow in shift**: Widen to `uint64_t` before shifting

## Memory Sanitizer (`memory`) Details

### Running MSan Tests

Unit tests (`SvtJpegxsUnitTests`) CANNOT run under MSan — googletest triggers a false positive in
libc++ `ios_base::precision` during global init. Instead, test using the encoder/decoder apps:

```bash
cd Build/linux
./build.sh cc=clang cxx=clang++ sanitizer=memory debug

# Generate test input
dd if=/dev/urandom of=/tmp/test_input.yuv bs=$((1920*1080*2)) count=1

# Test encoder + decoder (use --asm avx2 or --asm c)
cd ../../Bin/Debug
./SvtJpegxsEncApp -i /tmp/test_input.yuv -w 1920 -h 1080 --input-depth 8 \
    --colour-format yuv422 -b /tmp/test.jxs --bpp 3 --asm avx2
./SvtJpegxsDecApp -i /tmp/test.jxs -o /tmp/test_dec.yuv --asm avx2
```

### AVX-512 False Positives

MSan reports false positives in AVX-512 SIMD code paths. Clang's MSan instrumentation does not
properly track initialization state through `_mm512_*` intrinsic expansions. The reported "origin"
is a stack-allocated compiler intermediate (e.g., `__I.addr.i201`), not actual heap/stack memory.

**Affected functions:**

- `dwt_horizontal_line_avx512` (Encoder: `Source/Lib/Encoder/ASM_AVX512/Enc_avx512.c`)
- `idwt_horizontal_line_lf16_hf16_avx512` (Decoder: `Source/Lib/Decoder/ASM_AVX512/idwt-avx512.c`)

**Verification:** The C (`--asm c`) and AVX2 (`--asm avx2`) paths are MSan-clean, confirming the
AVX-512 reports are false positives since all paths process the same data.

**Recommendation:** Always run MSan tests with `--asm avx2` (or `--asm c`) to avoid false positives.

## Important

- MSan (`memory`) requires all dependencies built with MSan instrumentation — expect false positives from uninstrumented libraries.
- TSan (`thread`) and ASan (`address`) cannot be combined in the same build.
- Always use `clang` — GCC sanitizer support is less complete.
- The `integer` sanitizer has ~20× runtime overhead; test runs take significantly longer.
- Filter examples: `--gtest_filter="*Decoder*"`, `--gtest_filter="*DWT*:*FRAME*"`
