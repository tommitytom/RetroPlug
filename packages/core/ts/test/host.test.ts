// The host contract's group selections (docs/modular-graph-phase-f4b-design.md section 3.2): a hook types itself by the
// groups it uses, so a surface whose host lacks one is a compile error rather than a member that is undefined when the
// hook runs. The cases that decide that are the type errors, which `tsc` holds; the one runtime case keeps the table
// itself honest.
import { test, expect } from "@retroplug/testing";
import type { FilesHost, Host, PngHost } from "@retroplug/core/host";

const files: FilesHost = {
  readFile: () => null,
  readPrefix: () => null,
  writeFile: () => true,
  writeFileAtomic: () => true,
  fileExists: () => false,
  listDir: () => [],
};
const png: PngHost = {
  encode: () => null,
  decode: () => null,
};

/** A hook that asks for the PNG codec: an asset override decodes a linked font image. */
function needsPng(host: Host<"png">): string | null {
  return host.png.decode(new Uint8Array(0)) === null ? null : "";
}

/** A hook that asks for the file reads alone. */
function needsFiles(host: Host<"files">): Uint8Array | null {
  return host.files.readFile("");
}

test("a group selection is exactly the groups it names", () => {
  const both: Host<"files" | "png"> = { files, png };
  // A host that offers more than a hook asks is what the shell passes: a surface's own host satisfies every hook
  // narrower than it.
  expect(needsFiles(both)).toBe(null);
  expect(needsPng(both)).toBe(null);

  // The refusals, held by the compiler and never run.
  const typeCases = (filesOnly: Host<"files">): void => {
    // @ts-expect-error a files host has no png group, so the codec hook cannot be given one
    needsPng(filesOnly);
    // @ts-expect-error a host lacking a group does not satisfy that group's selection (and has an excess `files`)
    const noSuchGroup: Host<"kits"> = { files };
    void noSuchGroup;
  };
  // Marks the type cases used without running them.
  expect(typeof typeCases).toBe("function");
});
