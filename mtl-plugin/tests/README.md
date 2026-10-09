# mtl-plugin loopback tests

Scripts that check the SVT-JPEG-XS MTL plugin end to end: a Media Transport Library (MTL) TX
session encodes through the plugin, sends ST 2110-22 over a VF of the NIC, a second VF receives
it, and an RX session decodes it through the plugin again. They are not part of CI; run them on a
host with an SR-IOV NIC after changing the plugin.

No test content is stored in the repository: every case generates its pictures into a temp dir
(`/tmp/mtl-plugin-test.*`), removed when the script ends.

## Prerequisites

* MTL 26.09 or newer, built from source (`$MTL_ROOT/build.sh`): the tests use
  `build/app/TxSt22PipelineSample`, `build/app/RxSt22PipelineSample` and
  `tests/tools/RxTxApp/build/RxTxApp` from that tree.
* SVT-JPEG-XS installed and the plugin built (`mtl-plugin/build.sh`, or `./build.sh build-only`).
* Two VFs of one PF bound to vfio-pci, and hugepages (`setup_host.sh` can do both).
* `sudo` (the MTL processes run as root), `cc`, `cmp`, `taskset`, `fuser`, `pkg-config`.
* For `test_perf.sh`: `bpftrace` (`apt install bpftrace`), and MTL built with its USDT probes
  (meson option `enable_usdt`, on by default). The test stops if either is missing.

## Quick start

```bash
cd <jpeg-xs-repo>/mtl-plugin/tests
export MTL_ROOT=<mtl-repo>
PF_PORT=0000:15:00.0 ./run_all.sh
```

With `PF_PORT` set, `run_all.sh` first runs `setup_host.sh`, which finds or creates two VFs and
writes `.host.env`. Later runs read the ports from `.host.env`, so `PF_PORT` can be left out. Or
skip the setup and name the VFs yourself:

```bash
TX_PORT=0000:15:01.0 RX_PORT=0000:15:01.1 ./run_all.sh
```

`TESTS="config formats"` runs a subset; each `test_*.sh` can also be run on its own.

## Which plugin and SVT-JPEG-XS library are tested

No `kahawai.json` needs editing: every test writes its own into the temp dir, listing only the
plugin under test (`PLUGIN_SO`, and `PLUGIN_SO_B` for the second perf variant), and hands it to
the MTL processes with `KAHAWAI_CFG_PATH`. The `kahawai.json` of the MTL tree and
`mtl-plugin/kahawai.json` are not used, so no other ST 2110-22 plugin can take the sessions,
and the plugin doesn't have to be installed.

The plugin finds `libSvtJpegxs` like any shared library: a plugin in its meson build dir through
the runpath meson gives it (the library it was built against, as pkg-config reported it), an
installed plugin through the system library path. Every test prints the library the plugin
loads, and `env.txt` records it with its md5.

To test a library that is built but not installed, install it into a private directory and
point `SVT_PREFIX` there; no root is needed:

```bash
cmake --install <svt-jpeg-xs-build-dir> --prefix /tmp/svtjxs
SVT_PREFIX=/tmp/svtjxs ./run_all.sh
```

The tests look for `SvtJpegxs.pc` anywhere below `SVT_PREFIX` and build the plugin against it
into `<results dir>/plugin_build`, once for all tests of a run, so headers, feature checks and
the library match. They stop if the plugin, the config harness or the codec apps load a
`libSvtJpegxs` from outside `SVT_PREFIX`. `CODEC_TIER=1` uses the apps installed with the
library (`bin/` of the install prefix `SvtJpegxs.pc` names) and runs them with that library.

A library build tree can't be used directly: its `SvtJpegxs.pc` only gives correct paths from
its installed location. Swapping in a `.so` the plugin wasn't built against is not supported
either: when the library's parameter structs differ, config values silently land in the wrong
fields.

## Environment

All settings are environment variables, set on the command line or exported before running a
script, for example `MTL_ROOT=~/mtl TEST_SECONDS=5 ./run_all.sh`. Only `MTL_ROOT` and the ports
are needed; everything else has a default that suits a 1080p test on one host.

### Required

