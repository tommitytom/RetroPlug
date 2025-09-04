import {
	useCallback,
	useEffect,
	useRef,
	useState,
	type ReactNode,
} from "react";

import { RetroPlugApplication } from "../RetroPlugApplication";
import { Project } from "../wrapper/Project";
import { RetroPlugContext } from "./RetroPlugContext";

export function RetroPlugProvider({ children }: { children: ReactNode }) {
	const audioContextRef = useRef<AudioContext | null>(null);
	const canvasIdRef = useRef<string | null>(null);
	const [app, setApp] = useState<RetroPlugApplication | null>(null);
	const [project, setProject] = useState<Project | null>(null);
	const [isLoading, setIsLoading] = useState<boolean>(true);
	const [isReady, setIsReady] = useState<boolean>(false);
	const [audioContextState, setAudioContextState] =
		useState<AudioContextState>("suspended");

	useEffect(() => {
		audioContextRef.current = new AudioContext();
		setAudioContextState(audioContextRef.current.state);

		document.addEventListener(
			"click",
			() => {
				if (
					audioContextRef.current &&
					audioContextRef.current.state === "suspended"
				) {
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

		audioContextRef.current.addEventListener(
			"statechange",
			handleAudioContextStateChange,
		);

		let mounted = true;

		const pendingApp = new RetroPlugApplication();

		pendingApp
			.load()
			.then(() => {
				if (mounted) {
					setIsLoading(false);
					try {
						pendingApp.setupAudio(audioContextRef.current);

						if (canvasIdRef.current) {
							try {
								// This function seems to return fine but seemingly throws an exception after
								// Investigate!
								pendingApp.setupGraphics(canvasIdRef.current);
							} catch (ex) {
								console.error("Error setting up graphics:", ex);
							}

							setIsReady(true);
						}
					} catch (ex) {
						console.error("Error setting up WASM module:", ex);
					}
					setApp(pendingApp);
					setProject(pendingApp.project);
				}
			})
			.catch((err) => {
				console.error("Error loading WASM module:", err);
			});

		return () => {
			mounted = false;

			setIsReady(false);
			setApp(null);
			setProject(null);
			pendingApp.destroy();

			if (audioContextRef.current) {
				audioContextRef.current.removeEventListener(
					"statechange",
					handleAudioContextStateChange,
				);
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
					console.error("Error setting up graphics:", ex);
				}

				setIsReady(true);
			}
		},
		[app],
	);

	return (
		<RetroPlugContext.Provider
			value={{
				app,
				project,
				audioContext: audioContextRef.current,
				isLoading,
				isReady,
				audioContextState,
				canvasId: canvasIdRef.current,
				setCanvasId,
			}}
		>
			{children}
		</RetroPlugContext.Provider>
	);
}
