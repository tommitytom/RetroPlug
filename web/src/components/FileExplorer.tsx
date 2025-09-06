import { useCallback, useEffect, useRef, useState } from 'react';

import { useOPFSStore } from '../stores/FileSystemStore';
import type { FileSystemNode } from '../stores/types';

interface TreeNodeProps {
	node: FileSystemNode;
	level: number;
	onNodeClick: (node: FileSystemNode, event: React.MouseEvent) => void;
	onNodeDoubleClick: (node: FileSystemNode) => void;
	onToggleExpand: (nodeId: string) => void;
	selectedNodes: Set<string>;
	expandedNodes: Set<string>;
	dragOverNode: string | null;
	isFocused: boolean;
	onDragStart?: (event: React.DragEvent, node: FileSystemNode) => void;
	onDrop?: (event: React.DragEvent, targetNode: FileSystemNode) => void;
	onDragOver?: (event: React.DragEvent) => void;
	onDragEnter?: (event: React.DragEvent, node: FileSystemNode) => void;
	onDragLeave?: (event: React.DragEvent) => void;
}

function TreeNode({
	node,
	level,
	onNodeClick,
	onNodeDoubleClick,
	onToggleExpand,
	selectedNodes,
	expandedNodes,
	dragOverNode,
	isFocused,
	onDragStart,
	onDrop,
	onDragOver,
	onDragEnter,
	onDragLeave,
}: TreeNodeProps) {
	const isSelected = selectedNodes.has(node.id);
	const isExpanded = expandedNodes.has(node.id);
	const isDragOver = dragOverNode === node.id;

	const handleClick = (event: React.MouseEvent) => {
		onNodeClick(node, event);
	};

	const handleDoubleClick = () => {
		onNodeDoubleClick(node);
	};

	const handleToggleExpand = (event: React.MouseEvent) => {
		event.stopPropagation();
		onToggleExpand(node.id);
	};

	const getIcon = () => {
		if (node.type === 'directory') {
			return isExpanded ? '📁' : '📂';
		} else if (node.type === 'archive') {
			return isExpanded ? '📦' : '🗃️';
		} else {
			// File icons based on extension
			const ext = node.name.split('.').pop()?.toLowerCase();
			switch (ext) {
				case 'js':
				case 'ts':
				case 'jsx':
				case 'tsx':
					return '📄';
				case 'json':
					return '📋';
				case 'md':
				case 'txt':
					return '📝';
				case 'png':
				case 'jpg':
				case 'jpeg':
				case 'gif':
				case 'svg':
					return '🖼️';
				case 'mp3':
				case 'wav':
				case 'ogg':
					return '🎵';
				case 'zip':
				case 'tar':
				case 'gz':
					return '📦';
				default:
					return '📄';
			}
		}
	};

	const formatSize = (size: number | undefined) => {
		if (!size) return '';
		if (size < 1024) return `${size}B`;
		if (size < 1024 * 1024) return `${(size / 1024).toFixed(1)}KB`;
		return `${(size / (1024 * 1024)).toFixed(1)}MB`;
	};

	const getPaddingClass = (level: number) => {
		const paddingMap = {
			0: 'pl-2',
			1: 'pl-6',
			2: 'pl-10',
			3: 'pl-14',
			4: 'pl-18',
			5: 'pl-22',
		};
		return paddingMap[Math.min(level, 5) as keyof typeof paddingMap] || 'pl-22';
	};

	const getSelectionClass = () => {
		if (!isSelected) return '';
		return isFocused ? 'bg-blue-600/30 text-blue-300' : 'bg-gray-600/30 text-gray-300';
	};

	return (
		<>
			<div
				className={`flex cursor-pointer items-center py-1 text-sm text-gray-300 transition-colors duration-200 hover:bg-gray-700 hover:text-white ${getSelectionClass()} ${isDragOver ? 'border border-dashed border-blue-500 bg-blue-500/30' : ''} ${
					node.type === 'archive' ? 'border-l-2 border-purple-400/40 hover:border-purple-400/60' : ''
				} ${getPaddingClass(level)}`}
				onClick={handleClick}
				onDoubleClick={handleDoubleClick}
				onContextMenu={(e) => {
					e.preventDefault();
					onNodeClick(node, e as unknown as React.MouseEvent);
				}}
				draggable={true}
				onDragStart={(e) => {
					onDragStart?.(e, node);
					e.currentTarget.classList.add('opacity-50');
				}}
				onDrop={(e) => onDrop?.(e, node)}
				onDragOver={onDragOver}
				onDragEnter={(e) => onDragEnter?.(e, node)}
				onDragLeave={onDragLeave}
				onDragEnd={(e) => {
					e.currentTarget.classList.remove('opacity-50');
				}}
			>
				{(node.type === 'directory' || node.type === 'archive') && (
					<span className="mr-1 cursor-pointer text-xs text-white" onClick={handleToggleExpand}>
						{isExpanded ? '▼' : '▶'}
					</span>
				)}
				<span className="mr-2 text-xs text-white">{getIcon()}</span>
				<span className="flex-1 font-mono">{node.name}</span>
			</div>
			{(node.type === 'directory' || node.type === 'archive') && isExpanded && node.children && (
				<>
					{node.children.map((child) => (
						<TreeNode
							key={child.id}
							node={child}
							level={level + 1}
							onNodeClick={onNodeClick}
							onNodeDoubleClick={onNodeDoubleClick}
							onToggleExpand={onToggleExpand}
							selectedNodes={selectedNodes}
							expandedNodes={expandedNodes}
							dragOverNode={dragOverNode}
							isFocused={isFocused}
							onDragStart={onDragStart}
							onDrop={onDrop}
							onDragOver={onDragOver}
							onDragEnter={onDragEnter}
							onDragLeave={onDragLeave}
						/>
					))}
				</>
			)}
		</>
	);
}

