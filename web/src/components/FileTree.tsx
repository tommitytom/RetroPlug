import React, { useState } from "react";
import type { FileTreeNode, FileTreeProps, FileTreeItemProps } from "../types/FileTreeTypes";

import "../styles/FileTree.css";

// Placeholder data
const PLACEHOLDER_FILE_TREE: FileTreeNode[] = [
	{
		id: "1",
		name: "src",
		type: "folder",
		path: "/src",
		isExpanded: true,
		children: [
			{
				id: "2",
				name: "components",
				type: "folder",
				path: "/src/components",
				isExpanded: false,
				children: [
					{
						id: "3",
						name: "LsdjKit.tsx",
						type: "file",
						path: "/src/components/LsdjKit.tsx",
						extension: "tsx",
						size: 8256,
						lastModified: new Date("2025-09-05T10:30:00")
					},
					{
						id: "4",
						name: "WaveView.tsx",
						type: "file",
						path: "/src/components/WaveView.tsx",
						extension: "tsx",
						size: 4128,
						lastModified: new Date("2025-09-04T15:20:00")
					},
					{
						id: "5",
						name: "EffectList.tsx",
						type: "file",
						path: "/src/components/EffectList.tsx",
						extension: "tsx",
						size: 3072,
						lastModified: new Date("2025-09-03T09:45:00")
					}
				]
			},
			{
				id: "6",
				name: "panels",
				type: "folder",
				path: "/src/panels",
				isExpanded: true,
				children: [
					{
						id: "7",
						name: "FileTreePanel.tsx",
						type: "file",
						path: "/src/panels/FileTreePanel.tsx",
						extension: "tsx",
						size: 512,
						lastModified: new Date("2025-09-05T12:00:00")
					},
					{
						id: "8",
						name: "RomEditorPanel.tsx",
						type: "file",
						path: "/src/panels/RomEditorPanel.tsx",
						extension: "tsx",
						size: 6144,
						lastModified: new Date("2025-09-02T14:30:00")
					}
				]
			},
			{
				id: "9",
				name: "types",
				type: "folder",
				path: "/src/types",
				isExpanded: false,
				children: [
					{
						id: "10",
						name: "FileTreeTypes.ts",
						type: "file",
						path: "/src/types/FileTreeTypes.ts",
						extension: "ts",
						size: 1024,
						lastModified: new Date("2025-09-05T12:10:00")
					},
					{
						id: "11",
						name: "LsdjTypes.ts",
						type: "file",
						path: "/src/types/LsdjTypes.ts",
						extension: "ts",
						size: 2048,
						lastModified: new Date("2025-09-01T11:15:00")
					}
				]
			},
			{
				id: "12",
				name: "utils",
				type: "folder",
				path: "/src/utils",
				isExpanded: false,
				children: [
					{
						id: "13",
						name: "FileUtil.ts",
						type: "file",
						path: "/src/utils/FileUtil.ts",
						extension: "ts",
						size: 1536,
						lastModified: new Date("2025-08-30T16:45:00")
					},
					{
						id: "14",
						name: "NativeUtil.ts",
						type: "file",
						path: "/src/utils/NativeUtil.ts",
						extension: "ts",
						size: 2560,
						lastModified: new Date("2025-08-29T13:20:00")
					}
				]
			}
		]
	},
	{
		id: "15",
		name: "public",
		type: "folder",
		path: "/public",
		isExpanded: false,
		children: [
			{
				id: "16",
				name: "index.html",
				type: "file",
				path: "/public/index.html",
				extension: "html",
				size: 1024,
				lastModified: new Date("2025-08-28T10:00:00")
			},
			{
				id: "17",
				name: "favicon.ico",
				type: "file",
				path: "/public/favicon.ico",
				extension: "ico",
				size: 256,
				lastModified: new Date("2025-08-25T09:30:00")
			}
		]
	},
	{
		id: "18",
		name: "package.json",
		type: "file",
		path: "/package.json",
		extension: "json",
		size: 2048,
		lastModified: new Date("2025-09-04T17:00:00")
	},
	{
		id: "19",
		name: "tsconfig.json",
		type: "file",
		path: "/tsconfig.json",
		extension: "json",
		size: 512,
		lastModified: new Date("2025-08-20T14:15:00")
	},
	{
		id: "20",
		name: "README.md",
		type: "file",
		path: "/README.md",
		extension: "md",
		size: 1280,
		lastModified: new Date("2025-08-15T11:45:00")
	}
];

