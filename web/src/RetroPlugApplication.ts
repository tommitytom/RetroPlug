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
	}

	setup(canvasId: string, audioContext: AudioContext) {
		if (!this._module) {
			throw new Error('WASM module is not initialized');
		}

		this._module.emscriptenRegisterAudioObject(audioContext);
		//this._module._createView(canvasId);
	}

	createView(canvasId: string) {
		console.assert(this._module, 'WASM module is not initialized');
		//this._module!._createView(canvasId);
	}
}
