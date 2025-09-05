import React, { useState } from "react";
import type { FileTreeNode, FileTreeProps, FileTreeItemProps } from "../types/FileTreeTypes";

import "../styles/FileTree.css";

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
	const [internalRootNodes, setInternalRootNodes] = useState<FileTreeNode[]>(rootNodes || []);

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
