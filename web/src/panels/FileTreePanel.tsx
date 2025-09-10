import React, { useCallback } from "react";

import { FileExplorer } from '../components/FileExplorer';
import { ContextMenu } from '../components/Menu/ContextMenu';
import { useProject } from "../hooks/RetroPlugHooks";
import { useOPFSStore } from "../stores/FileSystemStore";
import { useContextMenu } from "../hooks/useContextMenu";
import { downloadArrayBuffer } from "../utils/FileUtil";
import type { MenuItem } from "../components/Menu/types";
import { useRetroPlug } from "../contexts/RetroPlugContext";
import { useDocument } from "../contexts/DocumentContext";
import { useLayoutControl } from "../App";
import type { FileSystemNode } from "../filesystem/types";

interface IComponent {
	type: number;
	name: string;
	data: any;
}

interface ITypedComponent<T> extends IComponent {
	data: T;
}

interface ISystem {
	components: IComponent[];
}

interface IProject {
	systems: ISystem[];
}

interface ISystemLoadComponent {
	entries: Record<string, { path?: string, data: Uint8Array }>;
}

function findComponent<T>(project: IProject, componentName: string): T | undefined {
	for (const system of project.systems) {
		const component = system.components.find(c => c.name === componentName);
		if (component) {
			return component.data as T;
		}
	}
	return undefined;
}

export const FileTreePanel: React.FC = () => {
	const { focusCanvas } = useRetroPlug();
	const { openDocument } = useDocument();
	const { switchToCenterPanel } = useLayoutControl();
	const project = useProject();
	const { isVisible, position, items, showContextMenu, hideContextMenu, handleItemClick } = useContextMenu();
	const { readPath, fileExists, writePath, deletePath, createDirectory } = useOPFSStore();

	const handleFileOpen = useCallback(async (node: FileSystemNode) => {
		project.loadFromPaths([node.path])

		console.log('Opening file:', node.path);

		if (node.path.endsWith('.rplg')) {
			project.reset();
			project.loadFromFile(node.path);
			focusCanvas();
		}

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

		const entries: Record<string, { path?: string }> = {};

		entries.rom = {
			path: romPath,
		};

		if (savPath) {
			entries.sram = {
				path: savPath,
			};
		}

		project.reset();
		project.addSystem({ entries });
		focusCanvas();
	}, [project]);

	const handleContextMenu = useCallback((node: FileSystemNode|null, event: React.MouseEvent) => {
		event.preventDefault();
		event.stopPropagation();

		const menuItems: MenuItem[] = [];
		if (node) {
			menuItems.push({
				id: '1',
				label: 'Open',
				disabled: false,
				onClick: () => {
					console.log('open');
				}
			}, {
				id: '2',
				label: 'Delete',
				disabled: false,
				onClick: async () => {
					// TODO: Confirm delete?
					await deletePath(node.path);
				}
			}, {
				id: '3',
				label: 'Download',
				disabled: false,
				onClick: async () => {
					console.log('click!');

					const data = await readPath(node.path);
					downloadArrayBuffer(data, node.name);
				}
			}, {
				id: '4',
				label: 'Edit',
				disabled: false,
				onClick: async () => {
					const data = await readPath(node.path);
					const decoder = new TextDecoder();
					const content = decoder.decode(data);

					// Open the document in the text editor
					openDocument(node.path, content, node.name);

					// Switch to the text editor panel in the center
					switchToCenterPanel('textEditor');
				}
			});
		} else {
			menuItems.push({
				id: '1',
				label: 'Create Folder',
				disabled: false,
				onClick: () => {
					console.log('create folder');
				}
			});
		}

		showContextMenu(event, menuItems);
	}, [project, readPath]);

	return <div className="h-full w-full">
		<FileExplorer
			onFileOpen={handleFileOpen}
			onContextMenu={handleContextMenu}
		/>
		<ContextMenu
			items={items}
			position={position}
			visible={isVisible}
			onClose={hideContextMenu}
			onItemClick={handleItemClick}
		/>
	</div>
};
