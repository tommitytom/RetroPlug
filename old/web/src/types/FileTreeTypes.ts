export interface FileTreeNode {
	id: string;
	name: string;
	type: 'file' | 'folder';
	path: string;
	children?: FileTreeNode[];
	isExpanded?: boolean;
	size?: number;
	lastModified?: Date;
	extension?: string;
}

export interface FileTreeProps {
	rootNodes?: FileTreeNode[];
	onFileClick?: (node: FileTreeNode) => void;
	onFolderToggle?: (node: FileTreeNode) => void;
	onFileDoubleClick?: (node: FileTreeNode) => void;
	selectedFileId?: string;
	className?: string;
}

export interface FileTreeItemProps {
	node: FileTreeNode;
	level: number;
	onFileClick?: (node: FileTreeNode) => void;
	onFolderToggle?: (node: FileTreeNode) => void;
	onFileDoubleClick?: (node: FileTreeNode) => void;
	selectedFileId?: string;
}
