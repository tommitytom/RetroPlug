import { useEffect, useRef, useState, useCallback } from 'react'
import { useOPFSStore } from '../stores/FileSystemStore'
import type { FileSystemNode } from '../stores/types'
import { ZipArchiveHandler } from '../stores/zip-handler'
import '../styles/FileTree.css'

interface TreeNodeProps {
	node: FileSystemNode
	level: number
	onNodeClick: (node: FileSystemNode, event: React.MouseEvent) => void
	onNodeDoubleClick: (node: FileSystemNode) => void
	onToggleExpand: (nodeId: string) => void
	selectedNodes: Set<string>
	expandedNodes: Set<string>
	dragOverNode: string | null
	onDragStart?: (event: React.DragEvent, node: FileSystemNode) => void
	onDrop?: (event: React.DragEvent, targetNode: FileSystemNode) => void
	onDragOver?: (event: React.DragEvent) => void
	onDragEnter?: (event: React.DragEvent, node: FileSystemNode) => void
	onDragLeave?: (event: React.DragEvent) => void
}

function TreeNode({
	node,
	level,
	onNodeClick,
	onNodeDoubleClick,
	onToggleExpand,
	selectedNodes,
	expandedNodes,
	dragOverNode,
	onDragStart,
	onDrop,
	onDragOver,
	onDragEnter,
	onDragLeave
}: TreeNodeProps) {
	const isSelected = selectedNodes.has(node.id)
	const isExpanded = expandedNodes.has(node.id)
	const isDragOver = dragOverNode === node.id

	const handleClick = (event: React.MouseEvent) => {
		onNodeClick(node, event)
	}

	const handleDoubleClick = () => {
		onNodeDoubleClick(node)
	}

	const handleToggleExpand = (event: React.MouseEvent) => {
		event.stopPropagation()
		onToggleExpand(node.id)
	}

	const getIcon = () => {
		if (node.type === 'directory') {
			return isExpanded ? '📁' : '📂'
		} else if (node.type === 'archive') {
			return isExpanded ? '📦' : '🗃️'
		} else {
			// File icons based on extension
			const ext = node.name.split('.').pop()?.toLowerCase()
			switch (ext) {
				case 'js': case 'ts': case 'jsx': case 'tsx':
					return '📄'
				case 'json':
					return '📋'
				case 'md': case 'txt':
					return '📝'
				case 'png': case 'jpg': case 'jpeg': case 'gif': case 'svg':
					return '🖼️'
				case 'mp3': case 'wav': case 'ogg':
					return '🎵'
				case 'zip': case 'tar': case 'gz':
					return '📦'
				default:
					return '📄'
			}
		}
	}

	const formatSize = (size: number | undefined) => {
		if (!size) return ''
		if (size < 1024) return `${size}B`
		if (size < 1024 * 1024) return `${(size / 1024).toFixed(1)}KB`
		return `${(size / (1024 * 1024)).toFixed(1)}MB`
	}

	return (
		<>
			<div
				className={`file-tree-item ${isSelected ? 'selected' : ''} ${isDragOver ? 'drag-over' : ''}`}
				data-level={level}
				data-node-type={node.type}
				onClick={handleClick}
				onDoubleClick={handleDoubleClick}
				draggable={true}
				onDragStart={(e) => onDragStart?.(e, node)}
				onDrop={(e) => onDrop?.(e, node)}
				onDragOver={onDragOver}
				onDragEnter={(e) => onDragEnter?.(e, node)}
				onDragLeave={onDragLeave}
			>
				{(node.type === 'directory' || node.type === 'archive') && (
					<span
						className="file-tree-item-icon file-tree-expand-icon"
						onClick={handleToggleExpand}
					>
						{isExpanded ? '▼' : '▶'}
					</span>
				)}
				<span className="file-tree-item-icon">
					{getIcon()}
				</span>
				<span className="file-tree-item-name">
					{node.name}
				</span>
				{node.size && (
					<span className="file-tree-item-size">
						{formatSize(node.size)}
					</span>
				)}
			</div>
			{(node.type === 'directory' || node.type === 'archive') && isExpanded && node.children && (
				<>
					{node.children.map((child) => (
						<TreeNode
							key={child.id}
							node={child}
							level={level + 1}
							onNodeClick={onNodeClick}
							onNodeDoubleClick={onNodeDoubleClick}
							onToggleExpand={onToggleExpand}
							selectedNodes={selectedNodes}
							expandedNodes={expandedNodes}
							dragOverNode={dragOverNode}
							onDragStart={onDragStart}
							onDrop={onDrop}
							onDragOver={onDragOver}
							onDragEnter={onDragEnter}
							onDragLeave={onDragLeave}
						/>
					))}
				</>
			)}
		</>
	)
}

