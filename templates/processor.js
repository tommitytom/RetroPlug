/**
 * This is the JS side of the AudioWorklet processing that creates our
 * AudioWorkletProcessor that fetches the audio data from native code and
 * copies it into the output buffers.
 *
 * This is intentionally not made part of Emscripten AudioWorklet integration
 * because apps will usually want to a lot of control here (formats, channels,
 * additional processors etc.)
 */

// Register our audio processors if the code loads in an AudioWorkletGlobalScope
if (typeof AudioWorkletGlobalScope === 'function') {
	// This processor node is a simple proxy to the audio generator in native code.
	// It calls the native function then copies the samples into the output buffer
	class NativePassthroughProcessor extends AudioWorkletProcessor {
		constructor(options) {
			super();
			this.app = options.processorOptions.app;
		}

		process(inputs, outputs, parameters) {
			const output = outputs[0];
			const numSamples = output[0].length;

			// Run the native audio generator function
			const mem = Module['_generateAudio'](this.app, numSamples);

			// Copy the results into the output buffer, float-by-float deinterleaving the data
			let curSrc = mem/4;
			const chL = output[0];
			const chR = output[1];
			for (let s = 0; s < numSamples; ++s) {
				chL[s] = Module.HEAPF32[curSrc++];
				chR[s] = Module.HEAPF32[curSrc++];
			}

			return true;
		}
	}

	// Register the processor as per the audio worklet spec
	registerProcessor('native-passthrough-processor', NativePassthroughProcessor);
}

async function syncFs() {
	const prom = new Promise((resolve, reject) => {
		FS.syncfs(false, function (err) {
			if (err) {
				console.log('Failed to sync FS: ', err);
			} else {
				console.log('FS Synced!');
			}

			resolve();
		});
	});

	await prom;
}

async function setupFs() {
	FS.mkdir('/.file-dialog');
	FS.mkdir('/retroplug');
	FS.mount(IDBFS, {}, '/retroplug');

	const prom = new Promise((resolve, reject) => {
		FS.syncfs(true, function (err) {
			if (err) {
				console.log('Failed to sync FS: ', err);
			} else {
				console.log('FS Synced!');
			}

			resolve();
		});
	});

	await prom;
}

let fileDialogOpen = false;

async function saveFileDialog(fileName) {
	const path = UTF8ToString(fileName);
	let content = FS.readFile(path);

	const a = document.createElement("a");
	a.style.display = 'none';
	a.href = window.URL.createObjectURL(new Blob([content]), { type: 'application/octet-stream' });
	a.download = path.substring(path.lastIndexOf('/') + 1);

	document.body.appendChild(a);
	a.click();

	setTimeout(() => {
		window.URL.revokeObjectURL(a.href);
		a.remove();
	}, 2000);
}

async function openFileDialog(extensions, multiselect) {
	const input = document.createElement('input');
	input.type = 'file';
	input.accept = '.gb, .gbc, .sav, .rplg, .retroplug';
	input.multiple = true;

	let changed = false;

	const prom = new Promise((resolve, reject) => {
		input.onchange = async () => {
			changed = true;

			const files = Array.from(input.files);
			const paths = [];

			for (const file of files) {
				const fileData = await file.arrayBuffer();
				const dataView = new Uint8Array(fileData);
				const filePath = '/.file-dialog/' + file.name;

				FS.writeFile(filePath, dataView);
				paths.push(filePath);
			}

			const pathsConcat = paths.join(';');

			resolve(pathsConcat);
		};

		input.onblur = () => {
			console.log('blur')
			if (!changed) {
				resolve(null);
			}

			changed = false;
		};
	});

	input.click();

	return await prom;
}
