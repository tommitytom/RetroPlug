import React, { useCallback } from "react";

import { FileExplorer } from '../components/FileExplorer';
import { ContextMenu } from '../components/Menu/ContextMenu';
import { useProject } from "../hooks/RetroPlugHooks";
import { useOPFSStore } from "../stores/FileSystemStore";
import { useContextMenu } from "../hooks/useContextMenu";
import { downloadArrayBuffer } from "../utils/FileUtil";
import type { MenuItem } from "../components/Menu/types";
import { useRetroPlug } from "../contexts/RetroPlugContext";
import type { FileSystemNode } from "../filesystem/types";
import { useDocument } from "../contexts/DocumentContext";

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
	const { setCurrentDocument } = useDocument();
	const project = useProject();
	const { isVisible, position, items, showContextMenu, hideContextMenu, handleItemClick } = useContextMenu();
	const { readPath, fileExists, writePath, deletePath, createDirectory } = useOPFSStore();

	const handleFileOpen = useCallback(async (node: FileSystemNode) => {
		setCurrentDocument({
			id: node.path,
			title: node.name,
			content: project,
			type: 'emulator',
			isDirty: false,
		})

		project.loadFromPaths([node.path])
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
					//openDocument(node.path, content, node.name);

					// Switch to the text editor panel in the center
					//switchToCenterPanel('textEditor');
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
