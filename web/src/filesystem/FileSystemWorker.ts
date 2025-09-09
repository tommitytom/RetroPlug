import * as Comlink from 'comlink';
import type { ArchiveHandler, ArchiveInstance, FileSystemNode, ParsedPath } from './types';
import { ZipArchiveHandler } from './ZipArchiveHandler';
import { SavArchiveHandler } from './SavArchiveHandler';
import type { MainModule } from '../native/RetroPlug.d.ts';

export interface FileSystemWorkerAPI {
	initialize: () => Promise<void>;
	listPath: (path: string) => Promise<FileSystemNode>;
	readPath: (path: string) => Promise<ArrayBuffer>;
	writePath: (path: string, content: ArrayBuffer) => Promise<void>;
	createDirectory: (path: string) => Promise<void>;
	deletePath: (path: string) => Promise<void>;
	copyPath: (source: string, destination: string) => Promise<void>;
	movePath: (source: string, destination: string) => Promise<void>;
	fileExists: (path: string) => Promise<boolean>;
}

class FileSystemWorker implements FileSystemWorkerAPI {
	private module: MainModule | null = null;
	private opfsRoot: FileSystemDirectoryHandle | null = null;
	private archiveHandlers = new Map<string, ArchiveHandler>();
	private archiveCache = new Map<string, ArchiveInstance>();

	async initialize(): Promise<void> {
		this.opfsRoot = await navigator.storage.getDirectory();

		const moduleFactory = (await import('../native/RetroPlugEcs.mjs')).default;
		this.module = (await moduleFactory({
			locateFile: (path: string) => {
				if (path.endsWith('.wasm')) {
					return '/RetroPlugEcs.wasm';
				}
				return path;
			},
			print: (text: string) => {
				console.log('WASM:', text);
			},
			printErr: (text: string) => {
				console.error('WASM Error:', text);
			},
		})) as MainModule;

		const zipHandler = new ZipArchiveHandler();
		this.registerArchiveHandler(zipHandler);

		const savHandler = new SavArchiveHandler(this.module);
		this.registerArchiveHandler(savHandler);
	}

	async registerArchiveHandler(handler: ArchiveHandler): Promise<void> {
		this.archiveHandlers.set(handler.type, handler);
	}

	async unregisterArchiveHandler(type: string): Promise<void> {
		// Close any open archives of this type
		for (const [path, archive] of this.archiveCache) {
			if (path.includes(`.${type}`)) {
				archive.close();
				this.archiveCache.delete(path);
			}
		}

		this.archiveHandlers.delete(type);
	}

	async listPath(path: string): Promise<FileSystemNode> {
		const parsed = this.parsePath(path);

		if (parsed.type === 'opfs') {
			return await this.listOPFSDirectory(parsed.opfsPath);
		} else {
			return await this.listArchiveDirectory(parsed.opfsPath, parsed.archivePath!);
		}
	}

	async readPath(path: string): Promise<ArrayBuffer> {
		const parsed = this.parsePath(path);

		if (parsed.type === 'opfs' || parsed.archivePath === '') {
			return await this.readOPFSFile(parsed.opfsPath);
		} else {
			return await this.readArchiveFile(parsed.opfsPath, parsed.archivePath!);
		}
	}

	async writePath(path: string, content: ArrayBuffer): Promise<void> {
		const parsed = this.parsePath(path);

		if (parsed.type === 'opfs' || parsed.archivePath === '') {
			await this.writeOPFSFile(parsed.opfsPath, content);
		} else {
			await this.writeArchiveFile(parsed.opfsPath, parsed.archivePath!, content);
		}
	}

	async createDirectory(path: string): Promise<void> {
		const parsed = this.parsePath(path);

		if (parsed.type === 'opfs') {
			await this.createOPFSDirectory(parsed.opfsPath);
		} else {
			// For archives, we might need to create directory entries
			const archive = await this.getOrOpenArchive(parsed.opfsPath);
			// Most archives handle directories implicitly when files are added
			// but we can add an empty directory marker if needed
			archive.write(parsed.archivePath! + '/.keep', new ArrayBuffer(0));
			await this.saveArchive(parsed.opfsPath, archive);
		}
	}

	async deletePath(path: string): Promise<void> {
		const parsed = this.parsePath(path);

		if (parsed.type === 'opfs' || parsed.archivePath === '') {
			await this.deleteOPFSNode(parsed.opfsPath);
		} else {
			await this.deleteArchiveFile(parsed.opfsPath, parsed.archivePath!);
		}
	}

