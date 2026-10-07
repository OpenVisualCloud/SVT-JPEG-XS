#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Host setup for the loopback tests: two VFs of PF_PORT on vfio-pci and enough 2 MiB hugepages.
# Writes tests/.host.env (TX_PORT, RX_PORT, NUMA node, CPU sets) for the test scripts.
#
#   PF_PORT=0000:15:00.0 ./setup_host.sh [--dry-run]   set up, or reuse what is there
#   ./setup_host.sh --check                           exit 0 if .host.env is still valid
#   ./setup_host.sh --undo [--dry-run]                revert only what an earlier setup changed
#
# VFs already on the PF are reused when two are bound to vfio-pci; VFs are created (with
# MTL_ROOT/script/nicctl.sh) only when the PF has none, and sriov_numvfs is never rewritten
# otherwise. Hugepages are only ever raised. --undo removes the VFs only if setup created them,
# which disrupts anything else using them.

# shellcheck source-path=SCRIPTDIR
set -eo pipefail
# shellcheck source=common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

VF_COUNT="${VF_COUNT:-2}"
HUGEPAGES="${HUGEPAGES:-2048}"
STATE_FILE="$TESTS_DIR/.host.state"
DRY_RUN=0
ACTION=setup
for arg in "$@"; do
    case "$arg" in
        --dry-run) DRY_RUN=1 ;;
        --undo) ACTION=undo ;;
        --check) ACTION=check ;;
        *) die "unknown argument $arg (expected --dry-run, --undo or --check)" ;;
    esac
done

# run <command...>: execute, or only print it with --dry-run
run() {
    if [ "$DRY_RUN" = 1 ]; then
        echo "dry-run: $*"
    else
        echo "run: $*"
        "$@"
    fi
}

state_get() {
    if [ -f "$STATE_FILE" ]; then
        sed -n "s/^$1=//p" "$STATE_FILE" | tail -1
    fi
}

driver_of() {
    basename "$(readlink "/sys/bus/pci/devices/$1/driver" 2>/dev/null)"
}

# vfs_of <pf>: VF addresses in virtfn order
vfs_of() {
    local i=0
    while [ -e "/sys/bus/pci/devices/$1/virtfn$i" ]; do
        basename "$(readlink "/sys/bus/pci/devices/$1/virtfn$i")"
        i=$((i + 1))
    done
}

hugepages_now() {
    cat /proc/sys/vm/nr_hugepages
}

# .host.env still matches the host: ports on vfio-pci, hugepages as required
check_host_env() {
    [ -f "$HOST_ENV" ] || return 1
    local tx rx pf
    tx="$(sed -n 's/^TX_PORT=//p' "$HOST_ENV")"
    rx="$(sed -n 's/^RX_PORT=//p' "$HOST_ENV")"
    pf="$(sed -n 's/^PF_PORT=//p' "$HOST_ENV")"
    [ -z "${PF_PORT:-}" ] || [ "$pf" = "$PF_PORT" ] || return 1
    [ "$(driver_of "$tx")" = vfio-pci ] && [ "$(driver_of "$rx")" = vfio-pci ] || return 1
    [ "$(hugepages_now)" -ge "$HUGEPAGES" ]
}

if [ "$ACTION" = check ]; then
    if check_host_env; then
        echo "$HOST_ENV is valid"
        exit 0
    fi
    echo "$HOST_ENV is missing or stale"
    exit 1
fi

if [ "$ACTION" = undo ]; then
    [ -f "$STATE_FILE" ] || {
        echo "no $STATE_FILE: nothing was changed by setup_host.sh"
        exit 0
    }
    pf="$(state_get PF_PORT)"
    if [ "$(state_get CREATED_VFS)" = 1 ]; then
        echo "WARNING: removing all VFs of $pf, anything still using them loses its port"
        run sudo sh -c "echo 0 > /sys/bus/pci/devices/$pf/sriov_numvfs"
    fi
    before="$(state_get HUGEPAGES_BEFORE)"
    if [ -n "$before" ]; then
        run sudo sysctl -w "vm.nr_hugepages=$before"
    fi
    run rm -f "$STATE_FILE" "$HOST_ENV"
    exit 0
fi

# --- setup ----------------------------------------------------------------------------------

