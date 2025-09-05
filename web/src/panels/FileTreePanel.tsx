import React, { useState } from "react";
import { FileTree } from "../components/FileTree";
import type { FileTreeNode } from "../types/FileTreeTypes";

export const FileTreePanel: React.FC = () => {
	const [selectedFileId, setSelectedFileId] = useState<string | undefined>();

	const handleFileClick = (node: FileTreeNode) => {
		setSelectedFileId(node.id);
		console.log('FileTreePanel: File clicked:', node.name, node.path);
	};

	const handleFolderToggle = (node: FileTreeNode) => {
		console.log('FileTreePanel: Folder toggled:', node.name, node.isExpanded ? 'collapsed' : 'expanded');
	};

	const handleFileDoubleClick = (node: FileTreeNode) => {
		console.log('FileTreePanel: File double-clicked for editing:', node.name, node.path);
		// TODO: Open file in editor
	};

	return (
		<div className="h-full p-2">
			<FileTree
				onFileClick={handleFileClick}
				onFolderToggle={handleFolderToggle}
				onFileDoubleClick={handleFileDoubleClick}
				selectedFileId={selectedFileId}
				className="h-full"
			/>
		</div>
	);
};
