import * as Comlink from 'comlink';
import { createContext, useContext } from 'react';

import type { FileSystemWorkerAPI } from '../filesystem/FileSystemWorker';
import type { MainModule } from '../native/RetroPlug';
import { RetroPlugApplication } from '../RetroPlugApplication';
import { Project } from '../wrapper/Project';

interface RetroPlugContextType {
	canvasId: string | null;
	setCanvasId: (entityId: string | null) => void;
	focusCanvas: () => void;
	fileSystem: Comlink.Remote<FileSystemWorkerAPI>;
	isLoading: boolean;
	audioContext: AudioContext | null;
	audioContextState: AudioContextState;
	module: MainModule;
	app: RetroPlugApplication;
	project: Project;
}

export const RetroPlugContext = createContext<RetroPlugContextType | undefined>(undefined);

export function useRetroPlug() {
	const context = useContext(RetroPlugContext);
	if (context === undefined) {
		throw new Error('useRetroPlug must be used within an RetroPlugProvider');
	}
	return context;
}
