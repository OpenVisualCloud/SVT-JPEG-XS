#
# Copyright(c) 2024 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
#

project('svt-jpeg-xs plugin for intel media transport libraray', 'c', default_options: ['c_std=gnu11', 'buildtype=release'], version: '0.1',)

# allow experimental api
add_global_arguments('-DALLOW_EXPERIMENTAL_API', language : 'c')

cc = meson.get_compiler('c')

mtl_dep = dependency('mtl', required : true)
libpthread_dep = cc.find_library('pthread', required : true)
jpegxs_dep = dependency('SvtJpegxs', required : true)

# add include directory
plugins_include_dir = [ include_directories('include'), ]

# add source file
subdir('src')

plugins_c_args = []
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
  dependencies: [mtl_dep, libpthread_dep, jpegxs_dep],
  install: true
)
