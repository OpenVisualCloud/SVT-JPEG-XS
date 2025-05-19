#!/bin/bash
exec=$1
nproc=$2
param=$3
valgrind=""
tmp_dir="tmp_ut_parallel"

echo "Example:"$0" UT_exe [parallel_number] [valgrind]"

[[ -z "$nproc" ]] && nproc=1

echo "Run UT Parallel NPROC: $nproc"
echo "UT: $exec"
if [ "$param" = "valgrind" ]; then
    echo "Valgrind: ON"
    valgrind="valgrind "
else
    echo "Valgrind: OFF"
fi

cmd="$valgrind$exec"
echo "Run command: $cmd"
mkdir $tmp_dir

pid_array=()
export GTEST_TOTAL_SHARDS=$nproc
for index in $(seq 0 $(($nproc-1))); do
export GTEST_SHARD_INDEX=$index
    ${cmd} > $tmp_dir/out_$index.txt 2>&1 & pid_id=$!
    pid_array+=($pid_id)
    echo "Run: $index/$nproc PID: $pid_id"
done

error=0
error_list=""
if [ ! ${#pid_array[@]} -ne 0 ]; then
    error=1
    echo "NOT START ANY JOB!!!"
else
    echo "Wait ${#pid_array[@]} jobs..."
    index=0
    index_first_error=0
    for pid in ${pid_array[*]}; do
        wait $pid
        ret=$?
        echo "Job $index/$nproc return: $ret"
        if [[ $ret -ne 0 ]]; then
            error_list="$error_list\nJob $index/$nproc return ERROR: $ret"
            if [[ $error -eq 0 ]]; then
                error=$ret
                index_first_error=$index
            fi
        fi
        index=$((index+1))
    done

    #Print output
    for index in $(seq 0 $(($nproc-1))); do
        echo "Out: $index/$nproc"
        cat "$tmp_dir/out_$index.txt"
    done
    #Print again job with first error
    if [ $error -ne 0 ]; then
        echo
        echo "Parallel Out: $index_first_error/$nproc FIRST ERROR: $error"
        cat "$tmp_dir/out_$index_first_error.txt"
    fi

    #echo -e change \n to break line
    echo -e $error_list
fi

rm -fr $tmp_dir

if [ $error -ne 0 ]; then
    echo "FAIL !!"
else
    echo "DONE OK"
fi
echo Exit $0 script with exit $error
exit $error