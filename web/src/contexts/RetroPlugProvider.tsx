import { useCallback, useEffect, useRef, useState, type ReactNode } from 'react';
import * as Comlink from 'comlink';

import { RetroPlugApplication } from '../RetroPlugApplication';
import { Project } from '../wrapper/Project';
import { RetroPlugContext } from './RetroPlugContext';
import type { FileSystemWorkerAPI } from '../filesystem/FileSystemWorker';

function createFileSystemWorker(): Comlink.Remote<FileSystemWorkerAPI> {
	const worker = new Worker(new URL('../filesystem/FileSystemWorker.ts', import.meta.url), { type: 'module' });
	return Comlink.wrap<FileSystemWorkerAPI>(worker);
}

export function RetroPlugProvider({ children }: { children: ReactNode }) {
	const audioContextRef = useRef<AudioContext | null>(null);
	const canvasIdRef = useRef<string | null>(null);
	const fileSystemRef = useRef<Comlink.Remote<FileSystemWorkerAPI> | null>(null);
	const [app, setApp] = useState<RetroPlugApplication | null>(null);
	const [project, setProject] = useState<Project | null>(null);
	const [isLoading, setIsLoading] = useState<boolean>(true);
	const [isReady, setIsReady] = useState<boolean>(false);
	const [audioContextState, setAudioContextState] = useState<AudioContextState>('suspended');

	useEffect(() => {
		audioContextRef.current = new AudioContext();
		setAudioContextState(audioContextRef.current.state);

		document.addEventListener(
			'click',
			() => {
				if (audioContextRef.current && audioContextRef.current.state === 'suspended') {
					audioContextRef.current.resume();
				}
			},
			{ once: true },
		);

		const handleAudioContextStateChange = () => {
			if (audioContextRef.current) {
				setAudioContextState(audioContextRef.current.state);
			}
		};

		audioContextRef.current.addEventListener('statechange', handleAudioContextStateChange);

		let mounted = true;

		const pendingApp = new RetroPlugApplication();
		const pendingFileSystem = createFileSystemWorker();
		const proms = [pendingApp.load(), pendingFileSystem.initialize()];

		Promise.all(proms)
			.then(() => {
				if (mounted) {
					try {
						pendingApp.setupAudio(audioContextRef.current);

						if (canvasIdRef.current) {
							try {
								// This function seems to return fine but seemingly throws an exception after
								// Investigate!
								pendingApp.setupGraphics(canvasIdRef.current);
							} catch (ex) {
								console.error('Error setting up graphics:', ex);
							}

							setIsReady(true);
						}
					} catch (ex) {
						console.error('Error setting up WASM module:', ex);
					}

					setApp(pendingApp);
					setProject(pendingApp.project);
					fileSystemRef.current = pendingFileSystem;
					setIsLoading(false);
				}
			})
			.catch((err) => {
				console.error('Error loading WASM module:', err);
			});

		return () => {
			mounted = false;

			setIsLoading(true);
			setIsReady(false);
			setApp(null);
			setProject(null);
			fileSystemRef.current = null;
			pendingApp.destroy();

			if (audioContextRef.current) {
				audioContextRef.current.removeEventListener('statechange', handleAudioContextStateChange);
				audioContextRef.current.close();
				audioContextRef.current = null;
			}
		};
	}, []);

	const setCanvasId = useCallback(
		(id: string | null) => {
			if (app) {
				app.destroyGraphics();
				setIsReady(false);
			}

			canvasIdRef.current = id;

			if (app && id !== null) {
				try {
					// This function seems to return fine but seemingly throws an exception after
					// Investigate!
					app.setupGraphics(id);
				} catch (ex) {
					console.error('Error setting up graphics:', ex);
				}

				setIsReady(true);
			}
		},
		[app],
	);

	const focusCanvas = useCallback(() => {
		const canvas = canvasIdRef.current && document.getElementById(canvasIdRef.current);
		if (!canvas) return;

		// Focus first
		canvas.focus();

		// Simulate a click to wake up GLFW
		const clickEvent = new MouseEvent('click', {
			bubbles: true,
			cancelable: true,
			view: window,
		});
		canvas.dispatchEvent(clickEvent);
	}, []);

	return (
		<RetroPlugContext.Provider
			value={{
				app: app!,
				project: project!,
				fileSystem: fileSystemRef.current!,
				audioContext: audioContextRef.current,
				isLoading,
				isReady,
				audioContextState,
				canvasId: canvasIdRef.current,
				setCanvasId,
				focusCanvas,
			}}
		>
			{children}
		</RetroPlugContext.Provider>
	);
}
