import { BiquadEffect } from "./effects/BiquadEffect.ts";
import type {
	MainModule,
	NativeProject,
	RetroPlugView,
	WebApplicationRunner,
	MemoryAccessor,
	NativeMemoryType,
	Uint8Buffer,
	LsdjMemoryOffsets,
	NativeLsdjRom,
	NativeBiquadEffect,
	NativeEffect
} from "./native/RetroPlug.d.ts";
import { Project } from "./wrapper/Project.ts";
import { convertMemoryType, MemoryType } from "./wrapper/System.ts";

export class RetroPlugApplication {
	private _module: MainModule | null = null;
	private _runner: WebApplicationRunner | null = null;

	get module() {
		return this._module;
	}

	async load() {
		const moduleFactory = (await import("./native/RetroPlug.mjs")).default;

		this._module = (await moduleFactory({
			locateFile: (path: string) => {
				if (path.endsWith(".wasm")) {
					return "/RetroPlug.wasm";
				}
				return path;
			},
			preRun: [
				() => {
					console.log("WASM module pre-run");
				},
			],
			onRuntimeInitialized: () => {
				console.log("WASM runtime initialized");
			},
			print: (text: string) => {
				console.log("WASM:", text);
			},
			printErr: (text: string) => {
				console.error("WASM Error:", text);
			},
		})) as MainModule;

		this._runner = new this._module.WebApplicationRunner();

		console.log("WASM module loaded");
	}

	get runner(): WebApplicationRunner | null {
		return this._runner;
	}

	get view(): RetroPlugView {
		if (!this._module || !this._runner) {
			throw new Error("WASM module is not initialized");
		}

		const view = this._runner.getView();
		if (!view) {
			throw new Error("WASM view is not initialized");
		}

		const final = this._module.upcastView(view)!;
		view.delete();

		return final;
	}

	get project(): Project {
		const project = this.nativeProject;
		return new Project(this._module!, project);
	}

	get nativeProject(): NativeProject {
		const view = this.view;
		const project = view.getProject();
		view.delete();

		if (!project) {
			throw new Error("WASM project is not initialized");
		}

		return project;
	}

	createAudioBuffer(channelCount: number, sampleCount: number, sampleRate: number) {
		return new this._module!.NativeAudioBuffer(channelCount, sampleCount, sampleRate);
	}

	createBiquadEffect() {
		return new BiquadEffect(this._module!, new this._module!.NativeBiquadEffect());
	}

	createDitherEffect() {
		return new this._module!.NativeDitherEffect();
	}

	createEffectChain() {
		return new this._module!.NativeEffectChain();
	}

	createMemoryAccessor(memoryType: MemoryType, buffer: Uint8Buffer, offset: number = 0) {
		return new this._module!.MemoryAccessor(convertMemoryType(this._module!, memoryType), buffer, offset);
	}

	createLsdjSav(): any;
	createLsdjSav(buffer: Uint8Buffer): any;
	createLsdjSav(buffer?: Uint8Buffer) {
		if (buffer !== undefined) {
			return new this._module!.NativeLsdjSav(buffer);
		}
		return new this._module!.NativeLsdjSav();
	}

	createLsdjRom(accessor: MemoryAccessor): NativeLsdjRom {
		return new this._module!.NativeLsdjRom(accessor);
	}

	createLsdjRam(accessor: MemoryAccessor, offsets: LsdjMemoryOffsets) {
		return new this._module!.NativeLsdjRam(accessor, offsets);
	}

	createUint8Buffer(): Uint8Buffer;
	createUint8Buffer(size: number): Uint8Buffer;
	createUint8Buffer(size?: number) {
		if (size !== undefined) {
			return new this._module!.Uint8Buffer(size);
		}
		return new this._module!.Uint8Buffer();
	}

	createFloat32Buffer(): any;
	createFloat32Buffer(size: number): any;
	createFloat32Buffer(size?: number) {
		if (size !== undefined) {
			return new this._module!.Float32Buffer(size);
		}
		return new this._module!.Float32Buffer();
	}

	setupAudio(audioContext: AudioContext | null) {
		if (!this._module || !this._runner) {
			throw new Error("WASM module is not initialized");
		}

		const contextId = this._module.emscriptenRegisterAudioObject(audioContext);
		this._runner.setupAudio(contextId);
	}

	setupGraphics(canvasId: string) {
		if (!this._module || !this._runner) {
			throw new Error("WASM module is not initialized");
		}

		this._runner.setupGraphics(canvasId);
		this._runner.start();
	}

	destroyGraphics() {
		if (!this._module || !this._runner) {
			throw new Error("WASM module is not initialized");
		}

		this._runner.stop();
		this._runner.destroyGraphics();
	}

	destroy() {
		//this._module?.destroy();
	}
}
