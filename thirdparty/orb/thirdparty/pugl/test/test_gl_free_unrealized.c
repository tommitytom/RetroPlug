// Copyright 2022-2024 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

/*
  Tests that deleting an unrealized view works properly.

  This was a crash bug with OpenGL backends.
*/

#undef NDEBUG

#include <pugl/gl.h>
#include <pugl/pugl.h>

#include <assert.h>
#include <stddef.h>

typedef struct {
  PuglWorld* world;
  PuglView*  view;
} PuglTest;

int
main(void)
{
  PuglTest test = {puglNewWorld(PUGL_PROGRAM, 0), NULL};

  // Set up view
  test.view = puglNewView(test.world);
  puglSetWorldString(test.world, PUGL_CLASS_NAME, "PuglTest");
  puglSetViewString(test.view, PUGL_WINDOW_TITLE, "Pugl OpenGL Free Test");
  puglSetBackend(test.view, puglGlBackend());
  puglSetHandle(test.view, &test);
  puglSetSizeHint(test.view, PUGL_DEFAULT_SIZE, 256, 256);
  puglSetPositionHint(test.view, PUGL_DEFAULT_POSITION, 640, 896);

  assert(!puglGetVisible(test.view));

  // Tear everything down without ever realizing the view
  puglFreeView(test.view);
  puglFreeWorld(test.world);

  return 0;
}
