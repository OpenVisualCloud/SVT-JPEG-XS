# Independent decoded-sample check

The standalone driver links to the unmodified
[ISO/IEC 21122-5 edition-3 reference software](https://standards.iso.org/iso-iec/21122/-5/ed-3/en/ISO_IEC_21122-5_ED-3.zip).
Archive SHA256: `36efc26b92a09b7447bc6e580c902d5c19ee9e919b324609768e390d38621835`.
No reference implementation source is included in this repository.

Build SVT with `BUILD_TESTING=ON`, then create fresh stream/sample output directories:

```sh
mkdir profile-streams
JPEGXS_PROFILE_ARTIFACT_DIR="$PWD/profile-streams" Bin/Release/SvtJpegxsUnitTests --gtest_filter=AutomaticProfileConstraints.DeclarationOnlyPreservesPayloadAndPixels
```

Set `jxs_ref_root` to the extracted ISO archive and `jxs_ref_build` to a fresh
external build directory:

```sh
cmake -S "$jxs_ref_root" -B "$jxs_ref_build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$jxs_ref_build" -j4
cc -O2 -I "$jxs_ref_root/libjxs/public" tests/reference/reference_decode.c "$jxs_ref_build/bin/libjxs.a" -lm -o reference-decode
python3 tests/reference/check_reference_decode.py "$PWD/reference-decode" profile-streams reference-samples
```

The driver probes and validates each stream, invokes `xs_dec_bitstream` and
`xs_dec_postprocess_image`, and writes every active native-plane sample (LE for
depth >8). The checker requires 476 cases and compares every byte to the SVT
sample export. Missing files, decoder errors and mismatches fail the check;
per-case logs and a summary receipt are retained. These are focused progressive
profile-declaration controls, not a complete decoder conformance qualification.
