/* retroplug-modc: QuickJS module bytecode for modules whose imports are OTHER modules a host registers at run time
 * (docs/downloadable-packs-m2b-plan.md). QuickJS resolves a module's imports while compiling it, and tjsc's loader
 * answers only tjs: builtins, so tjsc refuses `import ... from "@retroplug/core/registry"`. This tool answers every import
 * with an empty module, which a compile never evaluates, so the bytecode it writes is what tjsc writes for a module tjsc
 * accepts (test/script/modc.test.ts holds the two byte for byte) and the imports are left for the host to resolve by
 * name.
 *
 *   retroplug-modc compile [-s|-ss] <module-name> <in.js> <out.bin>
 *       One module's bytecode, raw: what the tests compare with tjsc.
 *   retroplug-modc array [-s|-ss] <module-name> <symbol> <in.js> <out.c>
 *       One module as `const uint8_t <symbol>[]` and `const uint32_t <symbol>_size`, the shape tjsc -o writes: a host
 *       entry (rp_cp_bundle, rp_ui_bundle, rp_cli_bundle).
 *   retroplug-modc table [-s|-ss] <symbol> <manifest.tsv> <out.c>
 *       Every module the manifest lists (one `<module-name>\t<in.js>` per line, dependencies first) as one ordered
 *       `const rp_js_module <symbol>[]` and `const uint32_t <symbol>_count`, which registerSharedModules reads in order
 *       (reading a module's bytecode resolves its imports, so a dependency must come first).
 *
 * -s strips the source (tjsc -s), -ss the debug information too. A module name is refused unless it is
 * [A-Za-z0-9@/:._-]+ and starts with neither "." (a relative name) nor "tjs:" (txiki's builtins). */
#include "quickjs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *open_file(const char *path, const char *mode) {
#ifdef _MSC_VER
    FILE *f = NULL;
    if (fopen_s(&f, path, mode) != 0) return NULL;
    return f;
#else
    return fopen(path, mode);
#endif
}

