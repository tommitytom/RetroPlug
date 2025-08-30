import type {
	LsdjMemoryOffsets,
	MainModule,
	NativeAccessType,
	NativeLsdjRam,
	NativeMemoryType,
	NativeProxySystem,
	NativeSystemStateHashes,
} from "../native/RetroPlug";

export enum AccessType {
	Unknown,
	Read,
	Write,
	ReadWrite,
}

export enum MemoryType {
	Unknown,
	Ram,
	Rom,
	Sram,
	Vram,
}

export const LSDJ_SERVICE_TYPE = 0x15D115D1;

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
		case MemoryType.Unknown:
		default:
			return module.NativeMemoryType.Unknown;
	}
}

function convertAccessType(
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

export class System {
	private _module: MainModule;
	private _system: NativeProxySystem;

	constructor(module: MainModule, system: NativeProxySystem) {
		this._module = module;
		this._system = system;
	}

	get romName(): string {
		return this._system.getRomName();
	}

	get id() {
		return this._system.id;
	}

	getMemory(memoryType: MemoryType, accessType: AccessType) {
		return this._system.getMemory(
			convertMemoryType(this._module, memoryType),
			convertAccessType(this._module, accessType),
		);
	}

	reset() {
		this._system.reset();
	}

	get stateHashes(): NativeSystemStateHashes {
		return this._system.getStateHashes();
	}

	get lsdjSav() {
		const systemMemory = this.getMemory(MemoryType.Sram, AccessType.Read);
		return new this._module.NativeLsdjSav(systemMemory.getBuffer());
	}

	get lsdjRom() {
		const systemMemory = this.getMemory(MemoryType.Rom, AccessType.Read);
		return new this._module.NativeLsdjRom(systemMemory);
	}

	getLsdjRam(ramOffset: LsdjMemoryOffsets): NativeLsdjRam {
		const systemMemory = this.getMemory(MemoryType.Ram, AccessType.Read);
		return new this._module.NativeLsdjRam(systemMemory, ramOffset);
	}
}
