#!/bin/bash
#
# Copyright(c) 2026 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#
# Shared by PerformanceTestSampleApp.sh / PerformanceTestGstreamer.sh /
# PerformanceTestFfmpegPlugin.sh: pin the CPU frequency governor to 'performance' for the
# duration of a perf run, and restore whatever it was before on exit. cpufreq governors
# (powersave/ondemand/schedutil) let the core clock drift or ramp lazily between runs, which
# produces exactly the run-to-run FPS swings a calibrated baseline can't tell apart from a real
# regression.
#
# The governor is a machine-wide resource, not a per-run one, and the three scripts above can run
# concurrently on the same host (see each script's own ramdisk comment) - so a plain
# save-on-start/restore-on-exit here would have one script's exit yank the governor out from
# under another script still mid-run. A refcount under a shared flock makes only the first
# concurrent caller set it and only the last one restore it.
#
# Usage: source this file, call acquire_performance_governor near the top of the script (after
# set -eo pipefail) and add release_performance_governor to the script's own EXIT trap.

GOV_LOCK_DIR="/tmp/.svt_jpegxs_perf_governor-$(id -u)"
GOV_LOCK_FILE="$GOV_LOCK_DIR/lock"
GOV_HOLDERS_FILE="$GOV_LOCK_DIR/holders"
GOV_SAVE_FILE="$GOV_LOCK_DIR/saved"
GOV_ACQUIRED=0

# GOV_HOLDERS_FILE holds one holder identity per line instead of a bare count: an EXIT trap is what decrements
# a plain refcount, and a force-killed process (timeout, cancellation, OOM) never runs its EXIT
# trap at all - SIGKILL can't be caught by any process, ever. A stale count would then wedge the
# governor on 'performance' forever, since nothing left alive could ever bring it back to zero.
# Pruning dead PIDs under the lock, on every acquire and release, means the next real caller
# always sees the true live-holder set and heals a leftover from a killed one automatically.
# Writes stdin to $1 via a temp file in the same directory followed by a rename rather than a
# direct redirect: `mv` on the same filesystem is a single rename syscall, so a reader (or a
# SIGKILL landing mid-write) only ever observes the previous complete content or the new complete
# content - never a file truncated partway through being written.
atomic_write_governor_state() {
    local target="$1" tmp="$1.tmp.$$"
    cat > "$tmp" && mv -f "$tmp" "$target"
}

# Holder identity is "pid:starttime", not a bare pid: the kernel recycles PIDs, and on a host
# that spawns as many subprocesses per run as these scripts do (every sudo/tee/cat/stat call is
# its own pid), a killed holder's old pid can end up reused by a completely unrelated process
# before anyone prunes it. `kill -0` alone would then see "a process with this pid exists" and
# wrongly treat that unrelated process as the original holder, wedging the governor exactly the
# way a bare-pid refcount already could. /proc/<pid>/stat field 22 (starttime, in clock ticks
# since boot) is fixed for the lifetime of one specific process and different for any other one,
# even a same-numbered successor - the same technique systemd/procps use to tell pids apart.
governor_holder_starttime() {
    local pid="$1" stat_line rest
    stat_line=$(cat "/proc/$pid/stat" 2>/dev/null) || return 1
    rest="${stat_line##*) }"
    [ "$rest" != "$stat_line" ] || return 1
    awk '{print $20}' <<< "$rest"
}

governor_holder_identity() {
    local pid="$1" start
    start=$(governor_holder_starttime "$pid") || return 1
    [ -n "$start" ] || return 1
    printf '%s:%s\n' "$pid" "$start"
}

governor_holder_alive() {
    local recorded="$1" pid start_recorded start_now
    pid="${recorded%%:*}"
    start_recorded="${recorded#*:}"
    [ -n "$pid" ] && [ -n "$start_recorded" ] && [ "$pid:$start_recorded" = "$recorded" ] || return 1
    start_now=$(governor_holder_starttime "$pid") || return 1
    [ "$start_now" = "$start_recorded" ]
}

prune_dead_governor_holders() {
    local self_id="$1" line
    GOV_LIVE_HOLDERS=""
    [ -f "$GOV_HOLDERS_FILE" ] || return 0
    while read -r line; do
        [ -n "$line" ] || continue
        [ "$line" = "$self_id" ] && continue
        governor_holder_alive "$line" && GOV_LIVE_HOLDERS="$GOV_LIVE_HOLDERS$line
"
    done < "$GOV_HOLDERS_FILE"
}

