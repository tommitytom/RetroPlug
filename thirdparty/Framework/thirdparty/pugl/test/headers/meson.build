# Copyright 2020-2025 David Robillard <d@drobilla.net>
# SPDX-License-Identifier: 0BSD OR ISC

header_c_suppressions = []

if get_option('warning_level') == 'everything'
  if cc.get_id() == 'clang'
    if not meson.is_cross_build()
      header_c_suppressions += ['-Wno-poison-system-directories']
    endif
  endif
endif

header_c_suppressions = cc.get_supported_arguments(header_c_suppressions)

test_headers_c_args = header_c_suppressions
test_headers_c_args += [
  '-DPUGL_NO_INCLUDE_GL_H',
  '-DPUGL_NO_INCLUDE_GLU_H',
]

if vulkan_dep.found()
  test_headers_c_args += ['-DPUGL_TEST_VULKAN']
endif

test(
  'headers',
  executable(
    'test_headers_c',
    files('test_headers.c'),
    c_args: test_headers_c_args,
    dependencies: [pugl_dep, vulkan_dep],
    implicit_include_directories: false,
  ),
  suite: 'unit',
)
