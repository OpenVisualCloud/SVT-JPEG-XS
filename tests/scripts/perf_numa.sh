#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Shared by PerformanceTestSampleApp.sh / PerformanceTestGstreamer.sh /
# PerformanceTestFfmpegPlugin.sh: choose the NUMA node a perf run is pinned to and the CPUs on it
# the measured process may run on.
#
# Node 1 is what the dual-socket CI runner is calibrated against; a single-node host (no node1
# under sysfs) has nothing to bind to there, so fall back to node 0.
#
# NUMA_PHYS_CPUS lists exactly one logical CPU per physical core of that node (the lowest-numbered
# SMT sibling). numactl --cpunodebind would allow every logical CPU of the node, including the
# second hyperthread of each core, so two worker threads could land on the same physical core and
# share its execution units - placement, and therefore FPS, would then vary from run to run.
# Passing NUMA_PHYS_CPUS to --physcpubind keeps every worker on its own core.
#
# Usage: source this file, then run the measured command under
#   numactl --physcpubind="$NUMA_PHYS_CPUS" --membind="$NUMA_NODE" ...

if [ -d /sys/devices/system/node/node1 ]; then
    NUMA_NODE=1
else
    NUMA_NODE=0
fi

# Expands a sysfs cpulist ("0-3,8,10-11") into one CPU number per line.
expand_cpulist() {
    local range
    local -a ranges
    IFS=',' read -ra ranges <<< "$1"
    for range in "${ranges[@]}"; do
        if [[ "$range" == *-* ]]; then
            seq "${range%-*}" "${range#*-}"
        else
            echo "$range"
        fi
    done
}

# Prints the CPUs of node $1 that are the first SMT sibling of their core, comma-separated.
physical_cpus_of_node() {
    local cpu siblings
    local -a cpus=()
    for cpu in $(expand_cpulist "$(cat "/sys/devices/system/node/node$1/cpulist")"); do
        siblings=$(cat "/sys/devices/system/cpu/cpu$cpu/topology/thread_siblings_list")
        # thread_siblings_list starts with the lowest sibling, in either "a,b" or "a-b" form.
        siblings="${siblings%%[,-]*}"
        [ "$siblings" = "$cpu" ] && cpus+=("$cpu")
    done
    (IFS=','; echo "${cpus[*]}")
}

NUMA_PHYS_CPUS=$(physical_cpus_of_node "$NUMA_NODE")
if [ -z "$NUMA_PHYS_CPUS" ]; then
    echo "ERROR: no CPUs found on NUMA node $NUMA_NODE" >&2
    exit 1
fi
echo "NUMA node $NUMA_NODE, physical-core CPUs: $NUMA_PHYS_CPUS"
