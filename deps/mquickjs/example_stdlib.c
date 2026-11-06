#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mquickjs_build.h"

/* simple class example */

static const JSPropDef js_rectangle_proto[] = {
    JS_CGETSET_DEF("x", js_rectangle_get_x, NULL ),
    JS_CGETSET_DEF("y", js_rectangle_get_y, NULL ),
    JS_PROP_END,
};

static const JSClassDef js_rectangle_class =
    JS_CLASS_DEF("Rectangle", 2, js_rectangle_constructor, JS_CLASS_RECTANGLE, NULL, js_rectangle_proto, NULL, js_rectangle_finalizer);

static const JSPropDef js_filled_rectangle_proto[] = {
    JS_CGETSET_DEF("color", js_filled_rectangle_get_color, NULL ),
    JS_PROP_END,
};

/* inherit from Rectangle */
static const JSClassDef js_filled_rectangle_class =
    JS_CLASS_DEF("FilledRectangle", 3, js_filled_rectangle_constructor, JS_CLASS_FILLED_RECTANGLE, NULL, js_filled_rectangle_proto, &js_rectangle_class, js_filled_rectangle_finalizer);

/* TestObject */

static const JSPropDef js_test_object_proto[] = {
    JS_CGETSET_DEF("x", js_test_object_get_x, js_test_object_set_x ),
    JS_CGETSET_DEF("value", js_test_object_get_value, js_test_object_set_value ),
    JS_PROP_END,
};

static const JSPropDef js_test_object[] = {
    JS_CFUNC_DEF("getClosure", 1, js_test_object_getClosure ),
    JS_CFUNC_DEF("call", 2, js_test_object_call ),
    JS_PROP_END,
};

static const JSClassDef js_test_object_class =
    JS_CLASS_DEF("Test_object", 2, js_test_object_constructor, JS_CLASS_TEST_OBJECT, js_test_object, js_test_object_proto, NULL, js_test_object_finalizer);

/* include the full standard library too */

#define CONFIG_CLASS_EXAMPLE
#include "mqjs_stdlib.c"
