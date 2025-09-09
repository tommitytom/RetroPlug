export interface FileSystemNode {
	id: string
	name: string
	path: string
	type: 'file' | 'directory' | 'archive'
	size?: number
	lastModified?: number
	children?: FileSystemNode[]
	archiveType?: string // 'zip', 'tar', etc.
	isExpanded?: boolean
	isLoading?: boolean
	parentId?: string
}

export interface FileOperation {
	type: 'read' | 'write' | 'delete' | 'move' | 'copy'
	source: string
	destination?: string
	content?: ArrayBuffer | Blob | string
}

export interface ArchiveHandler {
	type: string // 'zip', 'tar', etc.
	extensions: string[] // ['.zip', '.zipx']
	canHandle: (buffer: ArrayBuffer) => boolean
	open: (buffer: ArrayBuffer) => Promise<ArchiveInstance>
	create: () => ArchiveInstance
}

export interface ArchiveInstance {
	list: () => FileSystemNode[]
	read: (path: string) => ArrayBuffer
	write: (path: string, content: ArrayBuffer) => void
	delete: (path: string) => void
	extract: (path?: string) => Map<string, ArrayBuffer>
	serialize: () => ArrayBuffer // Convert archive to buffer for saving
	close: () => void
}

export interface ParsedPath {
	type: 'opfs' | 'archive'
	opfsPath: string // Path in OPFS (up to and including archive file if applicable)
	archivePath?: string // Path within archive (if type is 'archive')
	segments: string[] // All path segments
}
