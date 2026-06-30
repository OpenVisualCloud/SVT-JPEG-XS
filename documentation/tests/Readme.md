# Testing

Steps to execute different levels of tests:

## Sample files

Set of sample files should be downloaded to server.
Set environment variable INPUT_FILES_PATH to point there.

## Build

```bash
cd Build/linux
./build.sh
cd ../..
```

## Execute sequential tests

### Decoder Conformance Test

```bash
cd tests/scripts
./DecoderConformanceTest.sh $INPUT_FILES_PATH ../../Bin/Release/SvtJpegxsDecApp
cd ../..
```

### Decoder Multi Frames Test

```bash
cd tests/scripts
./DecoderConformanceTest.sh $INPUT_FILES_PATH ../../Bin/Release/SvtJpegxsDecApp
cd ../..
```

### Encoder Test

```bash
cd tests/scripts
./EncoderTest.sh $INPUT_FILES_PATH ../../Bin/Release/SvtJpegxsEncApp
cd ../..
```

### Unit Test

```bash
Bin/Release/SvtJpegxsUnitTests
```

## Execute parallel tests

### Run all tests parallel

This command runs parallelly the files:

- DecoderConformanceTest.sh
- DecoderConformanceTest.sh
- EncoderTest.sh

```bash
./ParallelAllTests.sh $(nproc) $INPUT_FILES_PATH ../../Bin/Release/SvtJpegxsDecApp
```

### Unit Test parallel

```bash
./parrallelUT.sh ../../Bin/Release/SvtJpegxsUnitTests $(nproc)
```
