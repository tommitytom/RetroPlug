import type { MainModule } from './native/RetroPlug.d.ts';

export class RetroPlugApplication {
	private _module: MainModule|null = null;

	async load() {
		const moduleFactory = (await import('./native/RetroPlug.mjs')).default;

		this._module = await moduleFactory({
			locateFile: (path: string) => {
				if (path.endsWith('.wasm')) {
					return '/RetroPlug.wasm';
				}
				return path;
			},
			preRun: [() => {
				console.log('WASM module pre-run');
			}],
			onRuntimeInitialized: () => {
				console.log('WASM runtime initialized');
			},
			print: (text: string) => {
				console.log('WASM:', text);
			},
			printErr: (text: string) => {
				console.error('WASM Error:', text);
			}
		}) as MainModule;

		console.log('WASM module loaded');
	}

	setup(canvasId: string, audioContext: AudioContext) {
		if (!this._module) {
			throw new Error('WASM module is not initialized');
		}

		const contextId = this._module.emscriptenRegisterAudioObject(audioContext);

		const runner = new this._module.WebApplicationRunner();
		runner.setup(contextId, canvasId);
		//this._module._createView(canvasId);
	}

	createView(canvasId: string) {
		console.assert(!!this._module, 'WASM module is not initialized');
		//this._module!._createView(canvasId);
	}
}
