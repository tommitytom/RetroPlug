// Generate a typed TS client from a service's OpenRPC schema.
//
// Usage (called from CMake):
//   node tools/gen-rpc-ts.js <schema-dump-exe> <out-ts> [serviceName] [...exeArgs]
//
// e.g. plugin:  gen-rpc-ts.js <rpc-schema-dump> <out>           (-> PluginService)
//      harness: gen-rpc-ts.js <retroplug-cli> <out> HarnessService --dump-harness-schema
//
// 1. Spawns <schema-dump-exe> [exeArgs], captures stdout (the OpenRPC JSON).
// 2. Bundles deps/rpcpp/clients/typescript/codegen/src/index.ts via the
//    workspace esbuild (see tools/esbuild-shared.js).
// 3. Requires the bundle and calls `writeService(doc, 'ts', 'PluginService',
//    <out-ts>)`. The codegen's writeService is idempotent — it skips the
//    write if the output is unchanged.

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const REPO_ROOT  = path.resolve(__dirname, '..');
const CODEGEN_TS = path.join(REPO_ROOT, 'deps/rpcpp/clients/typescript/codegen/src/index.ts');

const { esbuild } = require('./esbuild-shared');

const [, , exePath, outPath, serviceNameArg, ...exeArgs] = process.argv;
if (!exePath || !outPath) {
    process.stderr.write('usage: gen-rpc-ts.js <schema-dump-exe> <out-ts> [serviceName] [...exeArgs]\n');
    process.exit(2);
}
const serviceName = serviceNameArg || 'PluginService';

// Step 1: invoke the C++ schema dumper.
const schemaJson = execFileSync(exePath, exeArgs, { encoding: 'utf8' });
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
    await codegen.writeService(doc, 'ts', serviceName, outPath);

    let src = fs.readFileSync(outPath, 'utf8');

    // unsigned_int / int → number, char_const → string. Run before the
    // mangle-strip below so `std__optional_unsigned_int_` becomes
    // `number | null` rather than `UnsignedInt | null`.
    const primMap = {
        'unsigned_int':  'number',
        'unsigned_long': 'number',
        'unsigned_char': 'number',
        'int':           'number',
        'long':          'number',
        'double':        'number',
        'float':         'number',
        'bool':          'boolean',
    };

    // std::string mangles differently across stdlibs (libstdc++:
    // `std____cxx11__basic_string_char__`, libc++: `std____1__basic_string...`),
    // and reflect-cpp emits a dangling $ref for it inside vector/optional, so
    // match on the stable `basic_string` substring (plus the plain `std__string`).
    const isStdString = (inner) =>
        /basic_string/.test(inner) || inner === 'std__string';

    src = src.replace(/std__optional_([A-Za-z0-9_]+?)_(?=\b|\W)/g, (_, inner) => {
        if (isStdString(inner)) return 'string | null';
        const prim = primMap[inner];
        const baseRaw = prim || stripMangle(inner);
        const base = baseRaw.includes(' ') ? `(${baseRaw})` : baseRaw;
        return `${base} | null`;
    });
    src = src.replace(/std__vector_([A-Za-z0-9_]+?)_(?=\b|\W)/g, (_, inner) => {
        // rfl::Bytestring (std::vector<std::byte>) is a binary buffer — type it
        // as Uint8Array (it decodes from msgpack BIN), not StdByte[].
        if (inner === 'std__byte') return 'Uint8Array';
        if (isStdString(inner)) return 'string[]';
        const prim = primMap[inner];
        const baseRaw = prim || stripMangle(inner);
        const base = baseRaw.includes(' ') ? `(${baseRaw})` : baseRaw;
        return `${base}[]`;
    });

    // Class-name mangles: PluginRpcService__OpenRomOpts → PluginRpcServiceOpenRomOpts,
    // rp__BreakInfo → RpBreakInfo. Uppercase the first char so a method-position
    // $ref matches its PascalCase interface declaration.
    src = src.replace(/[A-Za-z][A-Za-z0-9]*(?:__[A-Za-z][A-Za-z0-9]*)+/g, (m) => {
        const collapsed = m.replace(/__(.)/g, (_, c) => c.toUpperCase());
        return collapsed.charAt(0).toUpperCase() + collapsed.slice(1);
    });

    // Idempotent write: only touch the file when bytes actually change.
    // writeService is documented idempotent, but the regex post-process
    // above unconditionally produces a new buffer — without this guard
    // the mtime bumps every build and esbuild rebuilds bundle.js for
    // nothing. (CMake's copy_if_different on bundle_data.c still
    // backstops the cascade, but quietness here is cheap.)
    let prior = null;
    try { prior = fs.readFileSync(outPath, 'utf8'); } catch (_) { /* missing */ }
    if (prior !== src) {
        fs.writeFileSync(outPath, src);
        console.log(`Generated ${path.relative(REPO_ROOT, outPath)}`);
    } else {
        console.log(`Up-to-date ${path.relative(REPO_ROOT, outPath)}`);
    }

    function stripMangle(s) {
        return s.replace(/__(.)/g, (_, c) => c.toUpperCase())
                .replace(/^./, (c) => c.toUpperCase());
    }
})().catch((e) => {
    console.error(e);
    process.exit(1);
});
