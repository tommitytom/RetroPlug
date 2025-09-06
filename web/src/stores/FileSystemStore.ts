import { create } from 'zustand'
import { subscribeWithSelector } from 'zustand/middleware'
import * as Comlink from 'comlink'
import type { FileSystemWorkerAPI } from './worker';
import type { FileSystemNode, ArchiveHandler, ParsedPath } from './types'

interface OPFSStore {
	// State
	rootNode: FileSystemNode | null
	selectedNodes: Set<string>
	expandedNodes: Set<string>
	loading: boolean
	error: string | null
	worker: Comlink.Remote<FileSystemWorkerAPI> | null

	// Initialization
	initialize: () => Promise<void>

	// Archive handlers
	registerArchiveHandler: (handler: ArchiveHandler) => Promise<void>
	unregisterArchiveHandler: (type: string) => Promise<void>

	// Unified path operations
	listPath: (path: string) => Promise<FileSystemNode>
	readPath: (path: string) => Promise<ArrayBuffer>
	writePath: (path: string, content: ArrayBuffer | Blob | string) => Promise<void>
	deletePath: (path: string) => Promise<void>
	createDirectory: (path: string) => Promise<void>
	fileExists: (path: string) => Promise<boolean>

	// Copy/Move operations
	copyPath: (source: string, destination: string) => Promise<void>
	movePath: (source: string, destination: string) => Promise<void>

	// Tree operations
	toggleNode: (nodeId: string) => void
	selectNode: (nodeId: string, multiSelect?: boolean) => void
	clearSelection: () => void
	refreshNode: (path: string) => Promise<void>

	// Utility
	parsePath: (path: string) => ParsedPath
	getNodeByPath: (path: string) => FileSystemNode | null
	getNodeById: (id: string) => FileSystemNode | null
	cleanup: () => void
}

