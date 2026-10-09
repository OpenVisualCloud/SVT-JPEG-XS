#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Shared by PerformanceTestSampleApp.sh / PerformanceTestGstreamer.sh /
# PerformanceTestFfmpegPlugin.sh: choose the NUMA node a perf run is pinned to and the CPUs on it
# the measured process may run on.
#
# The node is NOT chosen freely: CI may pin each GitHub runner service to a node (see
# documentation/ci-cd/self-hosted-runner-setup.md), and the run must stay inside that placement.
# So the node is derived from the placement this shell already has (/proc/self/status) rather
# than asserted. The node must hold at least one CPU in Cpus_allowed_list - that reflects both
# cpuset limits and plain sched_setaffinity pinning (taskset, systemd CPUAffinity=) - and must
# be in Mems_allowed_list, which reflects only cpuset limits and so cannot be relied on alone. On
# an unpinned host every node qualifies and node 1 is preferred, since that is what the
# baselines were calibrated on.
#
# NUMA_PHYS_CPUS lists exactly one logical CPU per physical core of that node (the lowest-numbered
# SMT sibling). The inherited binding still allows both hyperthreads of each core, so two worker
# threads could land on the same physical core and share its execution units - placement, and
# therefore FPS, would then vary from run to run. Passing NUMA_PHYS_CPUS to --physcpubind is a
# narrowing of what we already have, and keeps every worker on its own core.
#
# Usage: source this file, then run the measured command under
#   numactl --physcpubind="$NUMA_PHYS_CPUS" --membind="$NUMA_NODE" ...

# Expands a sysfs/procfs list ("0-3,8,10-11") into one number per line.
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

# CPUs this process may run on, and nodes it may allocate memory on - the placement inherited
# from the runner service. Mems falls back to node 0 if the kernel does not report it.
allowed_cpus=$(expand_cpulist "$(awk '/^Cpus_allowed_list:/ {print $2}' /proc/self/status)")
allowed_mems=$(expand_cpulist "$(awk '/^Mems_allowed_list:/ {print $2}' /proc/self/status)")
if [ -z "$allowed_mems" ]; then
    allowed_mems=0
fi

# Nodes that are memory-eligible and contain at least one allowed CPU, one per line, ascending.
allowed_nodes=$(
    for node in $allowed_mems; do
        [ -r "/sys/devices/system/node/node$node/cpulist" ] || continue
        for cpu in $(expand_cpulist "$(cat "/sys/devices/system/node/node$node/cpulist")"); do
            if grep -qx "$cpu" <<< "$allowed_cpus"; then
                echo "$node"
                break
            fi
        done
    done
)
if [ -z "$allowed_nodes" ]; then
    echo "ERROR: no memory-eligible NUMA node holds an allowed CPU (Cpus_allowed: $(tr '\n' ',' <<< "$allowed_cpus") Mems_allowed: $(tr '\n' ',' <<< "$allowed_mems"))" >&2
    exit 1
fi

if grep -qx 1 <<< "$allowed_nodes"; then
    NUMA_NODE=1
else
    NUMA_NODE=$(head -n1 <<< "$allowed_nodes")
fi

# Prints the CPUs of node $1 that we are allowed to run on and that are the first SMT sibling of
# their core, comma-separated.
physical_cpus_of_node() {
    local cpu siblings
    local -a cpus=()
    for cpu in $(expand_cpulist "$(cat "/sys/devices/system/node/node$1/cpulist")"); do
        grep -qx "$cpu" <<< "$allowed_cpus" || continue
        siblings=$(cat "/sys/devices/system/cpu/cpu$cpu/topology/thread_siblings_list")
        # thread_siblings_list starts with the lowest sibling, in either "a,b" or "a-b" form.
        siblings="${siblings%%[,-]*}"
        [ "$siblings" = "$cpu" ] && cpus+=("$cpu")
    done
    (IFS=','; echo "${cpus[*]}")
}

NUMA_PHYS_CPUS=$(physical_cpus_of_node "$NUMA_NODE")
if [ -z "$NUMA_PHYS_CPUS" ]; then
    echo "ERROR: no usable CPUs on NUMA node $NUMA_NODE (allowed nodes: $(tr '\n' ',' <<< "$allowed_nodes"))" >&2
    exit 1
fi
echo "NUMA node $NUMA_NODE, physical-core CPUs: $NUMA_PHYS_CPUS"