	async copyPath(source: string, destination: string): Promise<void> {
		const content = await this.readPath(source);
		await this.writePath(destination, content);

		// If source is a directory, copy recursively
		try {
			const sourceNode = await this.listPath(source);
			if (sourceNode.type === 'directory' && sourceNode.children) {
				for (const child of sourceNode.children) {
					const childSource = `${source}/${child.name}`;
					const childDest = `${destination}/${child.name}`;
					await this.copyPath(childSource, childDest);
				}
			}
		} catch {
			// Not a directory, already copied as file
		}
	}

	async movePath(source: string, destination: string): Promise<void> {
		await this.copyPath(source, destination);
		await this.deletePath(source);
	}

	async fileExists(path: string): Promise<boolean> {
		const parsed = this.parsePath(path);

		try {
			if (parsed.type === 'opfs' || parsed.archivePath === '') {
				await this.getFileHandle(parsed.opfsPath);
				return true;
			} else {
				const archive = await this.getOrOpenArchive(parsed.opfsPath);
				const allNodes = archive.list();
				return allNodes.some((node) => node.path === parsed.archivePath && node.type === 'file');
			}
		} catch {
			return false;
		}
	}

	private parsePath(path: string): ParsedPath {
		const segments = path.split('/').filter(Boolean);

		// Look for archive extensions in the path
		let archiveIndex = -1;
		let archiveType: string | undefined;

		for (let i = 0; i < segments.length; i++) {
			for (const [type, handler] of this.archiveHandlers) {
				const extensions = (handler as any).extensions || [`.${type}`];
				for (const ext of extensions) {
					if (segments[i].endsWith(ext)) {
						archiveIndex = i;
						archiveType = type;
						break;
					}
				}
				if (archiveIndex !== -1) break;
			}
			if (archiveIndex !== -1) break;
		}

		if (archiveIndex === -1) {
			// Pure OPFS path
			return {
				type: 'opfs',
				opfsPath: '/' + segments.join('/'),
				segments,
			};
		}

		// Archive path
		const opfsSegments = segments.slice(0, archiveIndex + 1);
		const archiveSegments = segments.slice(archiveIndex + 1);

		return {
			type: 'archive',
			opfsPath: '/' + opfsSegments.join('/'),
			archivePath: archiveSegments.join('/'),
			segments,
		};
	}

	private async listOPFSDirectory(path: string): Promise<FileSystemNode> {
		if (!this.opfsRoot) throw new Error('OPFS not initialized');

		const handle = await this.getDirectoryHandle(path);
		const node: FileSystemNode = {
			id: path,
			name: path === '/' ? 'root' : path.split('/').pop() || '',
			path,
			type: 'directory',
			children: [],
		};

		for await (const entry of handle.values()) {
			const childPath = path === '/' ? `/${entry.name}` : `${path}/${entry.name}`;

			if (entry.kind === 'file') {
				const fileHandle = entry as FileSystemFileHandle;
				const file = await fileHandle.getFile();

				// Check if it's an archive
				let isArchive = false;
				let archiveType: string | undefined;

				for (const [type, handler] of this.archiveHandlers) {
					const extensions = (handler as any).extensions || [`.${type}`];
					if (extensions.some((ext: string) => entry.name.endsWith(ext))) {
						isArchive = true;
						archiveType = type;
						break;
					}
				}

				node.children!.push({
					id: childPath,
					name: entry.name,
					path: childPath,
					type: isArchive ? 'archive' : 'file',
					archiveType,
					size: file.size,
					lastModified: file.lastModified,
				});
			} else {
				node.children!.push({
					id: childPath,
					name: entry.name,
					path: childPath,
					type: 'directory',
					children: [],
				});
			}
		}

		return node;
	}

	private async listArchiveDirectory(archivePath: string, internalPath: string): Promise<FileSystemNode> {
		const archive = await this.getOrOpenArchive(archivePath);
		const allNodes = archive.list();

		// Filter and build tree for the requested path
		const targetPath = internalPath || '';
		const node: FileSystemNode = {
			id: `${archivePath}/${targetPath}`,
			name: targetPath.split('/').pop() || archivePath.split('/').pop() || '',
			path: `${archivePath}/${targetPath}`,
			type: 'directory',
			children: [],
		};

		// Build children for this specific directory
		const childrenMap = new Map<string, FileSystemNode>();

		for (const item of allNodes) {
			// Check if item is a direct child of targetPath
			if (!targetPath || item.path.startsWith(targetPath + '/') || item.path === targetPath) {
				const relativePath = targetPath ? item.path.substring(targetPath.length + 1) : item.path;

				if (relativePath && !relativePath.includes('/')) {
					// Direct child
					childrenMap.set(item.name, {
						...item,
						id: `${archivePath}/${item.path}`,
						path: `${archivePath}/${item.path}`,
					});
				} else if (relativePath && relativePath.includes('/')) {
					// Child directory
					const dirName = relativePath.split('/')[0];
					if (!childrenMap.has(dirName)) {
						childrenMap.set(dirName, {
							id: `${archivePath}/${targetPath}/${dirName}`,
							name: dirName,
							path: `${archivePath}/${targetPath}/${dirName}`,
							type: 'directory',
							children: [],
						});
					}
				}
			}
		}

		node.children = Array.from(childrenMap.values());
		return node;
	}

