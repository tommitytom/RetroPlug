import type { Float32Buffer, MainModule, Uint8Buffer } from "../native/RetroPlug";

export function convertBuffer(module: MainModule, buffer: Uint8Array): Uint8Buffer {
	const target = new module!.Uint8Buffer(buffer.byteLength);
	target.data().set(buffer);
	return target;
}

export function convertFloat32Buffer(buffer: Float32Buffer): Float32Array {
	const target = new Float32Array(buffer.size());
	target.set(buffer.data());
	return target;
}