export const useOPFSStore = create<OPFSStore>()(
	subscribeWithSelector((set, get) => ({
		// Initial state
		rootNode: null,
		selectedNodes: new Set(),
		expandedNodes: new Set(),
		loading: false,
		error: null,
		worker: null,

		initialize: async () => {
			const { worker } = get()
			if (worker) return

			set({ loading: true, error: null })

			try {
				// Initialize web worker
				const Worker = new window.Worker(
					new URL('./worker.js', import.meta.url),
					{ type: 'module' }
				)
				const workerApi = Comlink.wrap<FileSystemWorkerAPI>(Worker)

				// Initialize OPFS in worker
				await workerApi.initialize()

				// Load root directory
				const rootNode = await workerApi.listPath('/')

				set({
					worker: workerApi,
					rootNode,
					loading: false,
					expandedNodes: new Set([rootNode.id])
				})
			} catch (error) {
				set({
					error: error instanceof Error ? error.message : 'Failed to initialize',
					loading: false
				})
			}
		},

		registerArchiveHandler: async (handler) => {
			const { worker } = get()
			if (!worker) throw new Error('Worker not initialized')

			// Transfer handler to worker
			await worker.registerArchiveHandler({
				type: handler.type,
				extensions: handler.extensions
			}, Comlink.transfer(handler, []))

			const rootNode = await worker.listPath('/');
			set({ rootNode });
		},

		unregisterArchiveHandler: async (type) => {
			const { worker } = get()
			if (!worker) throw new Error('Worker not initialized')

			await worker.unregisterArchiveHandler(type)
		},

		parsePath: (path: string): ParsedPath => {
			const segments = path.split('/').filter(Boolean)

			// Look for archive extensions in the path
			const archiveExtensions = ['.zip', '.tar', '.7z', '.rar'] // Common extensions
			let archiveIndex = -1
			let archiveType: string | undefined

			for (let i = 0; i < segments.length; i++) {
				for (const ext of archiveExtensions) {
					if (segments[i].endsWith(ext)) {
						archiveIndex = i
						archiveType = ext.substring(1)
						break
					}
				}
				if (archiveIndex !== -1) break
			}

			if (archiveIndex === -1) {
				// Pure OPFS path
				return {
					type: 'opfs',
					opfsPath: '/' + segments.join('/'),
					segments
				}
			}

			// Archive path
			const opfsSegments = segments.slice(0, archiveIndex + 1)
			const archiveSegments = segments.slice(archiveIndex + 1)

			return {
				type: 'archive',
				opfsPath: '/' + opfsSegments.join('/'),
				archivePath: archiveSegments.join('/'),
				segments
			}
		},

		listPath: async (path) => {
			const { worker } = get()
			if (!worker) throw new Error('Worker not initialized')

			set({ loading: true, error: null })

			try {
				const node = await worker.listPath(path)
				set({ loading: false })
				return node
			} catch (error) {
				set({
					error: error instanceof Error ? error.message : 'Failed to list path',
					loading: false
				})
				throw error
			}
		},

		readPath: async (path) => {
			const { worker } = get()
			if (!worker) throw new Error('Worker not initialized')

			return await worker.readPath(path)
		},

		writePath: async (path, content) => {
			const { worker } = get()
			if (!worker) throw new Error('Worker not initialized')

			let buffer: ArrayBuffer

			if (content instanceof ArrayBuffer) {
				buffer = content
			} else if (content instanceof Blob) {
				buffer = await content.arrayBuffer()
			} else if (typeof content === 'string') {
				buffer = new TextEncoder().encode(content).buffer
			} else {
				throw new Error('Invalid content type')
			}

			await worker.writePath(path, buffer)

			// Refresh parent directory
			const parentPath = path.substring(0, path.lastIndexOf('/')) || '/'
			await get().refreshNode(parentPath)
		},

		deletePath: async (path) => {
			const { worker } = get()
			if (!worker) throw new Error('Worker not initialized')

			await worker.deletePath(path)

			// Refresh parent directory
			const parentPath = path.substring(0, path.lastIndexOf('/')) || '/'
			await get().refreshNode(parentPath)
		},

		createDirectory: async (path) => {
			const { worker } = get()
			if (!worker) throw new Error('Worker not initialized')

			await worker.createDirectory(path)

			// Refresh parent directory
			const parentPath = path.substring(0, path.lastIndexOf('/')) || '/'
			await get().refreshNode(parentPath)
		},

		fileExists: async (path) => {
			const { worker } = get()
			if (!worker) throw new Error('Worker not initialized')

			return await worker.fileExists(path)
		},

		copyPath: async (source, destination) => {
			const { worker } = get()
			if (!worker) throw new Error('Worker not initialized')

			await worker.copyPath(source, destination)

			// Refresh destination parent
			const destParent = destination.substring(0, destination.lastIndexOf('/')) || '/'
			await get().refreshNode(destParent)
		},

		movePath: async (source, destination) => {
			const { worker } = get()
			if (!worker) throw new Error('Worker not initialized')

			await worker.movePath(source, destination)

			// Refresh both source and destination parents
			const sourceParent = source.substring(0, source.lastIndexOf('/')) || '/'
			const destParent = destination.substring(0, destination.lastIndexOf('/')) || '/'

			await get().refreshNode(sourceParent)
			if (sourceParent !== destParent) {
				await get().refreshNode(destParent)
			}
		},

		toggleNode: (nodeId) => {
			set((state) => {
				const expanded = new Set(state.expandedNodes)
				if (expanded.has(nodeId)) {
					expanded.delete(nodeId)
				} else {
					expanded.add(nodeId)
				}
				return { expandedNodes: expanded }
			})
		},

		selectNode: (nodeId, multiSelect = false) => {
			set((state) => {
				const selected = multiSelect ? new Set(state.selectedNodes) : new Set<string>()
				if (selected.has(nodeId)) {
					selected.delete(nodeId)
				} else {
					selected.add(nodeId)
				}
				return { selectedNodes: selected }
			})
		},

		clearSelection: () => {
			set({ selectedNodes: new Set() })
		},

		refreshNode: async (path) => {
			const node = get().getNodeByPath(path)
			if (!node) return

			const updatedNode = await get().listPath(path)

			// Update the node in the tree
			set((state) => {
				const updateNodeInTree = (root: FileSystemNode): FileSystemNode => {
					if (root.path === path) {
						return { ...root, ...updatedNode }
					}
					if (root.children) {
						return {
							...root,
							children: root.children.map(updateNodeInTree)
						}
					}
					return root
				}

				return {
					rootNode: state.rootNode ? updateNodeInTree(state.rootNode) : null
				}
			})
		},

		getNodeByPath: (path) => {
			const { rootNode } = get()
			if (!rootNode) return null

			const findNode = (node: FileSystemNode, targetPath: string): FileSystemNode | null => {
				if (node.path === targetPath) return node
				if (node.children) {
					for (const child of node.children) {
						const found = findNode(child, targetPath)
						if (found) return found
					}
				}
				return null
			}

			return findNode(rootNode, path)
		},

		getNodeById: (id) => {
			const { rootNode } = get()
			if (!rootNode) return null

			const findNode = (node: FileSystemNode, targetId: string): FileSystemNode | null => {
				if (node.id === targetId) return node
				if (node.children) {
					for (const child of node.children) {
						const found = findNode(child, targetId)
						if (found) return found
					}
				}
				return null
			}

			return findNode(rootNode, id)
		},

		cleanup: () => {
			const { worker } = get()
			if (worker) {
				// Cleanup worker resources if needed
			}
			set({
				rootNode: null,
				selectedNodes: new Set(),
				expandedNodes: new Set(),
				worker: null
			})
		}
	}))
)