static unsigned char *slurp(const char *path, size_t *len) {
    FILE *f = open_file(path, "rb");
    if (!f) {
        fprintf(stderr, "retroplug-modc: cannot open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    *len = (size_t) size;
    unsigned char *buf = (unsigned char *) malloc(*len + 1);
    if (!buf || fread(buf, 1, *len, f) != *len) {
        fprintf(stderr, "retroplug-modc: short read of %s\n", path);
        fclose(f);
        free(buf);
        return NULL;
    }
    buf[*len] = 0;
    fclose(f);
    return buf;
}

static int valid_name(const char *name) {
    if (!name[0] || name[0] == '.' || strncmp(name, "tjs:", 4) == 0) return 0;
    for (const char *c = name; *c; c++) {
        const int ok = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') ||
                       *c == '@' || *c == '/' || *c == ':' || *c == '.' || *c == '_' || *c == '-';
        if (!ok) return 0;
    }
    return 1;
}

/* An import's stand-in during a compile: never initialised, because a compile-only eval links and runs nothing. */
static int stub_init(JSContext *ctx, JSModuleDef *m) {
    (void) ctx;
    (void) m;
    return -1;
}

static JSModuleDef *stub_loader(JSContext *ctx, const char *name, void *opaque) {
    (void) opaque;
    return JS_NewCModule(ctx, name, stub_init);
}

typedef struct Compiler {
    JSRuntime *rt;
    JSContext *ctx;
} Compiler;

static int new_compiler(Compiler *c) {
    c->rt = JS_NewRuntime();
    if (!c->rt) return 0;
    c->ctx = JS_NewContext(c->rt);
    if (!c->ctx) {
        JS_FreeRuntime(c->rt);
        return 0;
    }
    JS_SetModuleLoaderFunc(c->rt, NULL, stub_loader, NULL);
    return 1;
}

static void free_compiler(Compiler *c) {
    JS_FreeContext(c->ctx);
    JS_FreeRuntime(c->rt);
}

/* Compile `in` as the module `name`: a js_malloc'd buffer the caller frees with js_free(c->ctx, ...) and its length in
 * *out_len, or NULL after printing why. */
static uint8_t *compile(Compiler *c, int strip, const char *name, const char *in, size_t *out_len) {
    if (!valid_name(name)) {
        fprintf(stderr, "retroplug-modc: %s is not a module name a host can register\n", name);
        return NULL;
    }
    size_t len = 0;
    unsigned char *src = slurp(in, &len);
    if (!src) return NULL;
    JSValue obj = JS_Eval(c->ctx, (const char *) src, len, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    free(src);
    if (JS_IsException(obj)) {
        JSValue e = JS_GetException(c->ctx);
        const char *msg = JS_ToCString(c->ctx, e);
        fprintf(stderr, "retroplug-modc: %s (%s): %s\n", in, name, msg ? msg : "an exception");
        JS_FreeCString(c->ctx, msg);
        JS_FreeValue(c->ctx, e);
        return NULL;
    }
    int flags = JS_WRITE_OBJ_BYTECODE;
    if (strip >= 1) flags |= JS_WRITE_OBJ_STRIP_SOURCE;
    if (strip >= 2) flags |= JS_WRITE_OBJ_STRIP_DEBUG;
    uint8_t *buf = JS_WriteObject(c->ctx, out_len, obj, flags);
    JS_FreeValue(c->ctx, obj);
    if (!buf) fprintf(stderr, "retroplug-modc: %s: could not write its bytecode\n", in);
    return buf;
}

static void write_bytes(FILE *f, const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) fprintf(f, "%s0x%02x,", i % 16 == 0 ? "\n  " : " ", buf[i]);
    fprintf(f, "\n");
}

static int cmd_compile(int strip, const char *name, const char *in, const char *out) {
    Compiler c;
    if (!new_compiler(&c)) return 1;
    size_t len = 0;
    uint8_t *buf = compile(&c, strip, name, in, &len);
    int rc = 1;
    if (buf) {
        FILE *f = open_file(out, "wb");
        if (!f) {
            fprintf(stderr, "retroplug-modc: cannot write %s\n", out);
        } else {
            const int wrote = fwrite(buf, 1, len, f) == len;
            if (fclose(f) == 0 && wrote) rc = 0;
            if (rc != 0) remove(out);
        }
        js_free(c.ctx, buf);
    }
    free_compiler(&c);
    return rc;
}

static int cmd_array(int strip, const char *name, const char *symbol, const char *in, const char *out) {
    Compiler c;
    if (!new_compiler(&c)) return 1;
    size_t len = 0;
    uint8_t *buf = compile(&c, strip, name, in, &len);
    int rc = 1;
    if (buf) {
        FILE *f = open_file(out, "wb");
        if (!f) {
            fprintf(stderr, "retroplug-modc: cannot write %s\n", out);
        } else {
            fprintf(f, "/* Generated by retroplug-modc from %s (module %s). Do not edit. */\n", in, name);
            fprintf(f, "#include <stdint.h>\n\n");
            fprintf(f, "const uint32_t %s_size = %lu;\n\n", symbol, (unsigned long) len);
            fprintf(f, "const uint8_t %s[%lu] = {", symbol, (unsigned long) len);
            write_bytes(f, buf, len);
            fprintf(f, "};\n");
            const int err = ferror(f);
            rc = (fclose(f) == 0 && !err) ? 0 : 1;
            if (rc != 0) remove(out);
        }
        js_free(c.ctx, buf);
    }
    free_compiler(&c);
    return rc;
}

static int cmd_table(int strip, const char *symbol, const char *manifest, const char *out) {
    size_t mlen = 0;
    char *text = (char *) slurp(manifest, &mlen);
    if (!text) return 1;
    FILE *f = open_file(out, "wb");
    if (!f) {
        free(text);
        return 1;
    }
    Compiler c;
    if (!new_compiler(&c)) {
        fclose(f);
        free(text);
        return 1;
    }
    fprintf(f, "/* Generated by retroplug-modc from %s. Do not edit. */\n", manifest);
    fprintf(f, "#include <stdint.h>\n\n");
    fprintf(f, "typedef struct rp_js_module {\n  const char *name;\n  const uint8_t *bytecode;\n  uint32_t size;\n} rp_js_module;\n");
    /* One pass over the manifest's lines writes each module's array and records its name and size; the table naming
     * them follows. A line is "<name>\t<path>"; an empty line is skipped. */
    int count = 0, rc = 0;
    char *names[4096];
    size_t sizes[4096];
    for (char *line = strtok(text, "\r\n"); line && rc == 0; line = strtok(NULL, "\r\n")) {
        char *tab = strchr(line, '\t');
        if (!tab) {
            fprintf(stderr, "retroplug-modc: %s: a line without a tab: %s\n", manifest, line);
            rc = 1;
            break;
        }
        *tab = 0;
        if (count == 4096) {
            fprintf(stderr, "retroplug-modc: %s lists more than 4096 modules\n", manifest);
            rc = 1;
            break;
        }
        for (int d = 0; d < count; d++) {
            if (strcmp(names[d], line) == 0) {
                fprintf(stderr, "retroplug-modc: %s: module %s is listed twice (entry %d)\n", manifest, line, count + 1);
                rc = 1;
                break;
            }
        }
        if (rc != 0) break;
        size_t len = 0;
        uint8_t *buf = compile(&c, strip, line, tab + 1, &len);
        if (!buf) {
            rc = 1;
            break;
        }
        fprintf(f, "\nstatic const uint8_t %s_%d[%lu] = {", symbol, count, (unsigned long) len);
        write_bytes(f, buf, len);
        fprintf(f, "};\n");
        js_free(c.ctx, buf);
        names[count] = line;
        sizes[count] = len;
        count++;
    }
    if (rc == 0) {
        fprintf(f, "\nconst rp_js_module %s[%d] = {\n", symbol, count > 0 ? count : 1);
        for (int i = 0; i < count; i++) {
            fprintf(f, "  { \"%s\", %s_%d, %lu },\n", names[i], symbol, i, (unsigned long) sizes[i]);
        }
        if (count == 0) fprintf(f, "  { 0, 0, 0 },\n");
        fprintf(f, "};\n\nconst uint32_t %s_count = %d;\n", symbol, count);
    }
    if (fclose(f) != 0) rc = 1;
    free_compiler(&c);
    free(text);
    if (rc != 0) remove(out);
    return rc;
}

static int strip_flag(const char *arg) {
    if (strcmp(arg, "-s") == 0) return 1;
    if (strcmp(arg, "-ss") == 0) return 2;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) goto usage;
    const char *cmd = argv[1];
    int i = 2;
    int strip = i < argc ? strip_flag(argv[i]) : 0;
    if (strip) i++;
    const int rest = argc - i;
    if (strcmp(cmd, "compile") == 0 && rest == 3) return cmd_compile(strip, argv[i], argv[i + 1], argv[i + 2]);
    if (strcmp(cmd, "array") == 0 && rest == 4) return cmd_array(strip, argv[i], argv[i + 1], argv[i + 2], argv[i + 3]);
    if (strcmp(cmd, "table") == 0 && rest == 3) return cmd_table(strip, argv[i], argv[i + 1], argv[i + 2]);
usage:
    fprintf(stderr,
            "usage: retroplug-modc compile [-s|-ss] <name> <in.js> <out.bin>\n"
            "       retroplug-modc array [-s|-ss] <name> <symbol> <in.js> <out.c>\n"
            "       retroplug-modc table [-s|-ss] <symbol> <manifest.tsv> <out.c>\n");
    return 2;
}
