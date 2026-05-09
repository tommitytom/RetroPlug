const fs = require("fs");
const path = require("path");

// Resolve esbuild from lv_binding_js's node_modules
const LV_BINDING_DIR = path.resolve(__dirname, "../deps/lv_binding_js");
const esbuild = require(path.join(LV_BINDING_DIR, "node_modules/esbuild"));
const aliasPlugin = require(path.join(LV_BINDING_DIR, "node_modules/esbuild-plugin-alias"));

// Args from CMake (or default to writing into ../build/ui/ for ad-hoc runs).
//   node build-ui.js <bundle.js> [bundle_data.c] [bundle.d]
const bundleArg = process.argv[2];
const embedArg  = process.argv[3];
const depArg    = process.argv[4];

const bundlePath = bundleArg
    ? path.resolve(bundleArg)
    : path.resolve(__dirname, "../build/ui/bundle.js");

fs.mkdirSync(path.dirname(bundlePath), { recursive: true });

esbuild
    .build({
        entryPoints: [path.resolve(__dirname, "../ui/PluginUI.tsx")],
        bundle: true,
        platform: "neutral",
        external: ["tjs:path"],
        jsx: "automatic",
        outfile: bundlePath,
        nodePaths: [path.join(LV_BINDING_DIR, "node_modules")],
        // Read the renderer source directly, bypassing pnpm's file:-dep copy.
        plugins: [
            aliasPlugin({
                "lvgljs-ui": path.join(LV_BINDING_DIR, "src/render/react/index.ts"),
                "lvgljs":    path.resolve(__dirname, "../runtime/lvgljs/index.ts"),
            }),
        ],
        define: {
            "process.env.NODE_ENV": '"production"',
        },
        metafile: true,
    })
    .then((result) => {
        console.log(`UI bundle built: ${bundlePath}`);

        if (embedArg) {
            const embedPath = path.resolve(embedArg);
            fs.mkdirSync(path.dirname(embedPath), { recursive: true });
            const bytes = fs.readFileSync(bundlePath);
            writeEmbed(embedPath, bytes, "ui_bundle_js");
            console.log(`UI bundle embedded: ${embedPath} (${bytes.length} bytes)`);
        }

        if (depArg) {
            const depPath = path.resolve(depArg);
            fs.mkdirSync(path.dirname(depPath), { recursive: true });
            writeDepfile(depPath, bundlePath, result.metafile);
        }
    })
    .catch((e) => {
        console.error(e);
        process.exit(1);
    });

function writeEmbed(outPath, bytes, symbol) {
    // 16 bytes per line for readability; bundle is ~280KB → ~1.5MB of text.
    //
    // We append a trailing 0x00 byte to the array but exclude it from the
    // reported length. txiki.js / QuickJS's JS_Eval requires the source
    // buffer to be null-terminated even when a length is supplied
    // (txiki's own file-loader path does the same — see
    // deps/lv_binding_js/deps/txiki/src/vm.c, "/* Add null termination,
    // required by JS_Eval. */"). Without it, the parser reads one byte
    // past the buffer end and reports a UTF-8 error at a phantom line/col.
    const parts = ["/* auto-generated; do not edit */\n#include <stddef.h>\n\n"];
    parts.push(`const unsigned char ${symbol}[] = {\n`);
    for (let i = 0; i < bytes.length; i += 16) {
        const chunk = [];
        for (let j = i; j < i + 16 && j < bytes.length; j++) {
            chunk.push("0x" + bytes[j].toString(16).padStart(2, "0"));
        }
        parts.push("    " + chunk.join(", ") + ",\n");
    }
    parts.push("    0x00 /* null terminator for JS_Eval; not counted in _len */\n");
    parts.push("};\n");
    parts.push(`const unsigned int ${symbol}_len = ${bytes.length};\n`);
    fs.writeFileSync(outPath, parts.join(""));
}

function writeDepfile(depPath, target, metafile) {
    // Make-style depfile: `<target>: <input1> <input2> ...`
    // CMake's DEPFILE expects POSIX-ish target paths and space-separated deps,
    // with backslash-newline continuations and escaped spaces.
    const inputs = Object.keys(metafile.inputs)
        .map((p) => path.resolve(p))
        .map(escapeMake);
    const escapedTarget = escapeMake(target);
    const body = `${escapedTarget}: ${inputs.join(" \\\n  ")}\n`;
    fs.writeFileSync(depPath, body);
}

function escapeMake(p) {
    // Escape spaces and backslashes for Make
    return p.replace(/\\/g, "\\\\").replace(/ /g, "\\ ");
}