export function FileExplorer() {
	const {
		rootNode,
		selectedNodes,
		expandedNodes,
		loading,
		error,
		initialize,
		toggleNode,
		selectNode,
		clearSelection,
		listPath,
		writePath,
		createDirectory,
		movePath,
		copyPath,
		refreshNode,
		registerArchiveHandler
	} = useOPFSStore()

	const [draggedNode, setDraggedNode] = useState<FileSystemNode | null>(null)
	const [dragOverNode, setDragOverNode] = useState<string | null>(null)
	const [isDragOverContainer, setIsDragOverContainer] = useState(false)
	const dropZoneRef = useRef<HTMLDivElement>(null)

	useEffect(() => {
		initialize().then(() => {
			registerArchiveHandler(new ZipArchiveHandler())
			// Register other archive handlers as needed
		})
	}, [initialize])

	// Load children when a directory or archive is expanded
	const handleToggleExpand = useCallback(async (nodeId: string) => {
		const node = findNodeById(rootNode, nodeId)
		if (!node) return

		toggleNode(nodeId)

		// If expanding and no children loaded yet, load them
		if (!expandedNodes.has(nodeId) && (node.type === 'directory' || node.type === 'archive') && !node.children) {
			try {
				await refreshNode(node.path)
			} catch (error) {
				console.error('Failed to load directory/archive children:', error)
			}
		}
	}, [rootNode, expandedNodes, toggleNode, refreshNode])

	const findNodeById = (root: FileSystemNode | null, id: string): FileSystemNode | null => {
		if (!root) return null
		if (root.id === id) return root
		if (root.children) {
			for (const child of root.children) {
				const found = findNodeById(child, id)
				if (found) return found
			}
		}
		return null
	}

	const handleNodeClick = useCallback((node: FileSystemNode, event: React.MouseEvent) => {
		const isCtrlClick = event.ctrlKey || event.metaKey
		selectNode(node.id, isCtrlClick)
	}, [selectNode])

	const handleNodeDoubleClick = useCallback((node: FileSystemNode) => {
		if (node.type === 'directory' || node.type === 'archive') {
			handleToggleExpand(node.id)
		} else {
			// Handle file opening here
			console.log('Opening file:', node.path)
		}
	}, [handleToggleExpand])

	const handleDragStart = useCallback((event: React.DragEvent, node: FileSystemNode) => {
		setDraggedNode(node)
		event.dataTransfer.effectAllowed = 'move'
		event.dataTransfer.setData('text/plain', node.path)

		// Add visual feedback
		event.currentTarget.classList.add('dragging')
	}, [])

	const handleDragOver = useCallback((event: React.DragEvent) => {
		event.preventDefault()
		event.dataTransfer.dropEffect = 'move'
	}, [])

	const handleDragEnter = useCallback((event: React.DragEvent, node: FileSystemNode) => {
		if (node.type === 'directory' || node.type === 'archive') {
			setDragOverNode(node.id)
		}
	}, [])

	const handleDragLeave = useCallback((event: React.DragEvent) => {
		// Only clear if we're actually leaving the element
		if (!event.currentTarget.contains(event.relatedTarget as Node)) {
			setDragOverNode(null)
		}
	}, [])

	const handleDrop = useCallback(async (event: React.DragEvent, targetNode: FileSystemNode) => {
		event.preventDefault()
		setDragOverNode(null)

		if (targetNode.type !== 'directory' && targetNode.type !== 'archive') return

		// Handle dropped files from external sources
		const files = Array.from(event.dataTransfer.files)
		if (files.length > 0) {
			for (const file of files) {
				try {
					const arrayBuffer = await file.arrayBuffer()
					const targetPath = `${targetNode.path}/${file.name}`
					await writePath(targetPath, arrayBuffer)
				} catch (error) {
					console.error('Failed to upload file:', error)
				}
			}
			return
		}

		// Handle internal node movement
		if (draggedNode && draggedNode.id !== targetNode.id) {
			try {
				const targetPath = `${targetNode.path}/${draggedNode.name}`
				await movePath(draggedNode.path, targetPath)
			} catch (error) {
				console.error('Failed to move file:', error)
			}
		}

		setDraggedNode(null)
	}, [draggedNode, writePath, movePath])

	// Handle external file drops on the entire container
	const handleContainerDrop = useCallback(async (event: React.DragEvent) => {
		event.preventDefault()
		setIsDragOverContainer(false)

		const files = Array.from(event.dataTransfer.files)
		if (files.length > 0 && rootNode) {
			for (const file of files) {
				try {
					const arrayBuffer = await file.arrayBuffer()
					const targetPath = `/${file.name}`
					await writePath(targetPath, arrayBuffer)
				} catch (error) {
					console.error('Failed to upload file:', error)
				}
			}
		}
	}, [rootNode, writePath])

	const handleContainerDragOver = useCallback((event: React.DragEvent) => {
		event.preventDefault()
		setIsDragOverContainer(true)
	}, [])

	const handleContainerDragLeave = useCallback((event: React.DragEvent) => {
		if (!event.currentTarget.contains(event.relatedTarget as Node)) {
			setIsDragOverContainer(false)
		}
	}, [])

	const handleKeyDown = useCallback((event: KeyboardEvent) => {
		if (event.key === 'Delete' && selectedNodes.size > 0) {
			// Handle delete operation - you can implement this
			const nodesToDelete = Array.from(selectedNodes)
			console.log('Delete requested for:', nodesToDelete)
			// TODO: Implement actual delete functionality
			// const deletePromises = nodesToDelete.map(nodeId => {
			//   const node = findNodeById(rootNode, nodeId)
			//   return node ? deletePath(node.path) : Promise.resolve()
			// })
			// Promise.all(deletePromises).catch(console.error)
		}
		if (event.key === 'Escape') {
			clearSelection()
		}
		if (event.key === 'Enter' && selectedNodes.size === 1) {
			const nodeId = Array.from(selectedNodes)[0]
			const node = findNodeById(rootNode, nodeId)
			if (node) {
				handleNodeDoubleClick(node)
			}
		}
		// Ctrl+A to select all visible nodes
		if ((event.ctrlKey || event.metaKey) && event.key === 'a') {
			event.preventDefault()
			// This would need more complex logic to select all visible nodes
			console.log('Select all requested')
		}
	}, [selectedNodes, clearSelection, rootNode, handleNodeDoubleClick])

	useEffect(() => {
		document.addEventListener('keydown', handleKeyDown)
		return () => document.removeEventListener('keydown', handleKeyDown)
	}, [handleKeyDown])

	if (loading) {
		return (
			<div className="file-tree-container">
				<div className="file-tree-header">
					<span className="file-tree-header-icon">📁</span>
					<span className="file-tree-header-title">File Explorer</span>
				</div>
				<div className="file-tree-content file-tree-loading">
					Loading...
				</div>
			</div>
		)
	}

	if (error) {
		return (
			<div className="file-tree-container">
				<div className="file-tree-header">
					<span className="file-tree-header-icon">❌</span>
					<span className="file-tree-header-title">Error</span>
				</div>
				<div className="file-tree-content file-tree-error">
					{error}
				</div>
			</div>
		)
	}

	return (
		<div
			className={`file-tree-container ${isDragOverContainer ? 'drag-over' : ''}`}
			ref={dropZoneRef}
			onDrop={handleContainerDrop}
			onDragOver={handleContainerDragOver}
			onDragLeave={handleContainerDragLeave}
		>
			<div className="file-tree-header">
				<span className="file-tree-header-icon">📁</span>
				<span className="file-tree-header-title">File Explorer</span>
				<div className="file-tree-header-status">
					{selectedNodes.size > 0 && `${selectedNodes.size} selected`}
				</div>
			</div>
			<div className="file-tree-content">
				{rootNode && (
					<TreeNode
						node={rootNode}
						level={0}
						onNodeClick={handleNodeClick}
						onNodeDoubleClick={handleNodeDoubleClick}
						onToggleExpand={handleToggleExpand}
						selectedNodes={selectedNodes}
						expandedNodes={expandedNodes}
						dragOverNode={dragOverNode}
						onDragStart={handleDragStart}
						onDrop={handleDrop}
						onDragOver={handleDragOver}
						onDragEnter={handleDragEnter}
						onDragLeave={handleDragLeave}
					/>
				)}
				{!rootNode && (
					<div className="file-tree-empty">
						No files found
					</div>
				)}
			</div>
		</div>
	)
}
