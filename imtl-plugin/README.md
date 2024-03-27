#  svt-jpeg-xs plugin library


## 1. Build:
### 1. Build and install Intel Media Transport Library
Please refer to https://github.com/OpenVisualCloud/Media-Transport-Library/blob/main/doc/build.md
### 2. Build and install svt-jpeg-xs
```
cd <jpeg-xs-repo>/Build/linux
./build.sh install
```
### 2. Build and install imtl-plugin
```bash
cd <jpeg-xs-repo>/imtl-plugin
./build.sh
```

## 2. Test:

#### 2.1 Prepare a yuv422p8le file.
```bash
wget https://www.larmoire.info/jellyfish/media/jellyfish-3-mbps-hd-hevc.mkv
ffmpeg -i jellyfish-3-mbps-hd-hevc.mkv -vframes 3 -c:v rawvideo yuv420p8le.yuv
ffmpeg -s 1920x1080 -pix_fmt yuv420p -i yuv420p8le.yuv -pix_fmt yuv422p test_planar8.yuv
```

#### 2.2 Edit "IMTL-repo/kahawai.json"  to enable the st22 svt jpeg xs plugin.
You can also copy kahawai.json from ```<jpeg-xs-repo>/imtl-plugin/kahawai.json```
```bash
        {
            "enabled": 1,
            "name": "st22_svt_jpeg_xs",
            "path": "/usr/local/lib/x86_64-linux-gnu/libst_plugin_st22_svt_jpeg_xs.so"
        },
        {
            "enabled": 1,
            "name": "st22_svt_jpeg_xs",
            "path": "/usr/local/lib64/libst_plugin_st22_svt_jpeg_xs.so"
        }
```

#### 2.3 Run the imtl sample with tx and rx based on jpegxs.
Tx run:
```bash
<IMTL-repo>/build/app/TxSt22PipelineSample --st22_codec jpegxs --pipeline_fmt YUV422PLANAR8 --p_port 0000:31:00.0 --tx_url test_planar8.yuv
```
Rx run:
```bash
<IMTL-repo>/build/app/RxSt22PipelineSample --st22_codec jpegxs --pipeline_fmt YUV422PLANAR8 --p_port 0000:31:00.1 --rx_url out_planar8.yuv
```

### 3 Notes
If you get below similar message when runing the RxTxApp, it's likely a ld library path problem.
```bash
./build/app/RxTxApp: error while loading shared libraries: librte_dmadev.so.23: cannot open shared object file: No such file or directory
```
In this case you might need to update your LD_LIBRARY_PATH
```bash
export LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
```
If you get below similar message when runing the RxTxApp, you need to update hugepage size
```bash
EAL: FATAL: Cannot get hugepage information.
EAL: Cannot get hugepage information.
```
Please run
```
sudo sysctl -w vm.nr_hugepages=2048
```