| Variable | Default | Description |
| --- | --- | --- |
| `MTL_ROOT` | none | Path to a Media Transport Library source checkout in which `./build.sh` has been run. The tests start the MTL sample apps and RxTxApp from its `build/` and `tests/tools/RxTxApp/build/` directories; `setup_host.sh` uses its `script/nicctl.sh` to create VFs. Not needed for `test_config.sh`. |
| `TX_PORT`, `RX_PORT` | from `.host.env` | PCI addresses of the two network ports the loopback runs over, e.g. `0000:15:01.0` and `0000:15:01.1`: the TX side sends from `TX_PORT`, the RX side receives on `RX_PORT`. Normally two VFs of the same PF, so the traffic stays inside the NIC. Both must be bound to `vfio-pci` (`dpdk-devbind.py -s` lists ports and drivers) and not used by another application. `setup_host.sh` finds or creates them and writes them to `.host.env`, so they only have to be set when you don't use it. |

### Ports and network

| Variable | Default | Description |
| --- | --- | --- |
| `PF_PORT` | none | PCI address of a physical NIC port (PF) with SR-IOV, e.g. `0000:15:00.0` (`lspci \| grep Ethernet`). When set, `run_all.sh` runs `setup_host.sh`, which takes `TX_PORT` and `RX_PORT` from the VFs of this PF. |
| `TX_IP`, `RX_IP` | `192.168.17.101`, `192.168.17.102` | IPv4 addresses MTL gives the TX and the RX port for the test; the stream is sent unicast from `TX_IP` to `RX_IP`. Change them only if these addresses are already used on the network of the PF. |

### Plugin and library under test

