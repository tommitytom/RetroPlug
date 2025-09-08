import { useCallback, useRef, useSyncExternalStore } from "react";

import { useRetroPlug } from "../contexts/RetroPlugContext";
import { Project } from "../wrapper/Project";
import { AccessType, MemoryType } from "../wrapper/System";
import type { MemoryAccessor } from "../native/RetroPlug";
import type { SystemId } from "../utils/NativeUtil";

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

export function useSystemMemoryVersion(system: SystemId, memoryType: MemoryType, intervalTimeout: number = 100) {
	const { project } = useRetroPlug();

	const subscribe = useCallback(
		(listener: () => void) => {
			if (!project) return () => {};

			project.subscribeToMemory(system, memoryType);
			let lastVersion = project.getSystemMemoryVersion(system, memoryType);

			const interval = setInterval(() => {
				const currentVersion = project.getSystemMemoryVersion(system, memoryType);
				if (currentVersion !== lastVersion) {
					lastVersion = currentVersion;
					listener();
				}
			}, intervalTimeout);
			return () => {
				clearInterval(interval);
				project.unsubscribeFromMemory(system, memoryType);
			};
		},
		[system, memoryType, intervalTimeout],
	);

	const getSnapshot = useCallback(() => {
		if (!project) return 0;
		return project.getSystemMemoryVersion(system, memoryType);
	}, [system, memoryType]);

	return useSyncExternalStore(subscribe, getSnapshot);
}

export function useSystemMemory(system: SystemId, memoryType: MemoryType, intervalTimeout: number = 100) {
	const { project } = useRetroPlug();

	const cache = useRef<{
		version: number;
		memory?: MemoryAccessor;
	}>({ version: -1 });

	const subscribe = useCallback(
		(listener: () => void) => {
			if (!project) return () => {};

			project.subscribeToMemory(system, memoryType);
			let lastVersion = project.getSystemMemoryVersion(system, memoryType);

			const interval = setInterval(() => {
				const currentVersion = project.getSystemMemoryVersion(system, memoryType);
				if (currentVersion !== lastVersion) {
					lastVersion = currentVersion;
					listener();
				}
			}, intervalTimeout);

			return () => {
				clearInterval(interval);
				project.unsubscribeFromMemory(system, memoryType);
			};
		},
		[system, memoryType, intervalTimeout],
	);

	const getSnapshot = useCallback(() => {
		if (!project) return null;

		const currentVersion = project.getSystemMemoryVersion(system, memoryType);
		if (cache.current.version === currentVersion && cache.current.memory) {
			return cache.current.memory;
		}

		const memory = project.getSystemMemory(system, memoryType, AccessType.Read);
		if (!memory || memory.getSize() === 0) return null;

		cache.current.version = currentVersion;
		cache.current.memory = memory;

		return memory;
	}, [system, memoryType]);

	return useSyncExternalStore(subscribe, getSnapshot);
}
