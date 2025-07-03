# Copyright 2021 David Robillard <d@drobilla.net>
# SPDX-License-Identifier: 0BSD OR ISC

cpp_pugl_rst = custom_target(
  'cpp_pugl.rst',
  command: [sphinxygen, '-l', 'cpp', '-f', '@INPUT@', '@OUTDIR@'],
  input: cpp_index_xml,
  output: 'pugl.rst',
)
