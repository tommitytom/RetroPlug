import { useCallback, useRef, useSyncExternalStore } from "react";

import { useRetroPlug } from "../contexts/RetroPlugContext";
import { Project, type SystemId } from "../wrapper/Project";
import { MemoryType, System } from "../wrapper/System";

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

export function useSystem(
	project: Project | null,
	systemId: SystemId,
	intervalTimeout: number = 100,
) {
	// Cache to avoid unnecessary System creation
	const cache = useRef<{
		version: number | null;
		system: System | null;
	}>({ version: null, system: null });

	const subscribe = useCallback(
		(listener: () => void) => {
			if (!project || systemId < 0 || systemId >= project.systemCount) return () => {};

			let nativeSystem;
			try {
				nativeSystem = project.getNativeSystemByIndex(systemId);
				if (!nativeSystem) return () => {};
			} catch {
				return () => {};
			}

			let lastVersion = nativeSystem.version;
			const interval = setInterval(() => {
				const currentVersion = nativeSystem.version;
				if (currentVersion !== lastVersion) {
					lastVersion = currentVersion;
					listener();
				}
			}, intervalTimeout);
			return () => clearInterval(interval);
		},
		[project, systemId, intervalTimeout],
	);

	const getSnapshot = useCallback(() => {
		if (!project || systemId < 0 || systemId >= project.systemCount) return null;

		let nativeSystem;
		try {
			nativeSystem = project?.getNativeSystemByIndex(systemId);
			if (!nativeSystem) return null;
		} catch {
			return null;
		}

		const currentVersion = nativeSystem.version;
		if (cache.current.version === currentVersion && cache.current.system) {
			return cache.current.system;
		}

		const newSystem = project.getSystemByIndex(systemId);
		cache.current = { version: currentVersion, system: newSystem };

		return newSystem;
	}, [project, systemId]);

	return useSyncExternalStore(subscribe, getSnapshot);
}

export function useSystemMemoryHash(system: System|null, memoryType: MemoryType, intervalTimeout: number = 100) {
	const subscribe = useCallback(
		(listener: () => void) => {
			if (!system) return () => {};

			let lastVersion = system.stateHashes[memoryType];
			const interval = setInterval(() => {
				const currentVersion = system.stateHashes[memoryType];
				if (currentVersion !== lastVersion) {
					lastVersion = currentVersion;
					listener();
				}
			}, intervalTimeout);
			return () => clearInterval(interval);
		},
		[system, memoryType, intervalTimeout],
	);

	const getSnapshot = useCallback(() => {
		if (!system) return 0;
		return system.stateHashes[memoryType];
	}, [system, memoryType]);

	return useSyncExternalStore(subscribe, getSnapshot);
}