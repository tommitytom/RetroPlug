import type { LoadConfig, MainModule, NativeProject, SystemDesc } from "../native/RetroPlug";
import { System } from "./System";

export const INVALID_SYSTEM_ID = 4294967295;

export type SystemId = number;

export class Project {
	private _module: MainModule;
	private _project: NativeProject;

	constructor(module: MainModule, project: NativeProject) {
		this._module = module;
		this._project = project;
	}

	get isDirty(): boolean {
		return this._project.isDirty;
	}

	get version(): number {
		return this._project.version;
	}

	get systemCount(): number {
		return this._project.systemCount;
	}

	get scale(): number {
		return this._project.scale;
	}

	clear(): void {
		this._project.clear();
	}

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

	removeSystem(index: SystemId): void {
		this._project.removeSystem(index);
	}

	loadSystem(config: LoadConfig, index: SystemId = INVALID_SYSTEM_ID) {
		return this._project.loadSystem(this._module.SAMEBOY_GUID, config, index);
	}

	addSystem(desc: SystemDesc, index: SystemId = INVALID_SYSTEM_ID) {
		return this._project.addSystem(this._module.SAMEBOY_GUID, desc, index);
	}

	/**
	 * Returns an iterable of System instances for all systems in the project.
	 */
	get systems(): Iterable<System> {
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
	}
}
