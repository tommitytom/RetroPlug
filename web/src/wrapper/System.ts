import type {
	LsdjMemoryOffsets,
	MainModule,
	NativeAccessType,
	NativeLsdjRam,
	NativeMemoryType,
} from "../native/RetroPlug";

export enum AccessType {
	Unknown,
	Read,
	Write,
	ReadWrite,
}

export enum MemoryType {
	Ram,
	Rom,
	Sram,
	Vram,
	MAX
}

export const LSDJ_SERVICE_TYPE = 0x15D115D1;



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
