import type {
	MainModule,
	NativeProject,
	RetroPlugView,
	WebApplicationRunner
} from "./native/RetroPlug.d.ts";
import { Project } from "./wrapper/Project.ts";

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

		this._module.setupWasmFs();
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

		return this._module.upcastView(view)!;
	}

	get project(): Project {
		const project = this.view.getProject();
		if (!project) {
			throw new Error("WASM project is not initialized");
		}
		return new Project(this._module!, project);
	}

	get nativeProject(): NativeProject {
		const project = this.view.getProject();
		if (!project) {
			throw new Error("WASM project is not initialized");
		}

		return project;
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
