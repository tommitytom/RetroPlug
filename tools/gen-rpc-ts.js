// Generate the typed TS client for PluginRpcService from its OpenRPC
// schema.
//
// Usage (called from CMake):
//   node tools/gen-rpc-ts.js <rpc-schema-dump-exe> <out-ts>
//
// 1. Spawns <rpc-schema-dump-exe>, captures stdout (the OpenRPC JSON).
// 2. Bundles deps/rpcpp/clients/typescript/codegen/src/index.ts via the
//    esbuild that ships with deps/lv_binding_js/node_modules.
// 3. Requires the bundle and calls `writeService(doc, 'ts', 'PluginService',
//    <out-ts>)`. The codegen's writeService is idempotent — it skips the
//    write if the output is unchanged.

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const REPO_ROOT  = path.resolve(__dirname, '..');
const LV_DIR     = path.join(REPO_ROOT, 'deps/lv_binding_js');
const CODEGEN_TS = path.join(REPO_ROOT, 'deps/rpcpp/clients/typescript/codegen/src/index.ts');

const esbuild = require(path.join(LV_DIR, 'node_modules/esbuild'));

const [, , exePath, outPath] = process.argv;
if (!exePath || !outPath) {
    process.stderr.write('usage: gen-rpc-ts.js <rpc-schema-dump-exe> <out-ts>\n');
    process.exit(2);
}

// Step 1: invoke the C++ schema dumper.
const schemaJson = execFileSync(exePath, [], { encoding: 'utf8' });
const doc = JSON.parse(schemaJson);

// Step 2: bundle the codegen entry to an ESM .mjs we can dynamic-import.
// The codegen uses import.meta.url; staying in ESM keeps that intact.
const bundleOut = path.join(path.dirname(outPath), '.codegen-bundle.mjs');
fs.mkdirSync(path.dirname(bundleOut), { recursive: true });

esbuild.buildSync({
    entryPoints: [CODEGEN_TS],
    bundle:      true,
    format:      'esm',
    platform:    'node',
    target:      'node20',
    outfile:     bundleOut,
    logLevel:    'silent',
});

// Step 3: write the typed TS client. writeService skips the write if the
// content is unchanged, so this is cheap on incremental builds.
//
// Then post-process the output. The codegen has two gaps for rpcpp's
// reflect-cpp-emitted schemas:
//   1. Method-position $refs are taken verbatim from the schema ($ref.split('/').pop()),
//      so wrapper type names like `std::optional<T>` and `std::vector<T>` leak
//      through as mangled identifiers (`std__optional_T_`, `std__vector_T_`).
//   2. Struct $refs in method bodies use the raw mangled `PluginRpcService__X`
//      form rather than the camel-cased `PluginRpcServiceX` form that the
//      interface declarations use.
// We patch both with regex substitutions on the emitted file.
(async () => {
    const codegen = await import('file://' + bundleOut);
    await codegen.writeService(doc, 'ts', 'PluginService', outPath);

    let src = fs.readFileSync(outPath, 'utf8');

    // unsigned_int / int → number, char_const → string. Run before the
    // mangle-strip below so `std__optional_unsigned_int_` becomes
    // `number | null` rather than `UnsignedInt | null`.
    const primMap = {
        'unsigned_int':  'number',
        'unsigned_long': 'number',
        'int':           'number',
        'long':          'number',
        'double':        'number',
        'float':         'number',
        'bool':          'boolean',
    };

    src = src.replace(/std__optional_([A-Za-z0-9_]+?)_(?=\b|\W)/g, (_, inner) => {
        const prim = primMap[inner];
        const baseRaw = prim || stripMangle(inner);
        const base = baseRaw.includes(' ') ? `(${baseRaw})` : baseRaw;
        return `${base} | null`;
    });
    src = src.replace(/std__vector_([A-Za-z0-9_]+?)_(?=\b|\W)/g, (_, inner) => {
        const prim = primMap[inner];
        const baseRaw = prim || stripMangle(inner);
        const base = baseRaw.includes(' ') ? `(${baseRaw})` : baseRaw;
        return `${base}[]`;
    });

    // Class-name mangles: PluginRpcService__OpenRomOpts → PluginRpcServiceOpenRomOpts.
    src = src.replace(/[A-Za-z][A-Za-z0-9]*(?:__[A-Za-z][A-Za-z0-9]*)+/g, (m) => {
        return m.replace(/__(.)/g, (_, c) => c.toUpperCase());
    });

    fs.writeFileSync(outPath, src);
    console.log(`Generated ${path.relative(REPO_ROOT, outPath)}`);

    function stripMangle(s) {
        return s.replace(/__(.)/g, (_, c) => c.toUpperCase())
                .replace(/^./, (c) => c.toUpperCase());
    }
})().catch((e) => {
    console.error(e);
    process.exit(1);
});
