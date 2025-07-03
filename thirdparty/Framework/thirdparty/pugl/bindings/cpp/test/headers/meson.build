# Copyright 2020-2025 David Robillard <d@drobilla.net>
# SPDX-License-Identifier: 0BSD OR ISC

header_cpp_suppressions = []

if get_option('warning_level') == 'everything'
  if cpp.get_id() == 'clang'
    if not meson.is_cross_build()
      header_cpp_suppressions += [
        '-Wno-old-style-cast',
        '-Wno-poison-system-directories',
      ]
    endif
  endif
endif

header_cpp_suppressions = cpp.get_supported_arguments(header_cpp_suppressions)

test_headers_cpp_args = header_cpp_suppressions
test_headers_cpp_args += [
  '-DPUGL_NO_INCLUDE_GL_H',
  '-DPUGL_NO_INCLUDE_GLU_H',
]

if vulkan_dep.found()
  test_headers_cpp_args += ['-DPUGL_TEST_VULKAN']
endif

test(
  'headers',
  executable(
    'test_headers_cpp',
    files('test_headers.cpp'),
    cpp_args: test_headers_cpp_args,
    dependencies: [puglpp_dep, vulkan_dep],
    implicit_include_directories: false,
  ),
  suite: 'unit',
)
