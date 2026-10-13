# Self-hosted runner requirements

Some CI jobs in this repository run on self-hosted GitHub Actions runners rather than
GitHub-hosted ones. Those runners are treated as **pre-provisioned**: their jobs do not run
`apt-get install`, they only verify that the expected tooling is on `PATH` and fail fast if it is
not.

This document states what a runner must provide for those jobs to pass. It is a contract for
whoever provisions a runner, and for anyone adding a job that targets one — it deliberately does
not describe any particular deployment.

## Labels

| `runs-on` | Used by |
| -- | -- |
| `['self-hosted', 'linux', 'x64', 'jpeg-xs']` | Linux library build, unit, conformance, fuzzing, OOMify, sanitizer, GStreamer plugin build/test and FFmpeg plugin test jobs |
| `['self-hosted', 'linux', 'x64', 'performance']` | Performance jobs only (`base_build.yaml` `performance-tests`, `gstreamer_plugin_build.yaml` `gstreamer-performance-tests`, `ffmpeg_plugin_build.yaml` `ffmpeg-9-0-performance-tests`) |

The FFmpeg plugin's source fetch, library and Linux build jobs, the `changes` path-filter jobs, the sanitizer `gate` job
and all Windows jobs run on GitHub-hosted runners.

Several runners may share the `jpeg-xs` label, and a job is dispatched to whichever one is idle.
Jobs must therefore target the shared label and never a label identifying one individual runner.

Performance jobs use a separate label, but a label only controls which runner a job is
dispatched to; it does not stop a `performance` runner and a `jpeg-xs` runner from executing
jobs at the same time. Hardware exclusivity is therefore a provisioning requirement: a
`performance` runner must not share a host, or at least a processor socket and its memory node,
with any `jpeg-xs` runner or other workload, so that measurements are never taken alongside a
build.

## Required packages

```sh
sudo apt-get update
sudo apt-get -y install \
    bison clang cmake flex g++ gcc git libglib2.0-dev llvm make nasm \
    ninja-build numactl pkg-config python3 python3-venv valgrind
```

`git` is used by the GStreamer build job (to fetch GStreamer) and the OOMify job (to fetch oomify), and `make` by the
OOMify job to build oomify.

Self-hosted jobs assert the resulting binaries are present with a `Verify host toolchain` step:

```sh
for t in cmake nasm make gcc g++ pkg-config ninja valgrind numactl flex bison clang; do ...
```

The GStreamer build job additionally checks for `git`, `python3`, a working `python3 -m venv`
(`python3-venv`) and `pkg-config --exists glib-2.0` (`libglib2.0-dev`).

If that step reports a missing tool, install the corresponding package on the runner rather than
adding an `apt-get` step back into the workflow. GitHub-hosted jobs (`ubuntu-*`, `windows-*`) are
ephemeral and keep their own install steps; self-hosted jobs do not.

## Privileges

The runner's service account must be able to run the following through `sudo` without a
password prompt; a prompt blocks the job until it times out. Grant only these commands (for
example with a dedicated `sudoers.d` drop-in) rather than blanket `NOPASSWD: ALL`:

- `chown` and `chmod` on paths under the runner's own `_work` directory, used by the unit-test,
  conformance, fuzzing, OOMify, sanitizer and plugin jobs, including the GStreamer and FFmpeg
  performance jobs on `performance` runners, to repair workspace ownership and permissions.
- On `performance` runners only, for the per-run ramdisk the performance scripts create under
  `/mnt/svt_jpegxs_perf_*ramdisk_*`:
  - `mkdir -p <dir>` and `rmdir <dir>`
  - `mount -t tmpfs -o size=3G,mode=1777,mpol=bind:<N> tmpfs <dir>`, where `<N>` is the NUMA
    node selected by `tests/scripts/perf_numa.sh`
  - `umount <dir>` and `umount -l <dir>`
- On `performance` runners only: `tee` to
  `/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor` to pin the CPU frequency governor for
  the duration of a run.

Sudoers rules that restrict arguments must match these argument lists; `,`, `:`, `=` and `\` in
command arguments must be escaped with `\`. For example:

```text
<runner-user> ALL=(root) NOPASSWD: /usr/bin/mount -t tmpfs -o size\=3G\,mode\=1777\,mpol\=bind\:* tmpfs /mnt/svt_jpegxs_perf_*
<runner-user> ALL=(root) NOPASSWD: /usr/bin/umount /mnt/svt_jpegxs_perf_*, /usr/bin/umount -l /mnt/svt_jpegxs_perf_*
```

## Network access

Besides GitHub itself (checkout, artifacts, actions), self-hosted jobs fetch from:

- `pypi.org` / `files.pythonhosted.org`: `.github/scripts/build_gstreamer_plugin.sh` installs a
  pinned Meson with `pip` into a venv.
- `gitlab.freedesktop.org`: `.github/scripts/build_gstreamer_plugin.sh` fetches the pinned
  GStreamer commit.
- `github.com`: the OOMify job fetches the pinned oomify commit from
  `github.com/tavianator/oomify`.

## Test assets

The conformance, functional and performance suites read their inputs from `/opt/samples`, which
must exist on every runner, be readable by the runner's service account, and contain:

```text
/opt/samples/test_bitsreams/
/opt/samples/reference_decode/
/opt/samples/bitstream_multi_frames/
/opt/samples/bitstream_invalid/
/opt/samples/encoder_tests/
```

## CPU and memory placement

Where a runner is pinned to a NUMA node, that pinning is applied once by the host configuration
(for example in the runner's service unit), **not** by the workflows.

Jobs inherit that placement and must not try to re-assert or leave it. Note that plain CPU
affinity and NUMA memory policy (`taskset`, `numactl`, or systemd `CPUAffinity=` /
`NUMAPolicy=`) are only defaults: any process can widen its own affinity or policy again, up to
whatever its cpuset cgroup permits. Only cpuset constraints are enforced. A runner that needs
real isolation must therefore be confined with a cpuset, for example systemd `AllowedCPUs=` and
`AllowedMemoryNodes=` in its service unit.

[tests/scripts/perf_numa.sh](../../tests/scripts/perf_numa.sh) stays within the inherited
placement either way: it reads `Cpus_allowed_list` and `Mems_allowed_list` from
`/proc/self/status`, picks a node that is memory-eligible and holds at least one allowed CPU, and
narrows to one logical CPU per physical core of that node so that two worker threads never share
a core's execution units.

## Workspace isolation

Every runner has its own `_work` directory, so `GITHUB_WORKSPACE` differs per runner even when
runners share a host. A job must not assume a workspace prepared by another job: everything it
needs has to come from its own checkout or from an uploaded artifact.

For the same reason, per-job scratch space must use `${{ runner.temp }}` rather than a fixed path
under `/tmp`, which concurrent jobs on a shared host would race over.

## Housekeeping

Self-hosted runners keep their workspaces between jobs, and the GStreamer and FFmpeg jobs check
out large source trees. Prune `_work` and rotate `_diag` periodically while no job is running, and
monitor free space — a runner that fills its disk fails builds midway with errors that look
unrelated to the real cause.
