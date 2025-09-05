import React, { useState } from "react";
import { FileTree } from "./FileTree";
import type { FileTreeNode } from "../types/FileTreeTypes";

// Demo component that shows how to use the FileTree with custom data
export const FileTreeDemo: React.FC = () => {
	const [selectedFileId, setSelectedFileId] = useState<string | undefined>();
	const [logs, setLogs] = useState<string[]>([]);

	const addLog = (message: string) => {
		setLogs(prev => [...prev.slice(-4), `${new Date().toLocaleTimeString()}: ${message}`]);
	};

	// Custom demo data
	const customFileTree: FileTreeNode[] = [
		{
			id: "demo-1",
			name: "RetroPlug Demo",
			type: "folder",
			path: "/demo",
			isExpanded: true,
			children: [
				{
					id: "demo-2",
					name: "songs",
					type: "folder",
					path: "/demo/songs",
					isExpanded: false,
					children: [
						{
							id: "demo-3",
							name: "track01.lsdsng",
							type: "file",
							path: "/demo/songs/track01.lsdsng",
							extension: "lsdsng",
							size: 32768
						},
						{
							id: "demo-4",
							name: "track02.lsdsng",
							type: "file",
							path: "/demo/songs/track02.lsdsng",
							extension: "lsdsng",
							size: 28672
						}
					]
				},
				{
					id: "demo-5",
					name: "kits",
					type: "folder",
					path: "/demo/kits",
					isExpanded: false,
					children: [
						{
							id: "demo-6",
							name: "drums.kit",
							type: "file",
							path: "/demo/kits/drums.kit",
							extension: "kit",
							size: 4096
						},
						{
							id: "demo-7",
							name: "bass.kit",
							type: "file",
							path: "/demo/kits/bass.kit",
							extension: "kit",
							size: 2048
						}
					]
				},
				{
					id: "demo-8",
					name: "readme.txt",
					type: "file",
					path: "/demo/readme.txt",
					extension: "txt",
					size: 512
				}
			]
		}
	];

	const handleFileClick = (node: FileTreeNode) => {
		setSelectedFileId(node.id);
		addLog(`Selected: ${node.name}`);
	};

	const handleFolderToggle = (node: FileTreeNode) => {
		addLog(`Toggled folder: ${node.name}`);
	};

	const handleFileDoubleClick = (node: FileTreeNode) => {
		addLog(`Opening: ${node.name} for editing`);
	};

	return (
		<div className="p-4 space-y-4">
			<div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
				<div>
					<h3 className="text-lg font-semibold text-white mb-2">Default File Tree</h3>
					<FileTree
						onFileClick={handleFileClick}
						onFolderToggle={handleFolderToggle}
						onFileDoubleClick={handleFileDoubleClick}
						selectedFileId={selectedFileId}
					/>
				</div>
				<div>
					<h3 className="text-lg font-semibold text-white mb-2">Custom Demo Tree</h3>
					<FileTree
						rootNodes={customFileTree}
						onFileClick={handleFileClick}
						onFolderToggle={handleFolderToggle}
						onFileDoubleClick={handleFileDoubleClick}
						selectedFileId={selectedFileId}
					/>
				</div>
			</div>
			<div className="bg-gray-800 border border-gray-700 rounded-sm p-3">
				<h4 className="text-sm font-semibold text-white mb-2">Event Log</h4>
				<div className="space-y-1 text-xs font-mono text-gray-300">
					{logs.length === 0 ? (
						<div className="text-gray-500">Click on files and folders to see events...</div>
					) : (
						logs.map((log, index) => (
							<div key={index}>{log}</div>
						))
					)}
				</div>
			</div>
		</div>
	);
};
