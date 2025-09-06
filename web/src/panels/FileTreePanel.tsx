import React, { useCallback } from "react";

import { FileExplorer } from '../components/FileExplorer';
import { useProject } from "../hooks/RetroPlugHooks";
import { useOPFSStore } from "../stores/FileSystemStore";
import type { FileSystemNode } from "../stores/types";

export const FileTreePanel: React.FC = () => {
	const project = useProject();
	const { readPath, fileExists } = useOPFSStore();

	const handleFileOpen = useCallback(async (node: FileSystemNode) => {
		if (!project) return;

		let romPath: string|undefined;
		let savPath: string|undefined;

		if (node.name.endsWith('.gb')) {
			romPath = node.path;
			const pairedSavPath = node.path.replace(/\.gb$/i, '.sav');

			if (await fileExists(pairedSavPath)) {
				savPath = pairedSavPath;
			}
		} else if (node.name.endsWith('.sav')) {
			savPath = node.path;
			const pairedRomPath = node.path.replace(/\.sav$/i, '.gb');

			if (await fileExists(pairedRomPath)) {
				romPath = pairedRomPath;
			}
		}

		if (!romPath) {
			return;
		}

		const entries: Record<string, { path?: string, data: Uint8Array }> = {};

		entries.rom = {
			path: romPath,
			data: new Uint8Array(await readPath(romPath))
		};

		if (savPath) {
			entries.sram = {
				path: savPath,
				data: new Uint8Array(await readPath(savPath))
			};
		}

		project.addSystem({ entries });
	}, [project]);

	return <div className="h-full w-full">
		<FileExplorer
			onFileOpen={handleFileOpen}
		/>
	</div>
};
