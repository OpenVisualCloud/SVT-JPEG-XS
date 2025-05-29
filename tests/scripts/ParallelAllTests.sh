#!/bin/bash
echo "Example: $0 parallel_number"
nproc=$1
script_params=""

index=0
for ARG in "$@"; do
    if (($index >= 1)); then
        #echo $ARG
        script_params="$script_params $ARG"
    fi
    index=$((index+1))
done

[[ -z "$nproc" ]] && nproc=1
echo "Run Parallel All NPROC: $nproc params: $script_params"
chmod +x ./CommonLib.sh
chmod +x ./ParallelScript.sh
chmod +x ./DecoderConformanceTest.sh
chmod +x ./DecoderMultiFramesTest.sh
chmod +x ./EncoderTest.sh

error=0
if [[ $error -eq 0 ]]; then
    echo RUN DECODER CONFORMANCE TEST
    ./ParallelScript.sh $nproc ./DecoderConformanceTest.sh $script_params
    ret=$?
    [[ $ret -ne 0 ]] && error=$ret
fi
if [[ $error -eq 0 ]]; then
    echo RUN DECODER MULTI FRAMES TEST
    ./ParallelScript.sh $nproc ./DecoderMultiFramesTest.sh $script_params
    ret=$?
    [[ $ret -ne 0 ]] && error=$ret
fi
if [[ $error -eq 0 ]]; then
    echo RUN ENOCDER TEST
    ./ParallelScript.sh $nproc ./EncoderTest.sh $script_params
    ret=$?
    [[ $ret -ne 0 ]] && error=$ret
fi

if [ $error -eq 0 ]; then
    echo "DONE OK"
else
    echo "FAIL !!"
fi

echo Exit $0 script with exit $error
exit $error
