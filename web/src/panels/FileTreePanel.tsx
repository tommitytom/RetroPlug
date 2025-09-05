import React, { useCallback, useState } from "react";
import { FileTree } from "../components/FileTree";
import type { FileTreeNode } from "../types/FileTreeTypes";
import { useProject } from "../hooks/RetroPlugHooks";

// Placeholder data
const PLACEHOLDER_FILE_TREE: FileTreeNode[] = [
	{
		id: "1",
		name: "LSDj-v5.0.3.gb",
		type: "file",
		path: "/LSDj-v5.0.3.gb",
		extension: "gb"
	},
	{
		id: "2",
		name: "LSDj-v5.0.3.sav",
		type: "folder",
		path: "/LSDj-v5.0.3.sav",
		extension: "sav",
		children: [
			{
				id: "3",
				name: "STEP.lsdsng",
				type: "file",
				path: "/LSDj-v5.0.3.sav/STEP.lsdsng",
				extension: "lsdsng"
			}
		]
	},
	{
		id: "4",
		name: "Aquellex - Anthropomorphosis",
		type: "folder",
		path: "/Aquellex - Anthropomorphosis",
		children: [
			{
				id: "5",
				name: "01 Wanderflux Expansion (v4.9.6).lsdprj",
				type: "file",
				path: "/Aquellex - Anthropomorphosis/01 Wanderflux Expansion (v4.9.6).lsdprj",
				extension: "lsdprj"
			}
		]
	}
];

export const FileTreePanel: React.FC = () => {
	const project = useProject();
	const [selectedFileId, setSelectedFileId] = useState<string | undefined>();

	const handleFileClick = useCallback(async (node: FileTreeNode) => {
		if (!project) return;

		setSelectedFileId(node.id);
		console.log('FileTreePanel: File clicked:', node.name, node.path);

		const romResponse = await fetch('LSDj-v5.0.3.gb');
		const savResponse = await fetch('LSDj-v5.0.3.sav');

		project.clearSystems();
		project.addSystem({
			entries: {
				rom: {
					path: node.path,
					data: new Uint8Array(await romResponse.arrayBuffer())
				},
				sram: {
					path: node.path,
					data: new Uint8Array(await savResponse.arrayBuffer())
				}
			}
		});
	}, [project]);


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
				rootNodes={PLACEHOLDER_FILE_TREE}
				onFileClick={handleFileClick}
				onFolderToggle={handleFolderToggle}
				onFileDoubleClick={handleFileDoubleClick}
				selectedFileId={selectedFileId}
				className="h-full"
			/>
		</div>
	);
};