const getFileIcon = (extension?: string) => {
	switch (extension) {
		case 'tsx':
		case 'ts':
			return '⚛️';
		case 'js':
		case 'jsx':
			return '🔧';
		case 'json':
			return '📄';
		case 'md':
			return '📝';
		case 'html':
			return '🌐';
		case 'css':
			return '🎨';
		case 'ico':
			return '🖼️';
		default:
			return '📄';
	}
};

const formatFileSize = (bytes?: number) => {
	if (!bytes) return '';
	if (bytes < 1024) return `${bytes}B`;
	if (bytes < 1024 * 1024) return `${Math.round(bytes / 1024)}KB`;
	return `${Math.round(bytes / (1024 * 1024))}MB`;
};

const FileTreeItem: React.FC<FileTreeItemProps> = ({
	node,
	level,
	onFileClick,
	onFolderToggle,
	onFileDoubleClick,
	selectedFileId
}) => {
	const isSelected = node.id === selectedFileId;

	const handleClick = () => {
		if (node.type === 'folder') {
			onFolderToggle?.(node);
		} else {
			onFileClick?.(node);
		}
	};

	const handleDoubleClick = () => {
		if (node.type === 'file') {
			onFileDoubleClick?.(node);
		}
	};

	return (
		<>
			<div
				className={`file-tree-item ${isSelected ? 'selected' : ''}`}
				data-level={level}
				onClick={handleClick}
				onDoubleClick={handleDoubleClick}
				title={node.path}
			>
				{node.type === 'folder' ? (
					<div className="file-tree-item-icon">
						{node.isExpanded ? "▼" : "▶"}
					</div>
				) : (
					<div className="file-tree-item-icon">
						{getFileIcon(node.extension)}
					</div>
				)}
				<span className="file-tree-item-name">{node.name}</span>
				{node.type === 'file' && node.size && (
					<span className="file-tree-item-size">
						{formatFileSize(node.size)}
					</span>
				)}
			</div>
			{node.type === 'folder' && node.isExpanded && node.children && (
				<>
					{node.children.map((child) => (
						<FileTreeItem
							key={child.id}
							node={child}
							level={level + 1}
							onFileClick={onFileClick}
							onFolderToggle={onFolderToggle}
							onFileDoubleClick={onFileDoubleClick}
							selectedFileId={selectedFileId}
						/>
					))}
				</>
			)}
		</>
	);
};

export const FileTree: React.FC<FileTreeProps> = ({
	rootNodes,
	onFileClick,
	onFolderToggle,
	onFileDoubleClick,
	selectedFileId,
	className
}) => {
	const [internalRootNodes, setInternalRootNodes] = useState<FileTreeNode[]>(rootNodes || PLACEHOLDER_FILE_TREE);

	const handleFolderToggle = (targetNode: FileTreeNode) => {
		const toggleNodeInTree = (nodes: FileTreeNode[]): FileTreeNode[] => {
			return nodes.map(node => {
				if (node.id === targetNode.id) {
					return { ...node, isExpanded: !node.isExpanded };
				}
				if (node.children) {
					return { ...node, children: toggleNodeInTree(node.children) };
				}
				return node;
			});
		};

		setInternalRootNodes(toggleNodeInTree(internalRootNodes));
		onFolderToggle?.(targetNode);
	};

	const handleFileClick = (node: FileTreeNode) => {
		console.log('File clicked:', node.name, node.path);
		onFileClick?.(node);
	};

	const handleFileDoubleClick = (node: FileTreeNode) => {
		console.log('File double-clicked:', node.name, node.path);
		onFileDoubleClick?.(node);
	};

	return (
		<div className={`file-tree-container ${className || ''}`}>
			<div className="file-tree-header">
				<span className="file-tree-header-icon">📁</span>
				<span className="file-tree-header-title">Project Files</span>
			</div>
			<div className="file-tree-content">
				{internalRootNodes.map((node) => (
					<FileTreeItem
						key={node.id}
						node={node}
						level={0}
						onFileClick={handleFileClick}
						onFolderToggle={handleFolderToggle}
						onFileDoubleClick={handleFileDoubleClick}
						selectedFileId={selectedFileId}
					/>
				))}
			</div>
		</div>
	);
};
