// The host groups a control-plane hook runs on (docs/modular-graph-phase-f4b-design.md section 3): the file I/O and the
// PNG codec, over the backend. A construct-time role hook and an asset bake are handed exactly this, and each surface's
// own host builds its `files` and `png` groups here, so "the backend as a host" has one definition.
import type { FilesHost, Host, PngHost } from "@retroplug/core/host";
import type { HostBackend } from "./backend";

/** The backend's file I/O as core's files group. The methods are wrapped rather than handed over, so what a hook
 *  receives is a table it cannot reach the rest of the backend through. */
export function filesHost(
  backend: Pick<HostBackend, "readFile" | "readFilePrefix" | "writeFile" | "writeFileAtomic" | "fileExists" | "listDir">,
): FilesHost {
  return {
    readFile: (path) => backend.readFile(path),
    readPrefix: (path, bytes) => backend.readFilePrefix(path, bytes),
    writeFile: (path, bytes) => backend.writeFile(path, bytes),
    writeFileAtomic: (path, bytes) => backend.writeFileAtomic(path, bytes),
    fileExists: (path) => backend.fileExists(path),
    listDir: (dir) => backend.listDir(dir),
  };
}

/** The backend's PNG codec as core's png group. */
export function pngHost(backend: Pick<HostBackend, "pngEncode" | "pngDecode">): PngHost {
  return {
    encode: (width, height, rgba) => backend.pngEncode(width, height, rgba),
    decode: (bytes) => backend.pngDecode(bytes),
  };
}

/** The two groups a construct-time hook or an asset bake is handed. */
export function filesPngHost(
  backend: Pick<
    HostBackend,
    "readFile" | "readFilePrefix" | "writeFile" | "writeFileAtomic" | "fileExists" | "listDir" | "pngEncode" | "pngDecode"
  >,
): Host<"files" | "png"> {
  return { files: filesHost(backend), png: pngHost(backend) };
}
