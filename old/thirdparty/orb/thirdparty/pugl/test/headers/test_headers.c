// Copyright 2022-2025 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include <pugl/attributes.h> // IWYU pragma: keep
#include <pugl/cairo.h>      // IWYU pragma: keep
#include <pugl/gl.h>         // IWYU pragma: keep
#include <pugl/glu.h>        // IWYU pragma: keep
#include <pugl/pugl.h>       // IWYU pragma: keep
#include <pugl/stub.h>       // IWYU pragma: keep

#ifdef PUGL_TEST_VULKAN
#  include <pugl/vulkan.h> // IWYU pragma: keep
#endif

#ifdef __GNUC__
__attribute__((const))
#endif
int
main(void)
{
  return 0;
}
