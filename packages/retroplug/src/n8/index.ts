// The TS Everdrive N8 Pro stack: the Edio protocol framing over an injected serial byte-transport.
// Everything here is pure TS + host-agnostic - the native side keeps only the serial transport (the
// `SerialRpcService` facet, reached from TS via createSerialClient() in ../realBackend.ts). The on-device
// menu commands + ROM/save orchestration land here in a later phase (n8Menu.ts / n8Load.ts).

export {
  Edio,
  N8TimeoutError,
  ADDR_SRM,
  ADDR_FIFO,
  SIZE_SRM_GAME,
  FA_WRITE,
  FA_CREATE_ALWAYS,
  FS_MAKEPATH,
} from "./edio";
export type { N8DirEntry } from "./edio";
export type { SerialTransport, OpenSerialPort, SerialPortInfo, SerialClient } from "./transport";
