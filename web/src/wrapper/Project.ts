import type { Entity, MainModule, NativeGameboyModel, NativeRetroPlugProject, NativeSameBoyComponent, NativeSystemLoadComponent, NativeSystemLoadEntry } from "../native/RetroPlug";
import { convertBuffer } from "../utils/FileUtil";

export const INVALID_SYSTEM_ID = 4294967295;

export type SystemId = number;

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
	Agb
};

interface SameBoyComponent {
	model?: GameboyModel;
	fastBoot?: boolean;
}

export function convertGameBoyModel(
	module: MainModule,
	type: GameboyModel,
): NativeGameboyModel {
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

	constructor(module: MainModule, project: NativeRetroPlugProject) {
		console.assert(!!project);
		this._module = module;
		this._project = project;
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

	addSystem(load: SystemLoadComponent, sameboy?: SameBoyComponent) {
		sameboy = sameboy || {};
		const nativeLoad = new this._module.NativeSystemLoadComponent();

		for (const entry in load.entries) {
			const { path, data } = load.entries[entry];
			const systemEntry = new this._module!.NativeSystemLoadEntry();
			if (path) systemEntry.path = path;
			if (data) systemEntry.setData(convertBuffer(this._module, data));
			nativeLoad.entries.set(entry, systemEntry);
		}

		const e = this._project.addSystem(nativeLoad, {
			model: convertGameBoyModel(this._module, sameboy.model || GameboyModel.Auto),
			fastBoot: sameboy.fastBoot || true
		});

		console.log('adding system');

		return e;
	}

	removeSystem(index: SystemId): void {
		this._project.removeSystem({ value: index } as Entity);
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
