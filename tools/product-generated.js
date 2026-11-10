// esbuild's `onResolve` half of the per-product bundles (docs/modular-graph-phase-f5a-design.md section 7.2): a
// product whose package list differs from the desktop's has its own generated modules, written under the build
// directory in the shape the shell's tree has, and this plugin makes the shell's own sources read them.
//
// The shell's sources import their generated modules by RELATIVE path ("../generated/packages", "./generated/menus"),
// and esbuild's `alias` only substitutes package specifiers: a one-file bundle with an alias for `generated` still
// compiled the desktop's file (a relative import is resolved against its importer, never through the alias table). So
// the redirect is a resolver: an import whose path ends in one of the generated modules' names, from an importer under
// the shell's src/ or ui/, is answered with the product's file. The entry points are never redirected (a script-node
// bundle's entry IS the product's generated module), and neither is an import from anywhere else: a product's own tree
// already names its siblings, and a package's module names no generated file.
//
//   const { productGenerated } = require("./product-generated");
//   esbuild.build({ ..., plugins: [productGenerated({ dir: "<build>/products/<name>/packages/retroplug" })] })
//
// No plugin (no `dir`) is the desktop: the shell's tree holds the desktop's modules, so nothing is redirected and the
// build takes esbuild's synchronous path (a plugin needs the asynchronous one).
const path = require("path");

const REPO_ROOT = path.resolve(__dirname, "..");
const SHELL = path.join(REPO_ROOT, "packages/retroplug");

/** The generated modules and the tree they live in: the protocol tables and the app registry are src/, the UI's two
 *  are ui/. */
const GENERATED = {
  packages: "src",
  consoles: "src",
  protocol: "src",
  scriptNodes: "src",
  placers: "src",
  menus: "ui",
  views: "ui",
};

const NAME = /\/generated\/([A-Za-z]+)(?:\.ts)?$/;

/** The generated trees the desktop's sources read: a product's bundle must carry none of them. A redirect can miss
 *  (an import spelled with .js, a new generated module the map above does not know, an importer whose path compares
 *  unequal on Windows), and a miss silently bundles the DESKTOP's registry into the product. This is the one check
 *  that covers every list and every platform. */
const SHELL_GENERATED = ["src", "ui", "cli"].map((tree) => path.join(SHELL, tree, "generated"));

const under = (file, dir) => file === dir || file.startsWith(dir + path.sep);

/** The plugin, or null when no product tree was given (the desktop's bundle). */
function productGenerated({ dir } = {}) {
  if (!dir) return null;
  const productTree = path.resolve(dir);
  return {
    name: "retroplug-product-generated",
    setup(build) {
      // The metafile is what onEnd below reads, and a plugin cannot ask for one after the build starts.
      build.initialOptions.metafile = true;
      build.onResolve({ filter: /\/generated\/[A-Za-z]+(?:\.ts)?$/ }, (args) => {
        if (args.kind === "entry-point" || !args.importer) return undefined;
        const found = NAME.exec(args.path.split("\\").join("/"));
        if (!found || !Object.hasOwn(GENERATED, found[1])) return undefined;
        const importer = path.resolve(args.importer);
        if (!under(importer, path.join(SHELL, "src")) && !under(importer, path.join(SHELL, "ui"))) return undefined;
        const name = found[1];
        return { path: path.join(productTree, GENERATED[name], "generated", `${name}.ts`) };
      });
      // Every module the bundle carries, after the fact: a file under the shell's generated trees is the desktop's
      // registry, which this product must not carry. Refused by name, so the build says which import missed.
      build.onEnd((result) => {
        if (!result.metafile) return undefined;
        const base = path.resolve(build.initialOptions.absWorkingDir ?? process.cwd());
        const offenders = Object.keys(result.metafile.inputs)
          .map((input) => path.resolve(base, input))
          .filter((file) => SHELL_GENERATED.some((dir) => under(file, dir)));
        if (!offenders.length) return undefined;
        return {
          errors: offenders.map((file) => ({
            text: `the product bundle carries ${path.relative(REPO_ROOT, file)}, one of the desktop's generated ` +
              "modules; a product's own tree does not import it",
          })),
        };
      });
    },
  };
}

module.exports = { productGenerated };
