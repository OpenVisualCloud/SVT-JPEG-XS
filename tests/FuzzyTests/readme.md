# Fuzzy Tests for SVT-JPEG-XS (Scalable Video Technology for JPEG XS)

This document describes the Fuzzy Tests used to validate the SVT-JPEG-XS encoder and decoder implementations.
The tests are designed to ensure that the encoder and decoder does not crash for different configurations and input data.
Please see https://llvm.org/docs/LibFuzzer.html for more information.

Prerequisites:

- Linux Environment
- Clang compiler
- libstdc++-${gcc-version}-dev installed
  - where gcc-version is the highest gcc version installed on host

## Encoder Fuzzy Tests

### Build guide

1. Build and install SVT-JPEG-XS:
   - `cd <repo_dir>/Build/linux && ./build.sh install`
2. Export Environment Variables:
   - `export CPATH="/usr/local/include/svt-jpegxs" && export LD_LIBRARY_PATH="/usr/local/lib/${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"`
3. Build Fuzzy Tests:
   - `cd <repo_dir>/tests/FuzzyTests && clang -lSvtJpegxs -fsanitize=fuzzer  encoder.c -o SvtJxsEncFuzzer`

### Usage

> Note: Running the command overwrites previously saved logs. Make sure to move them to archive before executing.

`./SvtJxsEncFuzzer -max_len=96 -rss_limit_mb=15000 -max_total_time=60 -jobs=20`

Where:

- `-max_len` is the maximum length of the input data to be fuzzed. In case of encoder this is the
  size of the fuzzed subset of configuration parameters in `svt_jpeg_xs_encoder_api_t`
  (`config_short_t` in `encoder.c`, currently 64 bytes); the extra bytes leave room for future
  fields
- `-rss_limit_mb` is the maximum memory usage of the fuzzer in megabytes
- `-max_total_time` is the maximum time in seconds the fuzzer will run
- `-jobs` is the number of parallel jobs to run the fuzzer

## Decoder Fuzzy Tests

### Build guide

1. Build and install SVT-JPEG-XS:
   - `cd <repo_dir>/Build/linux && ./build.sh install`
2. Export Environment Variables:
   - `export CPATH="/usr/local/include/svt-jpegxs" && export LD_LIBRARY_PATH="/usr/local/lib/${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"`
3. Build Fuzzy Tests:
   - `cd <repo_dir>/tests/FuzzyTests && clang -lSvtJpegxs -fsanitize=fuzzer  decoder.c -o SvtJxsDecFuzzer`

### Usage

> Note: Running the command overwrites previously saved logs. Make sure to move them to archive before executing.

1. Prepare a corpus directory with encoded streams
   - Create a directory named `CORPUS_DIR` and copy multiple encoded streams to it. It is recommended to use a variety of encoded
     streams with different configurations and sizes to ensure comprehensive testing.
   - Alternatively `.github/scripts/fuzzy_tests.sh -td` prepares the corpus and runs the fuzzer (see
     [Running with fuzzy_tests.sh](#running-with-fuzzy_testssh)): it copies the conformance streams from
     `INPUT_FILES_PATH` (if set) and adds small streams generated with `SvtJpegxsEncApp` covering lossless (8/10/12-bit),
     RCT, 4:2:2:4 and 4:4:4:4 formats, raw mode and slice packetization.
2. Run the fuzzer with the corpus directory:
   - `./SvtJxsDecFuzzer CORPUS_DIR -rss_limit_mb=15000 -max_total_time=60 -jobs=20`

Where:

- `CORPUS_DIR` is the path to the directory containing the encoded streams
- `-rss_limit_mb` is the maximum memory usage of the fuzzer in megabytes
- `-max_total_time` is the maximum time in seconds the fuzzer will run
- `-jobs` is the number of parallel jobs to run the fuzzer

## Running with fuzzy_tests.sh

`.github/scripts/fuzzy_tests.sh` is the script used by the Fuzzy Tests CI workflow. It accepts:

- `-b` builds and installs SVT-JPEG-XS (skipped if `SvtJpegxs.pc` is already installed under the install prefix) and builds
  both fuzzers in `tests/FuzzyTests`
- `-te` runs the encoder fuzzer
- `-td` prepares the decoder corpus and runs the decoder fuzzer

The fuzzers must be built (`-b`) before `-te` or `-td` is used. The script reads these environment variables:

- `JOBS_NUM` and `TIMEOUT_SECONDS` (required for `-te`/`-td`): values passed to `-jobs` and `-max_total_time`
- `SVT_INSTALL_DIR` (optional, default `/usr/local`): install prefix of SVT-JPEG-XS, used for the headers, the library and
  `bin/SvtJpegxsEncApp` (which generates the extra decoder seed streams)
- `INPUT_FILES_PATH` (optional): directory whose contents are copied into the decoder corpus. It must point directly at the
  directory holding the conformance `.jxs` streams (`<samples>/test_bitsreams`), not at the samples root used by
  [the test scripts](../../documentation/tests/Readme.md)
- `DECODER_SAMPLES` (optional, default `tests/FuzzyTests/decoder_corpus`): decoder corpus directory

Example:

```bash
export JOBS_NUM=20 TIMEOUT_SECONDS=60
export INPUT_FILES_PATH=/opt/samples/test_bitsreams
.github/scripts/fuzzy_tests.sh -b
.github/scripts/fuzzy_tests.sh -td
```

## Notes

If Fuzzy Test crashes, it will print the error message and store the input data that caused the crash in a file named `crash-*` in the current directory.
To reproduce the crash, you can use the `crash-*` file with the fuzzer executable as in examples below:

- `./SvtJxsEncFuzzer crash-*`
- `./SvtJxsDecFuzzer crash-*`

With `-jobs`, libFuzzer also writes the output of each job to `fuzz-*.log` in the current directory.

The Fuzzy Tests CI workflow does not upload `crash-*` or `fuzz-*.log` files as artifacts. They remain in
`tests/FuzzyTests/` of the self-hosted runner's workspace, where a later checkout on that runner may remove them; the
job log contains the fuzzer output.