[ -n "${PF_PORT:-}" ] || die "PF_PORT is not set: the PCI address of the PF whose VFs the tests use"
[ -e "/sys/bus/pci/devices/$PF_PORT/sriov_numvfs" ] || die "$PF_PORT is not an SR-IOV capable PF"
[ "$VF_COUNT" -ge 2 ] || die "VF_COUNT must be at least 2"

# what an earlier setup changed: only carried over for the same PF, --undo reverts it as one
state_pf="$(state_get PF_PORT)"
if [ -n "$state_pf" ] && [ "$state_pf" != "$PF_PORT" ]; then
    die "an earlier setup changed $state_pf ($STATE_FILE): run ./setup_host.sh --undo first"
fi
created_vfs="$(state_get CREATED_VFS)"
hugepages_before="$(state_get HUGEPAGES_BEFORE)"

# save_state: record what setup changes, before each change is made, so --undo reverts it even
# when setup stops on a later error
save_state() {
    [ "$DRY_RUN" = 1 ] && return 0
    cat >"$STATE_FILE" <<EOF
# what setup_host.sh changed, reverted by --undo
PF_PORT=$PF_PORT
CREATED_VFS=${created_vfs:-0}
HUGEPAGES_BEFORE=$hugepages_before
EOF
}

numvfs="$(cat "/sys/bus/pci/devices/$PF_PORT/sriov_numvfs")"
if [ "$numvfs" = 0 ]; then
    [ -n "${MTL_ROOT:-}" ] || die "PF $PF_PORT has no VFs: set MTL_ROOT so nicctl.sh can create them"
    created_vfs=1
    save_state
    run sudo "$MTL_ROOT/script/nicctl.sh" create_vf "$PF_PORT" "$VF_COUNT"
else
    echo "PF $PF_PORT has $numvfs VFs, reusing them"
fi

mapfile -t vfs < <(vfs_of "$PF_PORT")
if [ "$DRY_RUN" = 1 ] && [ "${#vfs[@]}" = 0 ]; then
    vfs=("<vf0 of $PF_PORT>" "<vf1 of $PF_PORT>")
else
    [ "${#vfs[@]}" -ge 2 ] || die "PF $PF_PORT has ${#vfs[@]} VF, need 2; sriov_numvfs is left alone, change it yourself"
    for vf in "${vfs[@]:0:2}"; do
        drv="$(driver_of "$vf")"
        [ "$drv" = vfio-pci ] ||
            die "VF $vf is bound to '${drv:-no driver}', not vfio-pci: bind it ($MTL_ROOT/script/nicctl.sh bind_pmd $vf)"
    done
fi
TX_PORT="${vfs[0]}"
RX_PORT="${vfs[1]}"

current="$(hugepages_now)"
if [ "$current" -lt "$HUGEPAGES" ]; then
    [ -n "$hugepages_before" ] || hugepages_before="$current"
    save_state
    run sudo sysctl -w "vm.nr_hugepages=$HUGEPAGES"
    if [ "$DRY_RUN" = 0 ] && [ "$(hugepages_now)" -lt "$HUGEPAGES" ]; then
        die "only $(hugepages_now) hugepages could be allocated, need $HUGEPAGES"
    fi
else
    echo "$current hugepages, $HUGEPAGES needed"
fi

node="$(cat "/sys/bus/pci/devices/$PF_PORT/numa_node" 2>/dev/null)"
[ -n "$node" ] && [ "$node" -ge 0 ] || node=0
if [ "$DRY_RUN" = 1 ]; then
    echo "dry-run: would write $HOST_ENV (TX_PORT=$TX_PORT RX_PORT=$RX_PORT NUMA_NODE=$node)"
    exit 0
fi
unset TX_CPUS RX_CPUS
default_cpu_sets

cat >"$HOST_ENV" <<EOF
# written by setup_host.sh $(date -Iseconds)
PF_PORT=$PF_PORT
TX_PORT=$TX_PORT
RX_PORT=$RX_PORT
NUMA_NODE=$node
TX_CPUS=$TX_CPUS
RX_CPUS=$RX_CPUS
EOF
echo "wrote $HOST_ENV:"
grep -v '^#' "$HOST_ENV"
