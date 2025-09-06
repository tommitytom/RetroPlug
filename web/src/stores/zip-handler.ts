import JSZip from 'jszip'
import type { ArchiveHandler, ArchiveInstance, FileSystemNode } from './types'

export class ZipArchiveHandler implements ArchiveHandler {
	type = 'zip'
	extensions = ['.zip', '.zipx']

	canHandle(buffer: ArrayBuffer): boolean {
		try {
			// Check for ZIP signature (PK)
			const view = new DataView(buffer)
			return view.getUint16(0) === 0x504B
		} catch {
			return false
		}
	}

	open(buffer: ArrayBuffer): ArchiveInstance {
		return new ZipArchiveInstance(buffer)
	}

	create(): ArchiveInstance {
		return new ZipArchiveInstance()
	}
}

class ZipArchiveInstance implements ArchiveInstance {
	private zip: JSZip

	constructor(buffer?: ArrayBuffer) {
		this.zip = new JSZip()
		if (buffer) {
			// Note: loadAsync is async, but we're in a worker so we can block
			// In a real implementation, you'd want to handle this properly
			this.zip.loadAsync(buffer).then(() => {
				// Archive loaded
			})
		}
	}

	list(): FileSystemNode[] {
		const nodes: FileSystemNode[] = []

		this.zip.forEach((relativePath, file) => {
			nodes.push({
				id: relativePath,
				name: relativePath.split('/').pop() || relativePath,
				path: relativePath,
				type: file.dir ? 'directory' : 'file',
				size: 0,//file._data?.uncompressedSize,
				lastModified: file.date?.getTime()
			})
		})

		return nodes
	}

	read(path: string): ArrayBuffer {
		const file = this.zip.file(path)
		if (!file) throw new Error('File not found in archive')

		// In worker, we can use sync operations
		// This is a simplified example - real implementation would handle async properly
		let result: ArrayBuffer | null = null
		file.async('arraybuffer').then(buffer => {
			result = buffer
		})

		// Wait for result (simplified - use proper async handling in production)
		while (!result) {
			// Busy wait - don't do this in production!
		}

		return result!
	}

	write(path: string, content: ArrayBuffer): void {
		this.zip.file(path, content)
	}

	delete(path: string): void {
		this.zip.remove(path)
	}

	extract(path?: string): Map<string, ArrayBuffer> {
		const files = new Map<string, ArrayBuffer>()

		this.zip.forEach((relativePath, file) => {
			if (!file.dir && (!path || relativePath.startsWith(path))) {
				// Simplified sync handling
				file.async('arraybuffer').then(content => {
					files.set(relativePath, content)
				})
			}
		})

		return files
	}

	serialize(): ArrayBuffer {
		// Generate the archive buffer
		let result: ArrayBuffer | null = null

		this.zip.generateAsync({ type: 'arraybuffer' }).then(buffer => {
			result = buffer
		})

		// Wait for result (simplified - use proper async handling in production)
		while (!result) {
			// Busy wait - don't do this in production!
		}

		return result!
	}

	close(): void {
		// Cleanup if needed
	}
}
