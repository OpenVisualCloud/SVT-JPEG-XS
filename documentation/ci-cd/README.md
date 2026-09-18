# CI/CD

This document describes the GitHub Actions workflows in `.github/workflows/` and how they fit
together, in particular how the core library build is shared across workflows to avoid redundant
rebuilds.

## Workflows

| Workflow file | Name | Trigger | Purpose |
| -- | -- | -- | -- |
| `base_build.yaml` | Base Build | `push` (main), `pull_request` | Builds the core library (Linux gcc + Windows MSVC), runs unit/conformance/performance tests, and calls the 4 reusable workflows below as jobs. |
| `ffmpeg_plugin_build.yaml` | ffmpeg Plugin Build | `workflow_call` (from Base Build), `workflow_dispatch` | Builds and tests the ffmpeg plugin against 6 ffmpeg versions (6.1, 7.0, 7.1, 8.0, 8.1, 9.0), on Linux and Windows. |
| `gstreamer_plugin_build.yaml` | GStreamer Plugin Build | `workflow_call` (from Base Build), `workflow_dispatch` | Builds and tests the GStreamer plugin. |
| `fuzzy-tests.yaml` | Fuzzy Tests | `workflow_call` (from Base Build), `workflow_dispatch` | Runs libFuzzer-based encoder/decoder fuzzing. |
| `oomify_tests.yaml` | OOMify Tests | `workflow_call` (from Base Build), `workflow_dispatch` | Injects a `malloc()` failure at every allocation point in the encoder and decoder and verifies each failure exits cleanly rather than crashing. Covers 7 codec configurations (yuv422/yuv444/yuv420, 8-bit/10-bit, production decomposition, vertical prediction). |
| `coverity.yaml` | Coverity Build | scheduled, `workflow_dispatch` | Nightly Coverity static analysis scan. |
| `linter.yaml` | - | `push`, `pull_request` | super-linter checks (bash, markdown, etc.). |

## Why `workflow_call` instead of `workflow_run`

`ffmpeg_plugin_build.yaml`, `gstreamer_plugin_build.yaml`, `fuzzy-tests.yaml` and `oomify_tests.yaml`
are reusable workflows (`on: workflow_call`) invoked as jobs directly from `base_build.yaml`
(`needs: linux-build`, `uses: ./.github/workflows/<file>.yaml`), rather than triggering on
`workflow_run` after `Base Build` completes. This still lets them download and reuse the core
library artifact `Base Build` already produced (falling back to building it locally if
unavailable), but keeps them part of the same workflow run as `Base Build` itself.

This matters for two reasons:

- `workflow_run`-triggered check runs don't show up in a PR's checks table (only in the Actions
  tab), because they're a separate event/run from the `pull_request` event GitHub uses to populate
  that table. As jobs of the same run, they now appear as normal PR checks.
- `workflow_run`-triggered workflows always execute using the workflow YAML committed on the
  **default branch**, never the triggering PR's version - a PR changing one of these 4 files
  couldn't exercise its own change through its own checks. Reusable workflows invoked via
  `workflow_call` use the calling ref's version, so this limitation goes away too.

`ffmpeg_plugin_build.yaml`, `gstreamer_plugin_build.yaml` and `oomify_tests.yaml` each still have
their own `changes`/path-filter job; `fuzzy-tests.yaml` has no path filter of its own and always
runs. All 4 keep their own `workflow_dispatch` trigger for standalone manual runs. The call site in
`base_build.yaml` only blocks them if `linux-build` or one of the Linux unit/conformance jobs that
validate its output actually failed - not if `linux-build` was merely skipped by `Base Build`'s own
path filter, so `ffmpeg_plugin_build`/`gstreamer_plugin_build`/`oomify_tests` still decide
independently via their own path filter whether they need to run.

## Shared build artifacts

The core library used to be rebuilt from scratch independently by every workflow/job that needed
it. It is now built at most 3 times per run and reused everywhere else:

| Producer | Toolchain | Consumed by |
| -- | -- | -- |
| `base_build.yaml` `linux-build` | gcc | `ffmpeg_plugin_build.yaml` `linux-lib-build`, `gstreamer_plugin_build.yaml`, `fuzzy-tests.yaml` `build-lib`, `oomify_tests.yaml` (all fall back to a local build if the artifact is unavailable) |
| `base_build.yaml` `windows-build` | MSVC | native Windows apps/tests only - not consumed by ffmpeg (different toolchain, see below) |
| `ffmpeg_plugin_build.yaml` `windows-mingw-lib-build` | MinGW (static) | all 6 `ffmpeg-*-windows-build` jobs |

`linux-lib-build`, `gstreamer_plugin_build.yaml`'s build job, and `fuzzy-tests.yaml`'s `build-lib`
job each try to download `base_build`'s installed tree first (`continue-on-error: true` on the
download step) and only fall back to building locally if it is missing - for example if
`Base Build`'s own path filter did not match for that commit.

### Why Windows needs two toolchains

`base_build.yaml`'s `windows-build` uses MSVC. The ffmpeg plugin on Windows is built through
MSYS2/MinGW, because that is FFmpeg's own supported way of building on Windows (`./configure` is a
POSIX shell script). A MinGW-built static library cannot be linked into an MSVC-built ffmpeg
binary (different C runtime/ABI), so `windows-mingw-lib-build` builds the core library once with
MinGW, reused by all 6 `ffmpeg-*-windows-build` jobs, instead of doing that 6 times.

### FFmpeg upstream source is also fetched only once

`ffmpeg_plugin_build.yaml`'s `ffmpeg-source-fetch` job fetches all 6 FFmpeg release branches
(shallow, one `git fetch` per branch) into a single repository and uploads it as one artifact. All
12 `ffmpeg-*-linux-build`/`ffmpeg-*-windows-build` jobs download that artifact and just
`git checkout release/X.Y` locally, instead of each cloning FFmpeg over the network. Falls back to
the original clone-with-retry logic if the pre-fetched artifact is unavailable.

## Path filters

`base_build`, `ffmpeg_plugin_build` and `gstreamer_plugin_build` each have a `changes` job using
`dorny/paths-filter` against `.github/config/path_filters.yaml` to skip work when nothing relevant
changed. `ffmpeg_plugin_build`/`gstreamer_plugin_build`'s filters include `Source/Lib/**` (the
actual codec) as well as their own plugin-specific paths, so a core codec change always exercises
the plugin integration tests too.

## Windows CI performance notes

The 7 Windows/MinGW jobs (`windows-mingw-lib-build` + 6 `ffmpeg-*-windows-build`) each:

- Cache the MSYS2/pacman package install (`cache: true` on `msys2/setup-msys2`) instead of
  re-downloading the toolchain every run.
- Disable Windows Defender real-time scanning before building, since many-small-file MinGW builds
  are heavily penalized by real-time AV scanning on GitHub-hosted Windows runners.

## Known gotchas

- `Build/linux/build.sh`'s `build()` always does a clean rebuild (`rm -rf $build_type`). Adding an
  `install` step after a build must be part of the *same* `build.sh` invocation
  (`./build.sh --all --test install --prefix X`), not a separate one, or it silently triggers a
  full extra rebuild before installing.
- `Build/linux/build.sh`'s `install_build()` probes for `sudo` via a helper that returns a
  non-zero "not found" sentinel by design. Because `set -e` is active, that sentinel used to abort
  the whole install step whenever `sudo` was genuinely absent (e.g. Windows/MSYS2) - fixed with
  `sudo=$(check_executable -p sudo) || true`.
