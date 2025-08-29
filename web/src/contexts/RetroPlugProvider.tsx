import { useEffect, useRef, useState, type ReactNode } from "react";

import { RetroPlugContext } from "./RetroPlugContext";
import { RetroPlugApplication } from "../RetroPlugApplication";

export function RetroPlugProvider({ children }: { children: ReactNode }) {
	const audioContextRef = useRef<AudioContext | null>(null);
	const canvasIdRef = useRef<string | null>(null);
	const [app, setApp] = useState<RetroPlugApplication | null>(null);
	const [isLoading, setIsLoading] = useState<boolean>(true);
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
			{ once: true }
		);

		const handleAudioContextStateChange = () => {
			if (audioContextRef.current) {
				setAudioContextState(audioContextRef.current.state);
			}
		};

		audioContextRef.current.addEventListener(
			"statechange",
			handleAudioContextStateChange
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
							pendingApp.setupGraphics(canvasIdRef.current);
						}
					} catch (ex) {
						console.error("Error setting up WASM module:", ex);
					}
					setApp(pendingApp);
				}
			})
			.catch((err) => {
				console.error("Error loading WASM module:", err);
			});

		return () => {
			mounted = false;

			setApp(null);
			pendingApp.destroy();

			if (audioContextRef.current) {
				audioContextRef.current.removeEventListener(
					"statechange",
					handleAudioContextStateChange
				);
				audioContextRef.current.close();
				audioContextRef.current = null;
			}
		};
	}, []);

	const setCanvasId = (id: string | null) => {
		if (app) {
			app.destroyGraphics();
		}

		canvasIdRef.current = id;

		if (app && id !== null) {
			app.setupGraphics(id);
		}
	};

	return (
		<RetroPlugContext.Provider
			value={{
				app,
				audioContext: audioContextRef.current,
				isLoading,
				audioContextState,
				canvasId: canvasIdRef.current,
				setCanvasId,
			}}
		>
			{children}
		</RetroPlugContext.Provider>
	);
}
