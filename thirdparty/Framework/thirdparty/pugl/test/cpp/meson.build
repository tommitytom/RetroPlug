# Copyright 2021-2025 David Robillard <d@drobilla.net>
# SPDX-License-Identifier: 0BSD OR ISC

test_cpp_args = []
if get_option('warning_level') == 'everything' and is_variable('cpp')
  if cpp.get_id() == 'clang'
    test_cpp_args += [
      '-Wno-documentation', # Cairo
      '-Wno-documentation-unknown-command', # Cairo
      '-Wno-old-style-cast',
    ]
  endif

  test_cpp_args = cpp.get_supported_arguments(test_cpp_args)
endif

unified_args = core_args
unified_deps = [core_deps]
if cairo_dep.found()
  unified_args += ['-DWITH_CAIRO']
  unified_deps += [cairo_dep]
endif

if opengl_dep.found()
  unified_args += ['-DWITH_OPENGL']
  unified_deps += [opengl_dep]
endif

if vulkan_dep.found()
  unified_args += ['-DWITH_VULKAN']
  unified_deps += [vulkan_deps]
endif

if host_machine.system() == 'darwin'
  add_languages(['objcpp'], native: false)

  objcpp = meson.get_compiler('objcpp')

  objcpp_unified_args = unified_args
  if objcpp.get_id() == 'clang'
    objcpp_unified_args += [
      '-Wno-c++98-compat',
      '-Wno-c++98-compat-pedantic',
      '-Wno-deprecated-declarations',
      '-Wno-direct-ivar-access',
    ]
  endif

  objcpp_unified_args = objcpp.get_supported_arguments(objcpp_unified_args)

  test(
    'inline_objcpp',
    executable(
      'test_inline_objcpp',
      'test_inline_objcpp.mm',
      dependencies: unified_deps,
      implicit_include_directories: false,
      include_directories: include_directories('../../include'),
      objcpp_args: objcpp_unified_args,
    ),
    suite: 'unit',
  )

elif is_variable('cpp')
  cpp_unified_args = unified_args
  if cpp.get_id() == 'clang'
    cpp_unified_args += [
      '-Wno-old-style-cast',
      '-Wno-switch-default',
      '-Wno-switch-enum',
      '-Wno-unused-macros', # Mac
    ]
    if host_machine.system() == 'windows'
      cpp_unified_args += [
        '-Wno-cast-function-type',
        '-Wno-deprecated-declarations',
        '-Wno-nonportable-system-include-path',
      ]
    endif
  elif cpp.get_id() == 'gcc'
    cpp_unified_args += [
      '-Wno-conditionally-supported',
      '-Wno-old-style-cast',
      '-Wno-switch-default',
      '-Wno-switch-enum',
      '-Wno-useless-cast',
    ]
  elif cpp.get_id() == 'msvc'
    cpp_unified_args += [
      '/wd4464', # relative include path contains '..'
    ]
  endif

  cpp_unified_args = cpp.get_supported_arguments(cpp_unified_args)

  test(
    'inline_cpp',
    executable(
      'test_inline_cpp',
      'test_inline_cpp.cpp',
      cpp_args: cpp_unified_args,
      dependencies: unified_deps,
      implicit_include_directories: false,
      include_directories: include_directories('../../include'),
    ),
    suite: 'unit',
  )
endif
