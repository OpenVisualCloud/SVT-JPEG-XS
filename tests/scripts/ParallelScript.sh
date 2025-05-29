#!/bin/bash
echo "Example: $0 parallel_number script_name.sh [parametr_to_script_1] [parametr_to_script_2] [...]"
nproc=$1
script_name=$2
script_params=""
tmp_dir_parallel="tmp_parallel_script"

index=0
for ARG in "$@"; do
    if (($index >= 2)); then
        #echo $ARG
        script_params="$script_params $ARG"
    fi
    index=$((index+1))
done

[[ -z "$nproc" ]] && nproc=1
echo "Run Parallel $script_name NPROC: $nproc Script params: $script_params"

#Scripts param needet to detect fast etc.
cmd="$script_name num $script_params"
source ${cmd}
TEST_NUMBER=${SCRIPT_TESTS_NUMBER}
echo "Tests in script: $TEST_NUMBER"
cmd="$script_name $script_params"
echo "Run command: $cmd"
mkdir $tmp_dir_parallel
tests_per_proc=$(((TEST_NUMBER+nproc-1)/nproc))
echo "tests_per_proc: $tests_per_proc"

pid_array=()
range_start=0
range_end=0
for index in $(seq 0 $(($nproc-1))); do
    range_end=$((range_start+tests_per_proc))
    cmd_proc="$cmd tmp:$tmp_dir_parallel/tmp_$index range:$range_start-$range_end"
    range_start=$range_end
    ${cmd_proc} > $tmp_dir_parallel/out_$index.txt 2>&1 & pid_id=$!
    pid_array+=($pid_id)
    echo "Run: $index/$nproc PID: $pid_id $cmd_proc"
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
        echo
        echo "Parallel Out: $index/$nproc"
        echo
        cat "$tmp_dir_parallel/out_$index.txt"
    done
    #Print again job with first error
    if [ $error -ne 0 ]; then
        echo
        echo "Parallel Out: $index_first_error/$nproc FIRST ERROR: $error"
        cat "$tmp_dir_parallel/out_$index_first_error.txt"
    fi

    #echo -e change \n to break line
    echo -e $error_list
fi

rm -fr $tmp_dir_parallel

if [ $error -ne 0 ]; then
    echo "FAIL !!"
else
    echo "DONE OK"
fi
echo Exit $0 script with exit $error
exit $error
