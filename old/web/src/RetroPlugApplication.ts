import type {
	LsdjMemoryOffsets,
	MainModule,
	MemoryAccessor,
	NativeLsdjRom,
	NativeRetroPlugApplication,
	NativeRetroPlugProject,
	Uint8Buffer,
	WebApplicationRunner,
} from './native/RetroPlug.d.ts';
import { convertMemoryType } from './utils/NativeUtil.ts';
import { Project } from './wrapper/Project.ts';
import { MemoryType } from './wrapper/System.ts';

export class RetroPlugApplication {
	private _module: MainModule | null = null;
	private _runner: WebApplicationRunner | null = null;
	private _nativeApp: NativeRetroPlugApplication | null = null;
	private _nativeProject: NativeRetroPlugProject | null = null;
	private _worker: Worker | null = null;

	get module() {
		return this._module;
	}

	get runner(): WebApplicationRunner | null {
		return this._runner;
	}

	get project(): Project {
		const project = this.nativeProject;
		return new Project(this._module!, project);
	}

	get nativeApp() {
		return this._nativeApp;
	}

	get nativeProject(): NativeRetroPlugProject {
		return this._nativeProject!;
	}

	async load() {
		const moduleFactory = (await import('./native/RetroPlug.mjs')).default;

		this._module = (await moduleFactory({
			locateFile: (path: string) => {
				if (path.endsWith('.wasm')) {
					return '/RetroPlug.wasm';
				}
				return path;
			},
			print: (text: string) => {
				if (text.includes('[error]')) {
					console.error('WASM Error:', text);
				} else if (text.includes('[warning]')) {
					console.warn('WASM Warning:', text);
				} else {
					console.log('WASM:', text);
				}
			},
			printErr: (text: string) => {
				console.error('WASM Error:', text);
			},
		})) as MainModule;

		this._runner = new this._module.WebApplicationRunner();
		this._runner.setupFileSystem();
		this._nativeApp = this._module.upcastApplication(this._runner.getApplication());
		this._nativeProject = this._nativeApp!.getProject();

		this._worker = new Worker(new URL('./TickWorker.ts', import.meta.url));

		this._worker.addEventListener('message', () => this._runner?.runFrame());
		document.addEventListener('visibilitychange', () => {
			if (this._worker) {
				if (document.hidden) {
					this._worker.postMessage({ type: 'start', interval: 1000 / 60 });
				} else {
					this._worker.postMessage({ type: 'stop' });
				}
			}
		});

		return new Promise<void>((resolve) => {
			const interval = setInterval(() => {
				if (this._runner && this._runner.isFileSystemReady()) {
					console.log('File system is ready');
					this._nativeProject?.loadConfigs();
					clearInterval(interval);
					resolve();
				}
			}, 100);
		});
	}

	createAudioBuffer(channelCount: number, sampleCount: number, sampleRate: number) {
		return new this._module!.NativeAudioBuffer(channelCount, sampleCount, sampleRate);
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
		return new this._module!.NativeLsdjRom(accessor.getBuffer());
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

	setupAudio(audioContext: AudioContext) {
		if (!this._module || !this._runner) {
			throw new Error('WASM module is not initialized');
		}

		const contextId = this._module.emscriptenRegisterAudioObject(audioContext);
		this._runner.setupAudio(contextId, audioContext.sampleRate);
	}

	setupGraphics(canvasId: string) {
		if (!this._module || !this._runner) {
			throw new Error('WASM module is not initialized');
		}

		this._runner.setupGraphics(canvasId);
		this._runner.start();
	}

	createNamedView(name: string, canvasId: string) {
		if (!this._module || !this._runner) {
			throw new Error('WASM module is not initialized');
		}

		return this._runner.createNamedView(name, canvasId);
	}

	destroyGraphics() {
		if (!this._module || !this._runner) {
			throw new Error('WASM module is not initialized');
		}

		this._runner.stop();
		this._runner.destroyGraphics();
	}

	destroy() {
		//this._module?.destroy();
	}
}
