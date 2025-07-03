# Copyright 2021-2024 David Robillard <d@drobilla.net>
# SPDX-License-Identifier: 0BSD OR ISC

c_pugl_rst = custom_target(
  'c_pugl.rst',
  command: [sphinxygen, '-f', '@INPUT0@', '@OUTDIR@'],
  input: [c_index_xml] + c_rst_files,
  output: 'pugl.rst',
)
