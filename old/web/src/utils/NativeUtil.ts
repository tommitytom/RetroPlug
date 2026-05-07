import type { Float32Buffer, MainModule, NativeAccessType, NativeMemoryType, Uint8Buffer } from "../native/RetroPlug";
import { AccessType, MemoryType } from "../wrapper/System";

export const INVALID_SYSTEM_ID = 4294967295;
export type SystemId = number;

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

export function fromArrayBuffer(module: MainModule, buffer: ArrayBuffer): Uint8Buffer {
	const target = new module!.Uint8Buffer(buffer.byteLength);
	target.data().set(new Uint8Array(buffer));
	return target;
}

export function toUint8Array(buffer: Uint8Buffer): Uint8Array {
	return new Uint8Array(buffer.data());
}

export function toArrayBuffer(buffer: Uint8Buffer): ArrayBuffer {
	return toUint8Array(buffer).slice().buffer;
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

export function convertMemoryType(
	module: MainModule,
	type: MemoryType,
): NativeMemoryType {
	switch (type) {
		case MemoryType.Ram:
			return module.NativeMemoryType.Ram;
		case MemoryType.Rom:
			return module.NativeMemoryType.Rom;
		case MemoryType.Sram:
			return module.NativeMemoryType.Sram;
		case MemoryType.Vram:
			return module.NativeMemoryType.Vram;
		case MemoryType.MAX:
		default:
			return module.NativeMemoryType.MAX;
	}
}

export function convertAccessType(
	module: MainModule,
	type: AccessType,
): NativeAccessType {
	switch (type) {
		case AccessType.Read:
			return module.NativeAccessType.Read;
		case AccessType.Write:
			return module.NativeAccessType.Write;
		case AccessType.ReadWrite:
			return module.NativeAccessType.ReadWrite;
		case AccessType.Unknown:
		default:
			return module.NativeAccessType.Unknown;
	}
}
