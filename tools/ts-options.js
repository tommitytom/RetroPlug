// The TypeScript options the bundled artifacts of this repo are compiled with, spelled out so that the bytes a bundle
// carries never depend on which tsconfig.json esbuild happened to find.
//
// esbuild reads the NEAREST tsconfig.json above each entry point when a build names none, so the compiler options of
// whatever package an entry lives in decide the output of a bundle that gets embedded in a binary or shipped to a
// consumer. One option shows up in the output as bytes directly: `alwaysStrict` makes esbuild head an iife bundle with
// a `"use strict";` directive, which is inside a script pack's bytecode (the packbytes and device-image legs are that
// byte proof) and inside every iife bundle the native build embeds. A pack whose package does not extend the
// workspace's tsconfig.base.json, or a later edit to that base, would then move those bytes with no bundler edit at
// all. So each build names the options it compiles with, and no bundle's bytes are anyone's tsconfig accident.
//
// What is pinned is what esbuild reads out of a tsconfig and nothing else: `target` (every build already passes it to
// esbuild itself; naming it here keeps the class-field lowering of a bundle fixed) with the `useDefineForClassFields`
// the ES2020 target implies, and `alwaysStrict`, which each caller states because the bundles genuinely differ - the
// iife bundles built from the shell's TS carry the directive today, and the device packs, which their toolchain wraps
// in a directive of their own, carry exactly one.
//
// A package's own tsconfig.json stays what `tsc` checks that package with; this file decides only what a bundler emits.
// That separation is why tsconfig.base.json sets no `alwaysStrict` at all (its `strict` implies one for the type
// checker, which changes no bytes): a bundle's directive is spelled here, so a compiler-settings edit moves no pack's
// and no bundle's bytes. The remaining typechecking options (`noUnusedLocals`, `lib`, `types`) change no output either,
// and nothing here resolves a module: a build that needs an alias passes it to esbuild as `alias`
// (tools/esbuild-shared.js), and no bundled module reaches a tsconfig `paths` entry.
function tsOptions({ alwaysStrict = false } = {}) {
    return {
        compilerOptions: {
            target: "ES2020",
            useDefineForClassFields: false,
            alwaysStrict,
        },
    };
}

module.exports = { tsOptions };
