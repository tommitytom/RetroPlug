import type { Float32Buffer, MainModule, NativeAccessType, NativeMemoryType, Uint8Buffer } from "../native/RetroPlug";
import { AccessType, MemoryType } from "../wrapper/System";

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

/**
 * Downloads a Uint8Array as a file
 * @param data - The Uint8Array data to download
 * @param filename - The filename for the download
 * @param mimeType - The MIME type for the file (defaults to 'application/octet-stream')
 */
export function downloadUint8Array(data: Uint8Array, filename: string, mimeType = 'application/octet-stream'): void {
	const buffer = new Uint8Array(data);
	const blob = new Blob([buffer], { type: mimeType });
	const url = URL.createObjectURL(blob);
	const a = document.createElement('a');
	a.href = url;
	a.download = filename;
	document.body.appendChild(a);
	a.click();
	document.body.removeChild(a);
	URL.revokeObjectURL(url);
}

/**
 * Downloads a Uint8Buffer as a file
 * @param buffer - The Uint8Buffer data to download
 * @param filename - The filename for the download
 * @param mimeType - The MIME type for the file (defaults to 'application/octet-stream')
 */
export function downloadUint8Buffer(buffer: Uint8Buffer, filename: string, mimeType = 'application/octet-stream'): void {
	const uint8Array = toUint8Array(buffer);
	downloadUint8Array(uint8Array, filename, mimeType);
}

/**
 * Sanitizes a filename by replacing special characters with underscores
 * @param filename - The original filename
 * @returns The sanitized filename
 */
export function sanitizeFilename(filename: string): string {
	return filename.replace(/[^a-zA-Z0-9.-]/g, '_');
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