# Every pathname read back out of GOV_SAVE_FILE goes through this before it's ever handed to
# `sudo tee` as a write target. The directory check below keeps other local users from writing
# that file in the first place, but this is the check that holds even if that ever regresses: a
# forged pathname (say /etc/shadow) piped into `sudo tee` would let it overwrite anything as
# root, no shell-quoting trick required.
#
# A `case` glob is the wrong tool here: unlike pathname expansion, `*` in a case pattern matches
# `/` too, so .../cpu[0-9]*/... would accept a forged .../cpu0/../../../../tmp/x/cpufreq/... and
# hand sudo tee a target well outside /sys. The `=~` regex below anchors the whole string with
# ^...$ and restricts the cpu-number segment to [0-9]+ - digits only, nothing that can smuggle a
# `/` or a `..` component through it.
is_governor_sysfs_path() {
    [[ "$1" =~ ^/sys/devices/system/cpu/cpu[0-9]+/cpufreq/scaling_governor$ ]]
}

# Restores every core from GOV_SAVE_FILE and removes it. Only ever correct to call from the same
# acquire_performance_governor call that itself just captured/reused that save file to pin - never
# from a call that only joined an already-pinned session, since that would destroy the real first
# holder's only copy of the true original values out from under it.
unwind_governor_pin() {
    if [ -f "$GOV_SAVE_FILE" ]; then
        while read -r rf rg; do
            is_governor_sysfs_path "$rf" || continue
            printf '%s\n' "$rg" | sudo tee "$rf" > /dev/null 2>&1 ||
                echo "WARN: failed to unwind $rf back to '$rg'" >&2
        done < "$GOV_SAVE_FILE"
        rm -f "$GOV_SAVE_FILE"
    fi
}

acquire_performance_governor() {
    local gov_files=(/sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor)
    if [ ! -e "${gov_files[0]}" ]; then
        echo "INFO: no cpufreq scaling_governor sysfs interface found - skipping governor pinning"
        return 0
    fi
    if ! grep -qw performance /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors 2>/dev/null; then
        echo "INFO: 'performance' governor not available on this host - leaving governor as-is"
        return 0
    fi
    # No sudo here on purpose: this directory only ever needs to be read/written by the user
    # running these scripts (always the same CI account), never by root, so it can be private
    # instead of world-writable. A pre-existing directory at this path is only trusted once its
    # ownership, permissions and type are all verified - mkdir -p succeeding on it proves nothing
    # by itself, since a hostile local user could have planted it (or a symlink) first.
    mkdir -p "$GOV_LOCK_DIR" 2>/dev/null
    chmod 0700 "$GOV_LOCK_DIR" 2>/dev/null
    if [ ! -d "$GOV_LOCK_DIR" ] || [ -L "$GOV_LOCK_DIR" ] ||
        [ "$(stat -c '%u' "$GOV_LOCK_DIR" 2>/dev/null)" != "$(id -u)" ] ||
        [ "$(stat -c '%a' "$GOV_LOCK_DIR" 2>/dev/null)" != "700" ]; then
        echo "WARN: $GOV_LOCK_DIR is not a private directory this user owns - skipping governor pinning" >&2
        return 0
    fi
    local self_id
    # No sentinel fallback here: a made-up value like "$$:0" could never be recognized as alive
    # by governor_holder_alive re-deriving the real starttime later, so a value this call goes on
    # to write into GOV_HOLDERS_FILE under that sentinel would look permanently dead to every
    # other process checking it, not merely to this one - skip pinning instead of writing an
    # identity nothing can ever confirm as live.
    self_id=$(governor_holder_identity "$$") || {
        echo "WARN: could not determine this process's own identity - skipping governor pinning" >&2
        return 0
    }
    local was_first_holder=0
    (
        flock -x 200
        prune_dead_governor_holders "$self_id"
        if [ -z "$GOV_LIVE_HOLDERS" ]; then
            # No live holder left standing under the lock - either the true first caller, or the
            # only trace of a killed one. Either way this call now owns pinning it, but only
            # capture the pre-pin values if nothing already did: a killed holder's own save file
            # still holds the true original values, and re-capturing now would instead save
            # today's already-pinned 'performance' as if it were the original.
            if [ ! -f "$GOV_SAVE_FILE" ]; then
                # Written into a temp file and renamed into place only once every core's line is
                # in it: a SIGKILL partway through this loop must not leave a save file that
                # looks complete but is missing entries for the cores not yet reached, since a
                # later caller trusts an existing save file's presence as "capture already done".
                local save_tmp="$GOV_SAVE_FILE.tmp.$$" orig_gov
                : > "$save_tmp"
                for f in "${gov_files[@]}"; do
                    # `echo "$f $(cat "$f")"` would still exit 0 even when the inner `cat` fails -
                    # the substitution just expands empty and echo happily prints it - so a
                    # dropped read (a CPU policy disappearing between the glob above and this
                    # loop, say) would commit a blank governor into what every later caller trusts
                    # as a complete, restorable snapshot. Read and check first; abort rather than
                    # save a snapshot that can't actually restore that core.
                    orig_gov=$(cat "$f") || {
                        echo "WARN: failed to read $f while capturing original governors - not pinning for this run" >&2
                        rm -f "$save_tmp"
                        exit 1
                    }
                    [ -n "$orig_gov" ] || {
                        echo "WARN: $f read as empty while capturing original governors - not pinning for this run" >&2
                        rm -f "$save_tmp"
                        exit 1
                    }
                    printf '%s %s\n' "$f" "$orig_gov" >> "$save_tmp"
                done
                mv -f "$save_tmp" "$GOV_SAVE_FILE" || {
                    echo "WARN: failed to move the captured governor snapshot into place - not pinning for this run" >&2
                    rm -f "$save_tmp"
                    exit 1
                }
            fi
            local pin_failed=0
            for f in "${gov_files[@]}"; do
                # Piped into tee rather than interpolated into a `sh -c` string: the value only
                # ever needs to be written as data, never parsed as shell syntax.
                if ! printf '%s\n' performance | sudo tee "$f" > /dev/null 2>&1; then
                    echo "WARN: failed to set performance governor on $f" >&2
                    pin_failed=1
                    break
                fi
            done
            if [ "$pin_failed" -eq 1 ]; then
                # Unwind whatever this loop did manage to set before the failure, rather than
                # leaving some cores pinned and reporting the run as pinned anyway.
                unwind_governor_pin
                echo "WARN: governor pinning failed partway through - left as found, not pinning for this run" >&2
                exit 1
            fi
            echo "INFO: CPU governor set to 'performance' for this run (first live holder)"
            was_first_holder=1
        fi
        # Retried rather than a single attempt: a holder invisible to this bookkeeping is a
        # holder a concurrent acquire/release can't see either, and prune_dead_governor_holders
        # would then treat this run as not holding anything - a second script could conclude it
        # is the sole holder and restore the governor while this one is still using it, exactly
        # the cross-script yank the whole refcount design exists to prevent. A transient failure
        # (a momentary ENOSPC from another process's own tmp file, say) is worth a few attempts
        # before giving up.
        local holders_write_ok=0 attempt
        for attempt in 1 2 3; do
            printf '%s\n%s\n' "$GOV_LIVE_HOLDERS" "$self_id" | grep -v '^$' | atomic_write_governor_state "$GOV_HOLDERS_FILE" &&
                { holders_write_ok=1; break; }
            sleep 0.2
        done
        if [ "$holders_write_ok" -ne 1 ]; then
            echo "WARN: failed to record this run as a governor holder after retries - not pinning for this run" >&2
            # Only ours to unwind if this call is the one that actually pinned: if it only joined
            # a session another live process already holds, that process's save file is the only
            # copy of the true original values left, and destroying it here would strand it.
            [ "$was_first_holder" -eq 1 ] && unwind_governor_pin
            exit 1
        fi
        exit 0
    ) 200>"$GOV_LOCK_FILE"
    [ $? -eq 0 ] && GOV_ACQUIRED=1
    return 0
}

