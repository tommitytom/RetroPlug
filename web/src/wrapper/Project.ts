import type {
	MainModule,
	NativeGameboyModel,
	NativeRetroPlugProject,
	NativeSystemLoadEntry,
} from '../native/RetroPlug';
import { convertAccessType, convertMemoryType, type SystemId } from '../utils/NativeUtil';
import { LsdjController } from './Lsdj';
import { AccessType, MemoryType } from './System';

interface SystemLoadComponent {
	entries: Record<string, { path?: string; data?: Uint8Array }>;
}

enum GameboyModel {
	Auto,
	DmgB,
	//SgbNtsc,
	//SgbPal,
	//Sgb2,
	CgbC,
	CgbE,
	Agb,
}

interface SameBoyComponent {
	model?: GameboyModel;
	fastBoot?: boolean;
}

export function convertGameBoyModel(module: MainModule, type: GameboyModel): NativeGameboyModel {
	switch (type) {
		case GameboyModel.Auto:
			return module.NativeGameboyModel.Auto;
		case GameboyModel.DmgB:
			return module.NativeGameboyModel.DmgB;
		case GameboyModel.CgbC:
			return module.NativeGameboyModel.CgbC;
		case GameboyModel.CgbE:
			return module.NativeGameboyModel.CgbE;
		case GameboyModel.Agb:
			return module.NativeGameboyModel.Agb;
	}
}

export class Project {
	private _module: MainModule;
	private _project: NativeRetroPlugProject;
	private _lsdjController: LsdjController;

	constructor(module: MainModule, project: NativeRetroPlugProject) {
		console.assert(!!project);
		this._module = module;
		this._project = project;
		this._lsdjController = new LsdjController(module, project.getLsdjController());
	}

	get module() {
		return this._module;
	}

	get isDirty(): boolean {
		return false;
		//return this._project.isDirty;
	}

	get version(): number {
		return this._project.version;
	}

	get systemCount(): number {
		return this._project.systemCount;
	}

	get scale(): number {
		return 3;
		//return this._project.scale;
	}

	get lsdj(): LsdjController {
		return this._lsdjController;
	}

	clear(): void {
		//this._project.clear();
	}
	/*
	getNativeSystemById(id: SystemId) {
		const system = this._project.getSystem(id);
		if (!system) throw new Error(`System not found at id ${id}`);
		return system;
	}

	getSystemById(id: SystemId) {
		return new System(this._module, this.getNativeSystemById(id));
	}

	getNativeSystemByIndex(index: SystemId) {
		const system = this._project.getSystemByIndex(index);
		if (!system) throw new Error(`System not found at index ${index}`);
		return system;
	}

	getSystemByIndex(index: SystemId) {
		return new System(this._module, this.getNativeSystemByIndex(index));
	}

	duplicateSystem(id: SystemId) {
		const system = this._project.duplicateSystem(id);
		if (!system) throw new Error(`Failed to duplicate system at id ${id}`);
		return new System(this._module, system);
	}
*/

	getSystemMemoryVersion(system: SystemId, memoryType: MemoryType) {
		return this._project.getMemoryVersion(system, convertMemoryType(this._module, memoryType));
	}

	getSystemMemory(system: SystemId, memoryType: MemoryType, accessType: AccessType) {
		return this._project.getSystemMemory(
			system,
			convertMemoryType(this._module, memoryType),
			convertAccessType(this._module, accessType),
		);
	}

	getSystemIds(): SystemId[] {
		const ids = this._project.getSystemIds();
		const out: SystemId[] = [];
		for (let i = 0; i < ids.size(); i++) {
			out.push(ids.get(i)!);
		}

		return out;
	}

	serialize(rootPath: string): string {
		return this._project.serializeToString(rootPath);
	}

	deserialize(data: string, rootPath: string) {
		return this._project.deserializeFromString(data, rootPath);
	}

	subscribeToMemory(system: SystemId, memoryType: MemoryType) {
		this._project.subscribeToMemory(system, convertMemoryType(this._module, memoryType));
	}

	unsubscribeFromMemory(system: SystemId, memoryType: MemoryType) {
		this._project.unsubscribeFromMemory(system, convertMemoryType(this._module, memoryType));
	}

	addSystem(load: SystemLoadComponent, sameboy?: SameBoyComponent) {
		sameboy = sameboy || {};
		const nativeLoad = new this._module.NativeSystemLoadComponent();
		const nativeEntries: NativeSystemLoadEntry[] = [];

		for (const entry in load.entries) {
			const { path } = load.entries[entry];
			const nativeEntry = new this._module!.NativeSystemLoadEntry();
			if (path) nativeEntry.path = path;
			nativeLoad.entries.set(entry, nativeEntry);
			nativeEntries.push(nativeEntry);
		}

		const e = this._project.addSystem(nativeLoad, {
			model: convertGameBoyModel(this._module, sameboy.model || GameboyModel.Auto),
			fastBoot: sameboy.fastBoot || true,
		});

		for (const nativeEntry of nativeEntries) nativeEntry.delete();
		nativeLoad.delete();

		return e;
	}

	removeSystem(system: SystemId): void {
		this._project.removeSystem(system);
	}

	reset(): void {
		this._project.reset();
	}

	loadFromFile(path: string): boolean {
		const mountPath = this._project.getMountPath();
		return this._project.loadFromFile(mountPath + path);
	}

	loadFromPaths(paths: string[]): boolean {
		const mountPath = this._project.getMountPath();
		const pathVec = new this._module.StringVector();

		for (const path of paths) {
			pathVec.push_back(mountPath + path);
		}

		const valid = this._project.loadFromPaths(pathVec);
		pathVec.delete();

		return valid;
	}

	resetSystem(system: SystemId, remote: boolean = false): boolean {
		return this._project.resetSystem(system, remote);
	}

	/**
	 * Returns an iterable of System instances for all systems in the project.
	 */
	/*get systems(): Iterable<System> {
		const self = this;
		return {
			[Symbol.iterator](): Iterator<System> {
				let index = 0;
				return {
					next(): IteratorResult<System> {
						if (index < self.systemCount) {
							const value = self.getSystemByIndex(index);
							index++;
							return { value, done: false };
						} else {
							return { value: undefined, done: true };
						}
					}
				};
			}
		};
	}*/
}
