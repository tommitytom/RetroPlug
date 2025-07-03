# Copyright 2021-2024 David Robillard <d@drobilla.net>
# SPDX-License-Identifier: 0BSD OR ISC

docdir = get_option('datadir') / 'doc'

# Find required external programs
doxygen = find_program('doxygen', required: get_option('docs'))
sphinx_build = find_program('sphinx-build', required: get_option('docs'))

# Find sphinxygen or fall back to subproject
if doxygen.found() and sphinx_build.found()
  sphinxygen = find_program('sphinxygen', required: false)
  if not sphinxygen.found()
    subproject('sphinxygen')
    sphinxygen = find_program('sphinxygen', required: get_option('docs'))
  endif
else
  sphinxygen = disabler()
endif

# Build documentation if all required tools are found
build_docs = doxygen.found() and sphinxygen.found() and sphinx_build.found()
if build_docs
  subdir('_static')
  subdir('c')

  if get_option('docs_cpp')
    subdir('cpp')
  endif
endif