release_performance_governor() {
    [ "$GOV_ACQUIRED" = "1" ] || return 0
    local self_id
    # Unlike the same fallback in acquire, a mismatched value here is harmless: this self_id is
    # only ever used to exclude our own entry from GOV_LIVE_HOLDERS one line down, and a value
    # that fails to match falls through to governor_holder_alive on our own entry instead - which
    # would find /proc/$$ equally unreadable and conclude the same "not alive" either way. It's
    # only ever written to the file (where a permanently-unmatchable value would actually cause
    # harm) from acquire, never from here.
    self_id=$(governor_holder_identity "$$") || self_id="$$:0"
    (
        flock -x 200
        prune_dead_governor_holders "$self_id"
        if [ -z "$GOV_LIVE_HOLDERS" ]; then
            if [ -f "$GOV_SAVE_FILE" ]; then
                while read -r f orig_gov; do
                    # Two independent checks on data that ultimately came out of a file: the
                    # value is piped into tee rather than interpolated into a `sh -c` string, and
                    # the path is checked against the real cpufreq layout before being trusted as
                    # a sudo write target - either one alone stops a forged line here from making
                    # this overwrite an arbitrary file as root.
                    is_governor_sysfs_path "$f" || continue
                    printf '%s\n' "$orig_gov" | sudo tee "$f" > /dev/null 2>&1 ||
                        echo "WARN: failed to restore $f back to '$orig_gov'" >&2
                done < "$GOV_SAVE_FILE"
                rm -f "$GOV_SAVE_FILE"
                echo "INFO: CPU governor restored to its original state (last live holder)"
            fi
            rm -f "$GOV_HOLDERS_FILE"
        else
            printf '%s' "$GOV_LIVE_HOLDERS" | atomic_write_governor_state "$GOV_HOLDERS_FILE"
        fi
    ) 200>"$GOV_LOCK_FILE"
    return 0
}
