/* RetroPlug's standard library for MicroQuickJS, as its build tool reads it (rp_mqjs.h says what it holds). Built with
 * upstream's mquickjs_build.c into a host tool that prints the library's tables for a width (cmake/mquickjs.cmake,
 * retroplug_mquickjs_stdlib); never compiled into a binary that runs JavaScript. A table entry names its C function by
 * name only, as a string the tool copies into the generated tables, which rp_mqjs_table.c compiles.
 *
 * Upstream's standard classes are its own tables (deps/mquickjs/mqjs_stdlib.c, MIT, unpatched), included as they stand
 * with the three names that are its REPL's alone renamed out of the way: its global object, its C function list and
 * its main. Its REPL's globals are then in no table built here: this file's global object lists what a script pack may
 * use. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mquickjs_build.h"

#define js_global_object   rp_mqjs_repl_global_object
#define js_c_function_decl rp_mqjs_repl_c_function_decl
#define main               rp_mqjs_repl_main
#include "mqjs_stdlib.c"
#undef main
#undef js_c_function_decl
#undef js_global_object

/* console.log, which the host prints where its embedder says (MqjsHost.hpp), or nowhere. */
static const JSPropDef js_rp_console[] = {
    JS_CFUNC_MAGIC_DEF("log", 1, js_rp_call, RP_MQJS_LOG),
    JS_PROP_END,
};
static const JSClassDef js_rp_console_obj = JS_OBJECT_DEF("Console", js_rp_console);

/* The block a node's process is handed (packages/retroplug/src/graph/defineNode.ts's Block). */
static const JSPropDef js_rp_block_proto[] = {
    JS_CFUNC_MAGIC_DEF("messages", 1, js_rp_call, RP_MQJS_MESSAGES),
    JS_CFUNC_MAGIC_DEF("send", 4, js_rp_call, RP_MQJS_SEND),
    JS_CFUNC_MAGIC_DEF("control", 1, js_rp_call, RP_MQJS_CONTROL),
    JS_CFUNC_MAGIC_DEF("emit", 3, js_rp_call, RP_MQJS_EMIT),
    JS_CFUNC_MAGIC_DEF("audio", 1, js_rp_call, RP_MQJS_AUDIO),
    JS_CGETSET_MAGIC_DEF("frames", js_rp_get, NULL, RP_MQJS_FRAMES),
    JS_CGETSET_MAGIC_DEF("transport", js_rp_get, NULL, RP_MQJS_TRANSPORT),
    JS_PROP_END,
};
static const JSClassDef js_rp_block_class = JS_CLASS_MAGIC_DEF("RpBlock", 0, js_rp_construct, RP_MQJS_CLASS_BLOCK,
                                                               NULL, js_rp_block_proto, NULL, NULL);

static const JSPropDef js_rp_message_proto[] = {
    JS_CGETSET_MAGIC_DEF("frame", js_rp_get, NULL, RP_MQJS_MESSAGE_FRAME),
    JS_CGETSET_MAGIC_DEF("data", js_rp_get, NULL, RP_MQJS_MESSAGE_DATA),
    JS_CGETSET_MAGIC_DEF("flags", js_rp_get, NULL, RP_MQJS_MESSAGE_FLAGS),
    JS_PROP_END,
};
static const JSClassDef js_rp_message_class = JS_CLASS_MAGIC_DEF("RpMessage", 0, js_rp_construct,
                                                                 RP_MQJS_CLASS_MESSAGE, NULL, js_rp_message_proto,
                                                                 NULL, NULL);

static const JSPropDef js_rp_transport_proto[] = {
    JS_CGETSET_MAGIC_DEF("sampleRate", js_rp_get, NULL, RP_MQJS_SAMPLE_RATE),
    JS_CGETSET_MAGIC_DEF("tempo", js_rp_get, NULL, RP_MQJS_TEMPO),
    JS_CGETSET_MAGIC_DEF("ppq", js_rp_get, NULL, RP_MQJS_PPQ),
    JS_CGETSET_MAGIC_DEF("playing", js_rp_get, NULL, RP_MQJS_PLAYING),
    JS_PROP_END,
};
static const JSClassDef js_rp_transport_class = JS_CLASS_MAGIC_DEF("RpTransport", 0, js_rp_construct,
                                                                   RP_MQJS_CLASS_TRANSPORT, NULL, js_rp_transport_proto,
                                                                   NULL, NULL);

static const JSPropDef js_rp_control_stream_proto[] = {
    JS_CGETSET_MAGIC_DEF("start", js_rp_get, NULL, RP_MQJS_STREAM_START),
    JS_CGETSET_MAGIC_DEF("changes", js_rp_get, NULL, RP_MQJS_STREAM_CHANGES),
    JS_PROP_END,
};
static const JSClassDef js_rp_control_stream_class =
    JS_CLASS_MAGIC_DEF("RpControlStream", 0, js_rp_construct, RP_MQJS_CLASS_CONTROL_STREAM, NULL,
                       js_rp_control_stream_proto, NULL, NULL);

