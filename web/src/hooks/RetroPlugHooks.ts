import { useCallback, useRef, useSyncExternalStore } from "react";
import { useRetroPlug } from "../contexts/RetroPlugContext";
import { Project, SystemId } from "../wrapper/Project";

export function useProject(intervalTimeout: number = 100) {
	const { app, isReady } = useRetroPlug();

	// Cache to avoid unnecessary Project creation
	const cache = useRef<{
		version: number | null;
		project: Project | null;
	}>({ version: null, project: null });

	const subscribe = useCallback(
		(listener: () => void) => {
			if (!app || !isReady) return () => {};

			let lastVersion = app.nativeProject.version;

			const interval = setInterval(() => {
				const currentVersion = app.nativeProject.version;
				// Only notify if version actually changed
				if (currentVersion !== lastVersion) {
					lastVersion = currentVersion;
					listener();
				}
			}, intervalTimeout);

			return () => clearInterval(interval);
		},
		[app, isReady, intervalTimeout],
	);

	const getSnapshot = useCallback(() => {
		if (!app || !app.module || !isReady) {
			return null;
		}

		const currentVersion = app.nativeProject.version;

		// Return cached Project if version hasn't changed
		if (cache.current.version === currentVersion && cache.current.project) {
			return cache.current.project;
		}

		// Create new Project only when version changes
		const newProject = new Project(app.module, app.nativeProject);
		cache.current = { version: currentVersion, project: newProject };

		return newProject;
	}, [app, isReady]);

	return useSyncExternalStore(subscribe, getSnapshot);
}

function useSystem(project: Project|null, systemId: SystemId) {
	if (!project || systemId >= project.systemCount) return null;
	const nativeSystem = project.getNativeSystem();

	return useSyncExternalStore(subscribe, getSnapshot);
}
