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
import { useModal } from "../contexts/ModalContext";

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
	const { openConfirm } = useModal();
	const project = useProject();
	const { isVisible, position, items, showContextMenu, hideContextMenu, handleItemClick } = useContextMenu();
	const { readPath, fileExists, writePath, deletePath, createDirectory, rootNode, refreshNode, movePath } = useOPFSStore();
	const [editingNodeId, setEditingNodeId] = React.useState<string | null>(null);

	const generateUniqueName = useCallback(async (parentPath: string, baseName: string, isFolder: boolean = false) => {
		const extension = isFolder ? '' : '.txt';
		let counter = 0;
		let name = counter === 0 ? baseName + extension : `${baseName} ${counter}${extension}`;

		// First check if the base name is available
		const fullPath = `${parentPath === '/' ? '' : parentPath}/${name}`;

		if (!(await fileExists(fullPath))) {
			return name;
		}

		// If base name exists, try numbered versions
		counter = 1;
		do {
			name = `${baseName} ${counter}${extension}`;
			const numberedPath = `${parentPath === '/' ? '' : parentPath}/${name}`;

			if (!(await fileExists(numberedPath))) {
				return name;
			}

			counter++;
		} while (counter < 100); // Safety limit

		// Fallback with timestamp if we hit the limit
		return `${baseName}-${Date.now()}${extension}`;
	}, [fileExists]);

	const ALLOWED_EXTENSIONS = new Set(['gb','gbc','sav','rplg','state']);

	const handleFileOpen = useCallback(async (node: FileSystemNode) => {
		if (node.type === 'directory') {
			// Ignore directories
			return;
		}

		const ext = node.path.split('.').pop()?.toLowerCase() || '';
		if (!ALLOWED_EXTENSIONS.has(ext)) {
			return;
		}

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

	const handleCreateNewFile = useCallback(async (parentPath: string, name: string) => {
		const fullPath = parentPath === '/' ? `/${name}` : `${parentPath}/${name}`;
		try {
			// Create empty file
			await writePath(fullPath, new ArrayBuffer(0));
		} catch (error) {
			console.error('Failed to create file:', error);
			throw error;
		}
	}, [writePath]);

	const handleCreateNewFolder = useCallback(async (parentPath: string, name: string) => {
		const fullPath = parentPath === '/' ? `/${name}` : `${parentPath}/${name}`;
		try {
			await createDirectory(fullPath);
		} catch (error) {
			console.error('Failed to create folder:', error);
			throw error;
		}
	}, [createDirectory]);

	const handleRename = useCallback(async (oldPath: string, newName: string) => {
		// Normalize paths to avoid issues with empty segments or trailing slashes
		const normalizedOldPath = oldPath.replace(/\/+/g, '/').replace(/\/$/, '') || '/';
		const pathParts = normalizedOldPath.split('/').filter(Boolean);
		const oldName = pathParts[pathParts.length - 1];

		// Don't rename if the name hasn't actually changed
		if (oldName === newName) {
			return;
		}

		// Sanitize the new name to avoid path issues
		const sanitizedNewName = newName.replace(/[\/\\]/g, '');
		if (!sanitizedNewName.trim()) {
			throw new Error('Invalid file name');
		}

		pathParts[pathParts.length - 1] = sanitizedNewName;
		const newPath = '/' + pathParts.join('/');

		try {
			// Check if the target path already exists to avoid conflicts
			if (await fileExists(newPath)) {
				throw new Error(`A file or folder with the name "${sanitizedNewName}" already exists.`);
			}

			// Now that we've fixed the FileSystemWorker bug, we can use movePath directly
			await movePath(normalizedOldPath, newPath);
		} catch (error) {
			console.error('Failed to rename:', error);
			throw error;
		}
	}, [fileExists, movePath]);	const handleStartRename = useCallback((nodeId: string) => {
		setEditingNodeId(nodeId);
	}, []);

	const handleCancelRename = useCallback(() => {
		setEditingNodeId(null);
	}, []);

	// Helper to find node by path after creation
	const findAndStartEditing = useCallback(async (parentPath: string, fileName: string) => {
		// Refresh the parent node to get the updated tree
		await refreshNode(parentPath);

		// Retry finding the node several times to account for state update delays
		const fullPath = parentPath === '/' ? `/${fileName}` : `${parentPath}/${fileName}`;
		let attempts = 0;
		const maxAttempts = 10;

		while (attempts < maxAttempts) {
			await new Promise(resolve => setTimeout(resolve, 50));

			const { getNodeByPath } = useOPFSStore.getState();
			const newNode = getNodeByPath(fullPath);

			if (newNode) {
				setEditingNodeId(newNode.id);
				return;
			}

			attempts++;
		}

		console.warn('Could not find newly created node after', maxAttempts, 'attempts:', fullPath);
	}, [refreshNode]);

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
					openConfirm({
						message: `Are you sure you want to delete "${node.name}"? This action cannot be undone.`,
						danger: true,
						onConfirm: async () => {
							await deletePath(node.path);
						}
					});
					// TODO: Confirm delete?
					//await deletePath(node.path);
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
				label: 'Rename',
				disabled: false,
				onClick: () => {
					handleStartRename(node.id);
				}
			}, {
				id: '5',
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
				onClick: async () => {
					const parentPath = rootNode?.path || '/';

					try {
						// Generate a unique folder name
						const folderName = await generateUniqueName(parentPath, 'New Folder', true);

						await handleCreateNewFolder(parentPath, folderName);

						// Find the new node and start editing
						await findAndStartEditing(parentPath, folderName);
					} catch (error) {
						console.error('Failed to create folder:', error);
					}
				}
			}, {
				id: '2',
				label: 'Create Text file',
				disabled: false,
				onClick: async () => {
					const parentPath = rootNode?.path || '/';

					try {
						// Generate a unique file name
						const fileName = await generateUniqueName(parentPath, 'new-file', false);

						await handleCreateNewFile(parentPath, fileName);

						// Find the new node and start editing
						await findAndStartEditing(parentPath, fileName);
					} catch (error) {
						console.error('Failed to create file:', error);
					}
				}
			});
		}

		showContextMenu(event, menuItems);
	}, [project, readPath]);

	return <div className="h-full w-full">
		<FileExplorer
			onFileOpen={handleFileOpen}
			onContextMenu={handleContextMenu}
			onCreateNewFile={handleCreateNewFile}
			onCreateNewFolder={handleCreateNewFolder}
			onRename={handleRename}
			editingNodeId={editingNodeId}
			onStartRename={handleStartRename}
			onCancelRename={handleCancelRename}
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
