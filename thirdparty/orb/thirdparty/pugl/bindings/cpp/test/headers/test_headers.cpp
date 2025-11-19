// Copyright 2022-2025 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include <pugl/cairo.hpp> // IWYU pragma: keep
#include <pugl/gl.hpp>    // IWYU pragma: keep
#include <pugl/pugl.hpp>  // IWYU pragma: keep
#include <pugl/stub.hpp>  // IWYU pragma: keep

#ifdef PUGL_TEST_VULKAN
#  include <pugl/vulkan.hpp> // IWYU pragma: keep
#endif

#ifdef __GNUC__
__attribute__((const))
#endif
int
main()
{
  return 0;
}