| Variable | Default | Description |
| --- | --- | --- |
| `PLUGIN_SO` | `mtl-plugin/build/libst_plugin_st22_svt_jpeg_xs.so` | The plugin library that is tested. The default is the output of `mtl-plugin/build.sh`; point it at another build to test that one. Can't be combined with `SVT_PREFIX`. |
| `PLUGIN_BUILD_DIR` | `mtl-plugin/build` | Meson build directory of the plugin. `test_config.sh` compiles the config loader with the flags recorded there (`compile_commands.json`), and `PLUGIN_SO` defaults to the plugin in it. Set automatically with `SVT_PREFIX`. |
| `PLUGIN_SO_B` | none | A second plugin build, only for `test_perf.sh`: every perf run is done once with `PLUGIN_SO` (variant a) and once with `PLUGIN_SO_B` (variant b), alternating, so the two can be compared under the same host conditions. |
| `SVT_PREFIX` | none | Directory an SVT-JPEG-XS build was installed into with `cmake --install <build> --prefix <dir>`. The tests then build the plugin against that library and fail if anything loads a different `libSvtJpegxs`; see [Which plugin and SVT-JPEG-XS library are tested](#which-plugin-and-svt-jpeg-xs-library-are-tested). |

### Test content and duration

| Variable | Default | Description |
| --- | --- | --- |
| `TEST_WIDTH`, `TEST_HEIGHT` | `1920`, `1080` | Picture size in pixels of every stream; both must be even. The lossy packet budget is computed from it. |
| `TEST_FPS` | `p59` | Frame rate of every stream, in RxTxApp notation: `p23` (23.98), `p24`, `p25`, `p29` (29.97), `p30`, `p50`, `p59` (59.94), `p60`, `p100`, `p119` (119.88), `p120`. |
| `TEST_SECONDS` | `10` | How many seconds the TX side runs in each loopback case of `test_formats.sh` and `test_interlaced.sh`, at least 5. The RX side starts 4 s earlier and stops about 2 s later, so one case takes about `TEST_SECONDS` + 6 s: a full `test_formats.sh` (25 loopbacks and 2 RxTxApp runs) takes about 7 min with the default, about 5 min with `TEST_SECONDS=5`. Starting MTL takes about 2 s of that time; a case fails if TX sends less than half the frames the remaining time at `TEST_FPS` allows. |
| `SOURCE_YUV` | none | A raw picture file to derive the test content from instead of the generated gradients: planar 4:2:2 10-bit little-endian (ffmpeg `yuv422p10le`), `TEST_WIDTH` x `TEST_HEIGHT`, only the first frame is used, e.g. `ffmpeg -i in.mkv -frames:v 1 -pix_fmt yuv422p10le -f rawvideo src.yuv`. Lossless content must compress to less than the raw size (half of it for interlaced), so noisy pictures can make the lossless cases fail. |

### Performance test

These only affect `test_perf.sh`.

| Variable | Default | Description |
| --- | --- | --- |
| `RUNS` | `10` | Number of perf runs per plugin variant (and of codec-only runs with `CODEC_TIER=1`). The summary drops the fastest and the slowest run when there are at least 3. |
| `PERF_SECONDS` | `15` | How many seconds RX measures in each perf run (RxTxApp `--test_time`). One run takes about `PERF_SECONDS` + 7 s, so 10 runs about 4 min. |
| `PERF_MODE` | `split` | `split`: TX and RX run as two RxTxApp processes, each pinned to its own CPU set (`TX_CPUS`, `RX_CPUS`), like two separate devices. `single`: one RxTxApp process runs both, pinned to both CPU sets; useful to cross-check the latency. |
| `PERF_CONFIG` | none | Path to a plugin config file (see `mtl-plugin/sample_config.json`) used by both sides in the perf runs, e.g. to measure a setting. Without it the plugin runs with its built-in defaults. |
| `CODEC_TIER` | `0` | `1` adds codec-only runs: `SvtJpegxsEncApp` encodes and `SvtJpegxsDecApp` decodes the same content without MTL, pinned to `TX_CPUS`, to tell codec speed from pipeline effects. |
| `CODEC_FRAMES` | `60` | Frames per codec-only run. |
| `ENC_APP`, `DEC_APP` | from `PATH`, or the ones installed in `SVT_PREFIX` | Paths of `SvtJpegxsEncApp` and `SvtJpegxsDecApp` for `CODEC_TIER=1`. |

### CPU pinning

| Variable | Default | Description |
| --- | --- | --- |
| `TX_CPUS`, `RX_CPUS` | 2 x 8 cores on the NIC's NUMA node | CPUs the TX and the RX processes are pinned to with `taskset`, as a list with ranges, e.g. `TX_CPUS=1-4 RX_CPUS=5-8`. The two sets must not overlap, and each needs at least 3 CPUs: MTL runs its packet threads on the first two, the encoder and decoder threads use the others (4 or more is better). Without them the tests pick 16 physical cores (one hardware thread per core, CPU 0 left out) on the NUMA node of `TX_PORT`; with only one of them set, the other gets 8 of the remaining cores of that node. `setup_host.sh` stores its choice in `.host.env`. |

### Results and temporary files

| Variable | Default | Description |
| --- | --- | --- |
| `OUT_DIR` | `mtl-plugin/tests/results/<date_time>` | Where logs and results are written (see [Results](#results)). `run_all.sh` passes one directory to all tests of a run. |
| `KEEP_FILES` | `0` | `1` keeps the temp dir (`/tmp/mtl-plugin-test.*`) with the generated pictures, config files and what RX received, to inspect a failure; its path is printed at the end. |
| `TESTS` | `config formats interlaced perf` | `run_all.sh` only: which tests to run, in this order. |

### Host setup

These only affect `setup_host.sh` (see [Host setup and undo](#host-setup-and-undo)).

| Variable | Default | Description |
| --- | --- | --- |
| `VF_COUNT` | `2` | Number of VFs to create on `PF_PORT`, only used when the PF has none yet. At least 2. |
| `HUGEPAGES` | `2048` | Minimum number of 2 MiB hugepages (2048 = 4 GiB) MTL needs; setup raises `vm.nr_hugepages` to it if lower. |

## Tests

### test_config.sh

The plugin's config file loader, without MTL. Builds `tools/config_test.c` together with
`src/st22_svt_jpeg_xs_config.c`, using the defines of the plugin build, and checks: no config
file, `sample_config.json` (all keys at the library defaults), every key with a valid value,
numbers instead of names, the library verbosity per `log_level`, comments, unknown keys, invalid
values, syntax errors with their line number, the 1 MiB size limit, and a key the installed
library doesn't support (the harness rebuilt without `HAVE_ENC_LOSSLESS_ENABLE`).

### test_formats.sh

Progressive loopback with the MTL pipeline samples, for every plugin format:

* lossless (`encoder.lossless`): every received frame is bit-exact with the source;
* `GBRPLANAR10LE` / `12LE` decoded as `YUV444PLANAR*`: output planes are R, G, B (source planes
  2, 0, 1), and GBR with `rct` stays lossless;
* lossy (the samples' QUALITY mode, 3 bpp): RX gets the frames TX encoded, the logs have no
  errors, and every frame fits the codestream budget in packets;
* `YUV422PLANAR16LE`: the same picture sent as 10LE and as 16LE (10LE << 6) decodes to the same
  samples (16LE >> 6, low 6 bits 0);
* rejected combinations: a 4:2:2 stream into a 4:4:4 receiver, a 12-bit stream into a 16LE
  receiver, lossless with 16LE, and 4:2:0 with `decomp_v` 0 (the samples with a config file,
  RxTxApp in SPEED mode), which passes with `decomp_v` 1.

The 16LE cases are skipped when the plugin doesn't offer the format (built against a library
without MSB-aligned input/output). The tests read that from the encoder caps MTL logs when the
plugin registers.

### test_interlaced.sh

Interlaced loopback (`--interlaced`). The plugin codes each field as its own picture:

* lossless for `YUV422PLANAR10LE`, `YUV420PLANAR8`, `YUV444PLANAR10LE` and `GBRPLANAR10LE`: each
  received field is bit-exact with one of the two source fields, and both fields arrive;
* lossy: packets per field are 45-55% of the packets per progressive frame.

### test_perf.sh

RxTxApp loopback of `YUV422PLANAR10LE` in SPEED mode, `RUNS` times. TX and RX run as two
RxTxApp processes, each pinned with `taskset` to its own CPU set (`PERF_MODE=single`: one
process). The CPU frequency governor is set to `performance` for the run
(`tests/scripts/perf_governor.sh`). Every run prints:

```text
   run  1/3  variant a  OK        rx  59.976 fps  rx latency  19.536 ms  838 frames
      stage avg ms  tx: queue   0.13  encode   4.90  wait  10.52  send  16.58
                    rx: receive  13.63  queue   0.14  decode   3.29  handover   0.14  total  49.32
```

* `rx ... fps`: decoded frames the RX application got per second. MTL sends at the stream's frame
  rate (`TEST_FPS`), so this can't exceed it; `OK` means within 5% of it, i.e. the pipeline keeps
  up in real time. It doesn't show how fast the codec could go.
* `rx latency`: RxTxApp's own measurement, the time the RX application gets the decoded frame
  minus the frame's RTP timestamp. TX sets the RTP timestamp after encoding, to the time the
  frame's transmit slot starts, so it covers sending, receiving and decoding but not encoding.
  Both ends use the host clock, so it is valid across the two processes (`PERF_MODE=single`
  gives the same within about 0.2 ms).
* stage times: `bpftrace` (`tools/st22p_stages.bt`) times every step of every frame with MTL's
  st22p probes, in both processes; a TX frame is matched to its RX frame by the RTP timestamp.
  The first 60 frames of each run are left out.

| Stage | From | To |
| --- | --- | --- |
| tx queue | TX application hands over the raw frame | encoder takes it |
| tx encode | encoder takes the frame | codestream done |
| tx wait | codestream done | MTL transport takes it |
| tx send | transport takes the codestream | NIC has read all its packets; includes waiting for the frame's transmit slot |
| rx receive | NIC has read the packets | RX has the whole codestream; with paced sending mostly the transmission itself |
| rx queue | codestream complete | decoder takes it |
| rx decode | decoder takes the codestream | frame decoded |
| rx handover | frame decoded | RX application gets it |
| total | TX application hands over the raw frame | RX application gets the decoded frame |

The stages add up to `total`. The encoder and decoder stages are the time a frame spends in the
codec, waiting for the previous one included; `CODEC_TIER=1` measures the codec alone, at full
speed. The "tx send" / "rx receive" boundary is when MTL frees the frame's packet buffers, which
depends on the NIC and driver; their sum is the time from the transport taking the codestream to
RX having all of it.

The summary gives fps and latency as the mean over the runs without the lowest and the highest
value, and per stage the same mean of the run averages plus the slowest single frame
(`perf/stages.txt`); `perf/perf.csv` has every run, `perf/run<n>_<variant>.stages.log` the raw
bpftrace output.

There is no stored baseline: compare a change by running both builds interleaved,
`PLUGIN_SO_B=<other build>/libst_plugin_st22_svt_jpeg_xs.so ./test_perf.sh`.

## Results

On the console every test starts with what it checks, how long it takes, and the plugin and
`libSvtJpegxs` it uses, then prints one line per case as it finishes, `PASS`, `FAIL` or `SKIP`
with the reason (shortened to the terminal width). `run_all.sh` ends with a summary, for example:

```text
== summary
   test          passed  failed  skipped    time
   config            29       0        0    0:00
   formats           28       0        0    7:02
   interlaced         5       0        0    1:38
   perf               1       0        0    4:21
   total             63       0        0   13:01

   perf pipeline_a fps 59.954 (min 59.924 max 59.983), latency ms 19.460 (min 19.381 max 19.623), 10/10 OK

   pipeline stages, variant a: ms per frame, mean of the run averages and the slowest frame
     stage             avg      max
     tx_queue        0.127    0.295  raw frame waits for the encoder
     tx_encode       4.787    6.278  encode
     tx_wait        10.678   12.496  codestream waits for the transport
     tx_send        16.580   16.739  wait for the transmit slot, NIC reads the packets
     rx_receive     13.628   13.823  paced transmission until the codestream is complete at RX
     rx_queue        0.141    0.308  codestream waits for the decoder
     rx_decode       3.338    3.684  decode
     rx_handover     0.063   50.059  decoded frame waits for the RX app
     total          49.322   49.889  raw frame in at TX to decoded frame out at RX

   failed cases: none
   skipped cases: none
   details  /tmp/TEST_000/summary.txt, logs per test in /tmp/TEST_000/<test>/

RESULT: PASS (63 cases)
```

Failed and skipped cases are listed there with their reason. The results dir
(`results/<date_time>/` or `OUT_DIR`) holds:

* `summary.txt`: one line per case with the full reason;
* `env.txt`: plugin and library md5, MTL version and commit, ports, CPU sets, governor;
* `<test>/`: the TX and RX log of every case, named after the case;
* `perf/perf.csv` (one line per run, with the stage times), `perf/perf_summary.txt`,
  `perf/stages.txt`, `perf/run<n>_<variant>.stages.log` (bpftrace), `perf/governor.log`, and
  with `CODEC_TIER=1` `perf/codec.csv`.

Every script exits with the number of failed cases, or 125 when it can't run at all (missing
`MTL_ROOT`, ports, plugin).

## Known limits

* MTL logs `st22_encoder_get_frame(0), invalid type 0` (also for the decoder) at the start of
  some sessions: the plugin asks for a frame before MTL has finished creating the session. It is
  harmless and not counted as an error.
* MTL halves an interlaced receiver's codestream buffer twice, so a lossless field must compress
  to half its raw size. The generated content compresses about 5 times; a `SOURCE_YUV` with
  noisy content can lose fields in the lossless interlaced cases.
* The receivers keep the default codestream buffer (raw frame size), so lossless content must
  compress at all; the same holds for `SOURCE_YUV`.
* The lossy packet budget assumes MTL's ST 2110-22 payload of 1280 bytes per packet
  (`ST22_PKT_PAYLOAD` in `common.sh`).

## Host setup and undo

```bash
PF_PORT=0000:15:00.0 ./setup_host.sh --dry-run   # show what would change
PF_PORT=0000:15:00.0 ./setup_host.sh             # set up, or reuse what is there
./setup_host.sh --check                          # .host.env still valid?
./setup_host.sh --undo --dry-run
./setup_host.sh --undo
```

Setup reuses the first two VFs of the PF when they are bound to vfio-pci. It creates VFs (with
`$MTL_ROOT/script/nicctl.sh create_vf`) only when the PF has none, and stops otherwise rather than
rewrite `sriov_numvfs`. It raises `vm.nr_hugepages` to `HUGEPAGES` if lower, never lowers it.
What it changed is stored in `.host.state`; `--undo` reverts only that.

**Warning:** `--undo` after setup created the VFs removes all VFs of the PF. Any other
application using them, on this host or in a VM, loses its port.