static const JSPropDef js_rp_control_change_proto[] = {
    JS_CGETSET_MAGIC_DEF("frame", js_rp_get, NULL, RP_MQJS_CHANGE_FRAME),
    JS_CGETSET_MAGIC_DEF("value", js_rp_get, NULL, RP_MQJS_CHANGE_VALUE),
    JS_PROP_END,
};
static const JSClassDef js_rp_control_change_class =
    JS_CLASS_MAGIC_DEF("RpControlChange", 0, js_rp_construct, RP_MQJS_CLASS_CONTROL_CHANGE, NULL,
                       js_rp_control_change_proto, NULL, NULL);

/* What a script pack may use: upstream's standard globals, the console, and the block API's classes (globals only
 * because the build tool takes a class from the global object alone; their constructors refuse). */
static const JSPropDef rp_global_object[] = {
    JS_PROP_CLASS_DEF("Object", &js_object_class),
    JS_PROP_CLASS_DEF("Function", &js_function_class),
    JS_PROP_CLASS_DEF("Number", &js_number_class),
    JS_PROP_CLASS_DEF("Boolean", &js_boolean_class),
    JS_PROP_CLASS_DEF("String", &js_string_class),
    JS_PROP_CLASS_DEF("Array", &js_array_class),
    JS_PROP_CLASS_DEF("Math", &js_math_obj),
    JS_PROP_CLASS_DEF("JSON", &js_json_obj),
    JS_PROP_CLASS_DEF("RegExp", &js_regexp_class),

    JS_PROP_CLASS_DEF("Error", &js_error_class),
    JS_PROP_CLASS_DEF("EvalError", &js_eval_error_class),
    JS_PROP_CLASS_DEF("RangeError", &js_range_error_class),
    JS_PROP_CLASS_DEF("ReferenceError", &js_reference_error_class),
    JS_PROP_CLASS_DEF("SyntaxError", &js_syntax_error_class),
    JS_PROP_CLASS_DEF("TypeError", &js_type_error_class),
    JS_PROP_CLASS_DEF("URIError", &js_uri_error_class),
    JS_PROP_CLASS_DEF("InternalError", &js_internal_error_class),

    JS_PROP_CLASS_DEF("ArrayBuffer", &js_array_buffer_class),
    JS_PROP_CLASS_DEF("Uint8ClampedArray", &js_Uint8ClampedArray_class),
    JS_PROP_CLASS_DEF("Int8Array", &js_Int8Array_class),
    JS_PROP_CLASS_DEF("Uint8Array", &js_Uint8Array_class),
    JS_PROP_CLASS_DEF("Int16Array", &js_Int16Array_class),
    JS_PROP_CLASS_DEF("Uint16Array", &js_Uint16Array_class),
    JS_PROP_CLASS_DEF("Int32Array", &js_Int32Array_class),
    JS_PROP_CLASS_DEF("Uint32Array", &js_Uint32Array_class),
    JS_PROP_CLASS_DEF("Float32Array", &js_Float32Array_class),
    JS_PROP_CLASS_DEF("Float64Array", &js_Float64Array_class),

    JS_CFUNC_DEF("parseInt", 2, js_number_parseInt),
    JS_CFUNC_DEF("parseFloat", 1, js_number_parseFloat),
    JS_CFUNC_DEF("isNaN", 1, js_global_isNaN),
    JS_CFUNC_DEF("isFinite", 1, js_global_isFinite),

    JS_PROP_DOUBLE_DEF("Infinity", 1.0 / 0.0, 0),
    JS_PROP_DOUBLE_DEF("NaN", NAN, 0),
    JS_PROP_UNDEFINED_DEF("undefined", 0),
    /* Note: null is expanded as the global object in the global object's table. */
    JS_PROP_NULL_DEF("globalThis", 0),

    JS_PROP_CLASS_DEF("console", &js_rp_console_obj),

    JS_PROP_CLASS_DEF("RpBlock", &js_rp_block_class),
    JS_PROP_CLASS_DEF("RpMessage", &js_rp_message_class),
    JS_PROP_CLASS_DEF("RpTransport", &js_rp_transport_class),
    JS_PROP_CLASS_DEF("RpControlStream", &js_rp_control_stream_class),
    JS_PROP_CLASS_DEF("RpControlChange", &js_rp_control_change_class),
    JS_PROP_END,
};

/* C closures: Function.prototype.bind's, which must come first. */
static const JSPropDef rp_c_function_decl[] = {
    JS_CFUNC_SPECIAL_DEF("bound", 0, generic_params, js_function_bound),
    JS_PROP_END,
};

int main(int argc, char** argv) {
    return build_atoms("rp_mqjs_stdlib", rp_global_object, rp_c_function_decl, argc, argv);
}
