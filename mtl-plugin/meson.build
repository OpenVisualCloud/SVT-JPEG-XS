#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

project('svt-jpeg-xs plugin for media transport library', 'c', default_options: ['c_std=gnu11', 'buildtype=release'], version: '0.2',)

# allow experimental api
add_global_arguments('-DALLOW_EXPERIMENTAL_API', language : 'c')

cc = meson.get_compiler('c')

# >= 26.09, the MTL release this plugin is tested with
mtl_dep = dependency('mtl', required : true, version : '>=26.09')
libpthread_dep = cc.find_library('pthread', required : true)
# >= 0.10 for bpp_denominator and the image_config fields used to validate
# the decoded stream format against the pipeline's requested output format.
jpegxs_dep = dependency('SvtJpegxs', required : true, version : '>=0.10')
# plugin config file (SVT_JXS_MTL_PLUGIN_CONFIG), also a dependency of mtl
# >= 0.15 for json_tokener_get_parse_end (syntax error line numbers)
jsonc_dep = dependency('json-c', required : true, version : '>=0.15')

# add include directory
plugins_include_dir = [ include_directories('include'), ]

# add source file
subdir('src')

plugins_c_args = []

# newer encoder parameters share the 0.10 API version with older libraries,
# probe the installed headers for them; config keys for missing ones are
# rejected at plugin start
foreach member : ['lossless_enable', 'coding_raw_disable', 'cap_compat',
                  'enable_color_transform', 'profile_ppih_override', 'level_plev_override',
                  'input_bit_depth_msb_aligned']
  if cc.has_member('svt_jpeg_xs_encoder_api_t', member, prefix : '#include <SvtJpegxsEnc.h>',
                   dependencies : jpegxs_dep)
    plugins_c_args += ['-DHAVE_ENC_' + member.to_upper()]
  endif
endforeach
# YUV422PLANAR16LE (MSB-aligned) needs both the encoder and the decoder field
if cc.has_member('svt_jpeg_xs_decoder_api_t', 'output_bit_depth_msb_aligned',
                 prefix : '#include <SvtJpegxsDec.h>', dependencies : jpegxs_dep)
  plugins_c_args += ['-DHAVE_DEC_OUTPUT_BIT_DEPTH_MSB_ALIGNED']
endif
#enable warning as error
plugins_c_args += ['-Werror']
plugins_c_args += ['-Wall']
#simd build option, enable sse4.2 default, todo: do we need AVX2/AVX512 for app ?
plugins_c_args += ['-msse4.2']

plugins_ld_args = []

# build st22 svt jpeg xs plugin lib
shared_library('st_plugin_st22_svt_jpeg_xs', st22_svt_jpeg_xs_plugin_sources,
  c_args : plugins_c_args,
  link_args : plugins_ld_args,
  include_directories : plugins_include_dir,
  dependencies: [mtl_dep, libpthread_dep, jpegxs_dep, jsonc_dep],
  # export only the plugin API (ST_PLUGIN_API), so internal symbols can't bind to
  # same-named ones elsewhere in the process
  gnu_symbol_visibility : 'hidden',
  install: true
)
