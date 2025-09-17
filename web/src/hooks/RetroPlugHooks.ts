import { useCallback, useRef, useSyncExternalStore } from 'react';

import { useRetroPlug } from '../contexts/RetroPlugContext';
import type { MemoryAccessor } from '../native/RetroPlug';
import type { SystemId } from '../utils/NativeUtil';
import { Project } from '../wrapper/Project';
import { AccessType, MemoryType } from '../wrapper/System';

export function useIsProjectDirty(intervalTimeout: number = 100) {
	const { project } = useRetroPlug();

	const subscribe = useCallback(
		(listener: () => void) => {
			let lastValue = project.isDirty;

			const interval = setInterval(() => {
				const currentValue = project.isDirty;
				if (currentValue !== lastValue) {
					lastValue = currentValue;
					listener();
				}
			}, intervalTimeout);

			return () => clearInterval(interval);
		},
		[project, intervalTimeout],
	);

	const getSnapshot = useCallback(() => {
		return project.isDirty;
	}, [project]);

	return useSyncExternalStore(subscribe, getSnapshot);
}

export function useProject(intervalTimeout: number = 100) {
	const { app, module } = useRetroPlug();

	// Cache to avoid unnecessary Project creation
	const cache = useRef<{
		version: number | null;
		project: Project | null;
	}>({ version: null, project: null });

	const subscribe = useCallback(
		(listener: () => void) => {
			let lastVersion = app.nativeProject.version;

			const interval = setInterval(() => {
				const currentVersion = app.nativeProject.version;
				if (currentVersion !== lastVersion) {
					lastVersion = currentVersion;
					listener();
				}
			}, intervalTimeout);

			return () => clearInterval(interval);
		},
		[app, intervalTimeout],
	);

	const getSnapshot = useCallback(() => {
		const currentVersion = app.nativeProject.version;

		// Return cached Project if version hasn't changed
		if (cache.current.version === currentVersion && cache.current.project) {
			return cache.current.project;
		}

		// Create new Project only when version changes
		const newProject = new Project(module, app.nativeProject);
		cache.current = { version: currentVersion, project: newProject };

		return newProject;
	}, [app, module]);

	return useSyncExternalStore(subscribe, getSnapshot);
}

export function useSystemMemoryVersion(system: SystemId|null, memoryType: MemoryType, intervalTimeout: number = 100) {
	const { project } = useRetroPlug();

	const subscribe = useCallback(
		(listener: () => void) => {
			if (system === null) return () => {};

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
		[project, system, memoryType, intervalTimeout],
	);

	const getSnapshot = useCallback(() => {
		if (system === null) return null;
		return project.getSystemMemoryVersion(system, memoryType);
	}, [project, system, memoryType]);

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
		[project, system, memoryType, intervalTimeout],
	);

	const getSnapshot = useCallback(() => {
		const currentVersion = project.getSystemMemoryVersion(system, memoryType);
		if (cache.current.version === currentVersion && cache.current.memory) {
			return cache.current.memory;
		}

		const memory = project.getSystemMemory(system, memoryType, AccessType.Read);
		if (!memory || memory.getSize() === 0) return null;

		cache.current.version = currentVersion;
		cache.current.memory = memory;

		return memory;
	}, [project, system, memoryType]);

	return useSyncExternalStore(subscribe, getSnapshot);
}


export function useSystemKitVersion(system: SystemId, kitId: number) {
	const { project } = useRetroPlug();

	const subscribe = useCallback(
		(listener: () => void) => {
			let lastVersion = project.lsdj.getKitVersion(system, kitId);
			let animationFrameId: number;

			const checkForUpdates = () => {
				const currentVersion = project.lsdj.getKitVersion(system, kitId);
				if (currentVersion !== lastVersion) {
					lastVersion = currentVersion;
					listener();
				}
				animationFrameId = requestAnimationFrame(checkForUpdates);
			};

			animationFrameId = requestAnimationFrame(checkForUpdates);

			return () => {
				cancelAnimationFrame(animationFrameId);
			};
		},
		[project, system, kitId],
	);

	const getSnapshot = useCallback(() => {
		return project.lsdj.getKitVersion(system, kitId);
	}, [project, system, kitId]);

	return useSyncExternalStore(subscribe, getSnapshot);
}