interface FileExplorerProps {
	onFileOpen?: (node: FileSystemNode) => void;
	onFileSelect?: (nodes: FileSystemNode[], isMultiSelect: boolean) => void;
	onFileMove?: (sourceNode: FileSystemNode, targetNode: FileSystemNode) => Promise<void>;
	onFileCopy?: (sourceNode: FileSystemNode, targetNode: FileSystemNode) => Promise<void>;
	onFileDelete?: (nodes: FileSystemNode[]) => Promise<void>;
	onFileUpload?: (files: File[], targetNode: FileSystemNode) => Promise<void>;
	onDirectoryCreate?: (parentNode: FileSystemNode, name: string) => Promise<void>;
	onDirectoryExpand?: (node: FileSystemNode) => Promise<void>;
	onDirectoryCollapse?: (node: FileSystemNode) => void;
	onContextMenu?: (node: FileSystemNode | null, event: React.MouseEvent) => void;
	onDragStart?: (node: FileSystemNode, event: React.DragEvent) => void;
	onDragEnd?: (node: FileSystemNode, event: React.DragEvent) => void;
	onError?: (error: string, operation?: string) => void;
}

export function FileExplorer({
	onFileOpen,
	onFileSelect,
	onFileMove,
	onFileCopy,
	onFileDelete,
	onFileUpload,
	onDirectoryCreate,
	onDirectoryExpand,
	onDirectoryCollapse,
	onContextMenu,
	onDragStart,
	onDragEnd,
	onError,
}: FileExplorerProps = {}) {
	const {
		rootNode,
		selectedNodes,
		expandedNodes,
		loading,
		error,
		initialize,
		toggleNode,
		selectNode,
		clearSelection,
		listPath,
		writePath,
		createDirectory,
		movePath,
		copyPath,
		deletePath,
		refreshNode,
		registerArchiveHandler,
	} = useOPFSStore();

	const [draggedNode, setDraggedNode] = useState<FileSystemNode | null>(null);
	const [dragOverNode, setDragOverNode] = useState<string | null>(null);
	const [isDragOverContainer, setIsDragOverContainer] = useState(false);
	const [isFocused, setIsFocused] = useState(false);
	const containerRef = useRef<HTMLDivElement>(null);

	useEffect(() => {
		initialize().then(() => {
			//registerArchiveHandler(new ZipArchiveHandler())
			// Register other archive handlers as needed
		});
	}, [initialize]);

	// Load children when a directory or archive is expanded
	const handleToggleExpand = useCallback(
		async (nodeId: string) => {
			const node = findNodeById(rootNode, nodeId);
			if (!node) return;

			const wasExpanded = expandedNodes.has(nodeId);

			if (wasExpanded && onDirectoryCollapse) {
				onDirectoryCollapse(node);
			}

			toggleNode(nodeId);

			// If expanding and no children loaded yet, load them
			if (!wasExpanded && (node.type === 'directory' || node.type === 'archive') && !node.children) {
				try {
					if (onDirectoryExpand) {
						await onDirectoryExpand(node);
					} else {
						await refreshNode(node.path);
					}
				} catch (error) {
					const errorMessage = `Failed to load directory/archive children: ${error}`;
					console.error(errorMessage);
					if (onError) {
						onError(errorMessage, 'expand');
					}
				}
			} else if (!wasExpanded && onDirectoryExpand) {
				try {
					await onDirectoryExpand(node);
				} catch (error) {
					const errorMessage = `Failed to expand directory: ${error}`;
					console.error(errorMessage);
					if (onError) {
						onError(errorMessage, 'expand');
					}
				}
			}
		},
		[rootNode, expandedNodes, toggleNode, refreshNode, onDirectoryExpand, onDirectoryCollapse, onError],
	);

	const findNodeById = (root: FileSystemNode | null, id: string): FileSystemNode | null => {
		if (!root) return null;
		if (root.id === id) return root;
		if (root.children) {
			for (const child of root.children) {
				const found = findNodeById(child, id);
				if (found) return found;
			}
		}
		return null;
	};

	const handleClick = useCallback(
		(event: React.MouseEvent) => {
			if (event.button === 2 && onContextMenu) {
				event.preventDefault();
				onContextMenu(null, event);
			} else {
				clearSelection();
			}
		},
		[clearSelection],
	);

	const handleNodeClick = useCallback(
		(node: FileSystemNode, event: React.MouseEvent) => {
			//event.stopPropagation();

			const isCtrlClick = event.ctrlKey || event.metaKey;
			selectNode(node.id, isCtrlClick);

			// Call the callback with current selection state
			if (onFileSelect) {
				// Get all selected nodes
				const currentSelection = Array.from(selectedNodes)
					.map((id) => findNodeById(rootNode, id))
					.filter(Boolean) as FileSystemNode[];

				// Add the current node if not already selected
				if (!selectedNodes.has(node.id)) {
					if (isCtrlClick) {
						currentSelection.push(node);
					} else {
						currentSelection.length = 0;
						currentSelection.push(node);
					}
				}

				onFileSelect(currentSelection, isCtrlClick);
			}

			// Handle context menu if right-click
			if (event.button === 2 && onContextMenu) {
				event.preventDefault();
				onContextMenu(node, event);
			}
		},
		[selectNode, selectedNodes, rootNode, onFileSelect, onContextMenu],
	);

	const handleNodeDoubleClick = useCallback(
		(node: FileSystemNode) => {
			// Handle file opening
			if (onFileOpen) {
				onFileOpen(node);
			} else {
				console.log('Opening file:', node.path);
			}
		},
		[handleToggleExpand, onFileOpen],
	);

	const handleDragStart = useCallback(
		(event: React.DragEvent, node: FileSystemNode) => {
			setDraggedNode(node);
			event.dataTransfer.effectAllowed = 'move';
			event.dataTransfer.setData('text/plain', node.path);

			// Call callback
			if (onDragStart) {
				onDragStart(node, event);
			}
		},
		[onDragStart],
	);

	const handleDragOver = useCallback((event: React.DragEvent) => {
		event.preventDefault();
		event.dataTransfer.dropEffect = 'move';
	}, []);

	const handleDragEnter = useCallback((event: React.DragEvent, node: FileSystemNode) => {
		if (node.type === 'directory' || node.type === 'archive') {
			setDragOverNode(node.id);
		}
	}, []);

	const handleDragLeave = useCallback((event: React.DragEvent) => {
		// Only clear if we're actually leaving the element
		if (!event.currentTarget.contains(event.relatedTarget as Node)) {
			setDragOverNode(null);
		}
	}, []);

	const handleDrop = useCallback(
		async (event: React.DragEvent, targetNode: FileSystemNode) => {
			event.preventDefault();
			setDragOverNode(null);

			if (targetNode.type !== 'directory' && targetNode.type !== 'archive') return;

			// Handle dropped files from external sources
			const files = Array.from(event.dataTransfer.files);
			if (files.length > 0) {
				if (onFileUpload) {
					try {
						await onFileUpload(files, targetNode);
					} catch (error) {
						const errorMessage = `Failed to upload files: ${error}`;
						console.error(errorMessage);
						if (onError) {
							onError(errorMessage, 'upload');
						}
					}
				} else {
					// Default behavior
					for (const file of files) {
						try {
							const arrayBuffer = await file.arrayBuffer();
							const targetPath = `${targetNode.path}/${file.name}`;
							await writePath(targetPath, arrayBuffer);
						} catch (error) {
							const errorMessage = `Failed to upload file: ${error}`;
							console.error(errorMessage);
							if (onError) {
								onError(errorMessage, 'upload');
							}
						}
					}
				}
				return;
			}

			// Handle internal node movement
			if (draggedNode && draggedNode.id !== targetNode.id) {
				if (onFileMove) {
					try {
						await onFileMove(draggedNode, targetNode);
					} catch (error) {
						const errorMessage = `Failed to move file: ${error}`;
						console.error(errorMessage);
						if (onError) {
							onError(errorMessage, 'move');
						}
					}
				} else {
					// Default behavior
					try {
						const targetPath = `${targetNode.path}/${draggedNode.name}`;
						await movePath(draggedNode.path, targetPath);
					} catch (error) {
						const errorMessage = `Failed to move file: ${error}`;
						console.error(errorMessage);
						if (onError) {
							onError(errorMessage, 'move');
						}
					}
				}
			}

			setDraggedNode(null);

			// Call drag end callback
			if (draggedNode && onDragEnd) {
				onDragEnd(draggedNode, event);
			}
		},
		[draggedNode, writePath, movePath, onFileUpload, onFileMove, onDragEnd, onError],
	);

	// Handle external file drops on the entire container
	const handleContainerDrop = useCallback(
		async (event: React.DragEvent) => {
			event.preventDefault();
			setIsDragOverContainer(false);

			const files = Array.from(event.dataTransfer.files);
			if (files.length > 0 && rootNode) {
				if (onFileUpload) {
					try {
						await onFileUpload(files, rootNode);
					} catch (error) {
						const errorMessage = `Failed to upload files to root: ${error}`;
						console.error(errorMessage);
						if (onError) {
							onError(errorMessage, 'upload');
						}
					}
				} else {
					// Default behavior
					for (const file of files) {
						try {
							const arrayBuffer = await file.arrayBuffer();
							const targetPath = `/${file.name}`;
							await writePath(targetPath, arrayBuffer);
						} catch (error) {
							const errorMessage = `Failed to upload file to root: ${error}`;
							console.error(errorMessage);
							if (onError) {
								onError(errorMessage, 'upload');
							}
						}
					}
				}
			}
		},
		[rootNode, writePath, onFileUpload, onError],
	);

	const handleContainerDragOver = useCallback((event: React.DragEvent) => {
		event.preventDefault();
		setIsDragOverContainer(true);
	}, []);

	const handleContainerDragLeave = useCallback((event: React.DragEvent) => {
		if (!event.currentTarget.contains(event.relatedTarget as Node)) {
			setIsDragOverContainer(false);
		}
	}, []);

	const handleFocus = useCallback(() => {
		setIsFocused(true);
	}, []);

	const handleBlur = useCallback(() => {
		setIsFocused(false);
	}, []);

	const handleKeyDown = useCallback(
		(event: KeyboardEvent) => {
			// Only handle keyboard events if the FileExplorer container is focused
			if (document.activeElement !== containerRef.current && !containerRef.current?.contains(document.activeElement)) {
				return;
			}

			if (event.key === 'Delete' && selectedNodes.size > 0) {
				// Handle delete operation
				const nodesToDelete = Array.from(selectedNodes)
					.map((nodeId) => findNodeById(rootNode, nodeId))
					.filter(Boolean) as FileSystemNode[];

				if (onFileDelete && nodesToDelete.length > 0) {
					onFileDelete(nodesToDelete).catch((error) => {
						const errorMessage = `Failed to delete files: ${error}`;
						console.error(errorMessage);
						if (onError) {
							onError(errorMessage, 'delete');
						}
					});
				} else {
					console.log(
						'Delete requested for:',
						nodesToDelete.map((n) => n.path),
					);
					// TODO: Implement actual delete functionality if no callback provided
					const deletePromises = nodesToDelete.map((node) => deletePath(node.path));
					Promise.all(deletePromises).catch(console.error);
				}
			}
			if (event.key === 'Escape') {
				clearSelection();
			}
			if (event.key === 'Enter' && selectedNodes.size === 1) {
				const nodeId = Array.from(selectedNodes)[0];
				const node = findNodeById(rootNode, nodeId);
				if (node) {
					handleNodeDoubleClick(node);
				}
			}
			// Ctrl+A to select all visible nodes
			if ((event.ctrlKey || event.metaKey) && event.key === 'a') {
				event.preventDefault();
				// This would need more complex logic to select all visible nodes
				console.log('Select all requested');
			}
		},
		[selectedNodes, clearSelection, rootNode, handleNodeDoubleClick, onFileDelete, onError],
	);

	useEffect(() => {
		const container = containerRef.current;
		if (container) {
			container.addEventListener('keydown', handleKeyDown);
			return () => container.removeEventListener('keydown', handleKeyDown);
		}
	}, [handleKeyDown]);

	if (loading) {
		return (
			<div className="flex h-full w-full flex-col bg-gray-900">
				<div className="flex items-center bg-gray-800 px-2 py-1 text-sm font-medium text-white">
					<span className="font-mono font-medium">📁</span>
					<span className="ml-2 font-medium">File Explorer</span>
				</div>
				<div className="flex-1 p-4 text-center">Loading...</div>
			</div>
		);
	}

	if (error) {
		return (
			<div className="flex h-full w-full flex-col bg-gray-900">
				<div className="flex items-center bg-gray-800 px-2 py-1 text-sm font-medium text-white">
					<span className="font-mono font-medium">❌</span>
					<span className="ml-2 font-medium">Error</span>
				</div>
				<div className="flex-1 p-4 text-center text-red-500">{error}</div>
			</div>
		);
	}

	return (
		<div
			className={`flex h-full w-full flex-col bg-gray-900 outline-none ${isDragOverContainer ? 'border border-blue-500 ring-2 ring-blue-500/20' : ''}`}
			ref={containerRef}
			tabIndex={0}
			onFocus={handleFocus}
			onBlur={handleBlur}
			onDrop={handleContainerDrop}
			onDragOver={handleContainerDragOver}
			onDragLeave={handleContainerDragLeave}
		>
			<div className="flex items-center bg-gray-800 px-2 py-1 text-sm font-medium text-white">
				<span className="font-mono font-medium">📁</span>
				<span className="ml-2 font-medium">File Explorer</span>
				<div className="ml-auto text-xs opacity-70">{selectedNodes.size > 0 && `${selectedNodes.size} selected`}</div>
			</div>
			<div className="flex-1 overflow-y-auto" onContextMenu={handleClick}>
				{rootNode?.children && rootNode.children.length > 0 ? (
					rootNode.children.map((child) => (
						<TreeNode
							key={child.id}
							node={child}
							level={0}
							onNodeClick={handleNodeClick}
							onNodeDoubleClick={handleNodeDoubleClick}
							onToggleExpand={handleToggleExpand}
							selectedNodes={selectedNodes}
							expandedNodes={expandedNodes}
							dragOverNode={dragOverNode}
							isFocused={isFocused}
							onDragStart={handleDragStart}
							onDrop={handleDrop}
							onDragOver={handleDragOver}
							onDragEnter={handleDragEnter}
							onDragLeave={handleDragLeave}
						/>
					))
				) : (
					<div className="p-4 text-center opacity-70">No files found</div>
				)}
			</div>
		</div>
	);
}