	private async readOPFSFile(path: string): Promise<ArrayBuffer> {
		if (!this.opfsRoot) throw new Error('OPFS not initialized');

		const fileHandle = await this.getFileHandle(path);
		const file = await fileHandle.getFile();
		return await file.arrayBuffer();
	}

	private async readArchiveFile(archivePath: string, internalPath: string): Promise<ArrayBuffer> {
		const archive = await this.getOrOpenArchive(archivePath);
		return archive.read(internalPath);
	}

	private async writeOPFSFile(path: string, content: ArrayBuffer): Promise<void> {
		if (!this.opfsRoot) throw new Error('OPFS not initialized');

		const fileName = path.split('/').pop();
		if (!fileName) throw new Error('Invalid file path');

		const dirPath = path.substring(0, path.lastIndexOf('/')) || '/';
		const dirHandle = await this.getDirectoryHandle(dirPath);

		const fileHandle = await dirHandle.getFileHandle(fileName, {
			create: true,
		});

		// Use sync access handle in worker for better performance
		// @ts-ignore - createSyncAccessHandle is available in workers
		const accessHandle = await fileHandle.createSyncAccessHandle();

		try {
			accessHandle.truncate(0);
			accessHandle.write(content);
			accessHandle.flush();
		} finally {
			accessHandle.close();
		}
	}

	private async writeArchiveFile(archivePath: string, internalPath: string, content: ArrayBuffer): Promise<void> {
		const archive = await this.getOrOpenArchive(archivePath);
		archive.write(internalPath, content);
		await this.saveArchive(archivePath, archive);
	}

	private async createOPFSDirectory(path: string): Promise<void> {
		if (!this.opfsRoot) throw new Error('OPFS not initialized');

		const parts = path.split('/').filter(Boolean);
		let currentHandle = this.opfsRoot;

		for (const part of parts) {
			currentHandle = await currentHandle.getDirectoryHandle(part, {
				create: true,
			});
		}
	}

	private async deleteOPFSNode(path: string): Promise<void> {
		if (!this.opfsRoot) throw new Error('OPFS not initialized');

		const name = path.split('/').pop();
		if (!name) throw new Error('Invalid path');

		const parentPath = path.substring(0, path.lastIndexOf('/')) || '/';
		const parentHandle = await this.getDirectoryHandle(parentPath);

		await parentHandle.removeEntry(name, { recursive: true });
	}

	private async deleteArchiveFile(archivePath: string, internalPath: string): Promise<void> {
		const archive = await this.getOrOpenArchive(archivePath);
		archive.delete(internalPath);
		await this.saveArchive(archivePath, archive);
	}

	private async getOrOpenArchive(archivePath: string): Promise<ArchiveInstance> {
		// Check cache first
		if (this.archiveCache.has(archivePath)) {
			return this.archiveCache.get(archivePath)!;
		}

		// Read archive file
		const buffer = await this.readOPFSFile(archivePath);

		// Find appropriate handler
		const extension = archivePath.split('.').pop();
		const handler = this.archiveHandlers.get(extension!);

		if (!handler) {
			throw new Error(`No handler registered for .${extension} files`);
		}

		// Open archive
		const archive = await handler.open(buffer);
		this.archiveCache.set(archivePath, archive);

		return archive;
	}

	private async saveArchive(archivePath: string, archive: ArchiveInstance): Promise<void> {
		const buffer = archive.serialize();
		await this.writeOPFSFile(archivePath, buffer);
	}

	private async getDirectoryHandle(path: string): Promise<FileSystemDirectoryHandle> {
		if (!this.opfsRoot) throw new Error('OPFS not initialized');

		if (path === '/' || path === '') return this.opfsRoot;

		const parts = path.split('/').filter(Boolean);
		let handle = this.opfsRoot;

		for (const part of parts) {
			handle = await handle.getDirectoryHandle(part);
		}

		return handle;
	}

	private async getFileHandle(path: string): Promise<FileSystemFileHandle> {
		if (!this.opfsRoot) throw new Error('OPFS not initialized');

		const fileName = path.split('/').pop();
		if (!fileName) throw new Error('Invalid file path');

		const dirPath = path.substring(0, path.lastIndexOf('/')) || '/';
		const dirHandle = await this.getDirectoryHandle(dirPath);

		return await dirHandle.getFileHandle(fileName);
	}
}

// Expose the worker API
Comlink.expose(new FileSystemWorker());
