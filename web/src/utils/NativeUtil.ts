import type { Float32Buffer, MainModule, Uint8Buffer } from "../native/RetroPlug";

export interface NativeVector<T> {
	size(): number;
	get(index: number): T | undefined;
	push_back(item: T): void;
	resize(newSize: number, defaultValue: T): void;
	set(index: number, value: T): boolean;
}

export function fromUint8Array(module: MainModule, buffer: Uint8Array): Uint8Buffer {
	const target = new module!.Uint8Buffer(buffer.byteLength);
	target.data().set(buffer);
	return target;
}

export function toUint8Array(buffer: Uint8Buffer): Uint8Array {
	return new Uint8Array(buffer.data());
}

export function convertFloat32Buffer(buffer: Float32Buffer): Float32Array {
	const target = new Float32Array(buffer.size());
	target.set(buffer.data());
	return target;
}

export function vectorToArray<T>(vec: NativeVector<T>): T[] {
	const arr: T[] = [];
	for (let i = 0; i < vec.size(); ++i) {
		arr.push(vec.get(i)!);
	}
	return arr;
}
