import type { Float32Buffer, Uint8Buffer } from "../native/RetroPlug";
import { RetroPlugApplication } from "../RetroPlugApplication";

export async function convertFile(app: RetroPlugApplication, file: File): Promise<Uint8Buffer> {
	const romData = new Uint8Array(await file.arrayBuffer());
	const romBuffer = new app.module!.Uint8Buffer(romData.byteLength);
	romBuffer.data().set(romData);
	return romBuffer;
}

export function convertFloat32Buffer(buffer: Float32Buffer): Float32Array {
	const target = new Float32Array(buffer.size());
	target.set(buffer.data());
	return target;
}
