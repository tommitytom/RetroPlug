import { useCallback, useEffect, useState } from 'react';

import { useDocument } from '../contexts/DocumentContext';
import { useModal } from '../contexts/ModalContext';
import { useRetroPlug } from '../contexts/RetroPlugContext';
import type { FileSystemWorkerAPI } from '../filesystem/FileSystemWorker';
import { useContextMenu } from '../hooks/useContextMenu';
import { RomSelectDialog } from './Dialogs/RomSelectDialog';
import { ContextMenu } from './Menu/ContextMenu';
import type { MenuItem } from './Menu/types';
import { CreateFolderDialog } from './Dialogs/CreateFolderDialog';
import { downloadArrayBuffer, openFileCopyDialog } from '../utils/FileUtil';
import type { FileSystemNode } from '../filesystem/types';

const getIcon = (section: string) => {
	switch (section) {
		case 'roms':
			return '▣';
		case 'savs':
			return '◉';
		case 'kits':
			return '◆';
		case 'samples':
			return '♪';
		default:
			return '▢';
	}
};

const getFileIcon = (fileName: string) => {
	const ext = fileName.split('.').pop()?.toLowerCase();
	switch (ext) {
		case 'gb':
		case 'gbc':
		case 'rom':
			return '▣';
		case 'sav':
			return '◉';
		case 'kit':
			return '◆';
		case 'wav':
		case 'mp3':
		case 'ogg':
		case 'aiff':
			return '♪';
		default:
			return '▢';
	}
};

interface FileTreeNodeProps {
	node: FileSystemNode;
	sectionId: string;
	depth: number;
	basePath?: string;
	onDoubleClick: (sectionId: string, itemName: string) => void;
	onContextMenu: (sectionId: string, itemPath: string | null, event: React.MouseEvent) => void;
	onDragStart: (event: React.DragEvent, sectionId: string, itemPath: string) => void;
	onDragEnd: (event: React.DragEvent) => void;
}

const FileTreeNode: React.FC<FileTreeNodeProps> = ({
	node,
	sectionId,
	depth,
	basePath = '',
	onDoubleClick,
	onContextMenu,
	onDragStart,
	onDragEnd
}) => {
	const [isExpanded, setIsExpanded] = useState(false);
	const hasChildren = node.type === 'directory' && node.children && node.children.length > 0;
	const paddingLeft = `${2 + depth * 16}px`;
	const currentPath = basePath ? `${basePath}/${node.name}` : node.name;

	const toggleExpanded = () => {
		if (hasChildren) {
			setIsExpanded(!isExpanded);
		}
	};

	const handleItemDoubleClick = () => {
		if (node.type === 'directory') {
			toggleExpanded();
		} else {
			onDoubleClick(sectionId, currentPath);
		}
	};

	return (
		<div>
			<div
				draggable={true}
				className="flex cursor-pointer items-center py-1 text-sm text-gray-300 transition-colors duration-200 hover:bg-gray-700 hover:text-white"
				style={{ paddingLeft }}
				onDoubleClick={handleItemDoubleClick}
				onContextMenu={(event) => onContextMenu(sectionId, currentPath, event)}
				onDragStart={(event) => onDragStart(event, sectionId, currentPath)}
				onDragEnd={onDragEnd}
			>
				{hasChildren && (
					<span className="mr-1 cursor-pointer text-xs text-white" onClick={toggleExpanded}>
						<div className="mr-2 flex h-3 w-3 items-center justify-center">
							{isExpanded ? (
								<div className="h-0 w-0 border-t-6 border-r-4 border-l-4 border-t-white border-r-transparent border-l-transparent" />
							) : (
								<div className="h-0 w-0 border-t-4 border-b-4 border-l-6 border-t-transparent border-b-transparent border-l-white" />
							)}
						</div>
					</span>
				)}
				{!hasChildren && <span className="mr-1 w-3" />}
				<span className="mr-2 text-xs text-white">
					{node.type === 'directory' ? '▢' : getFileIcon(node.name)}
				</span>
				<span className="flex-1">{node.name}</span>
			</div>
			{hasChildren && isExpanded && (
				<div>
					{node.children?.map((childNode, index) => (
						<FileTreeNode
							key={`${childNode.id || index}`}
							node={childNode}
							sectionId={sectionId}
							depth={depth + 1}
							basePath={currentPath}
							onDoubleClick={onDoubleClick}
							onContextMenu={onContextMenu}
							onDragStart={onDragStart}
							onDragEnd={onDragEnd}
						/>
					))}
				</div>
			)}
		</div>
	);
};

interface ISection {
	id: string;
	name: string;
	extensions: string[];
	recurse: boolean;
}

const sections: ISection[] = [
	{ id: 'roms', name: 'Roms', extensions: ['.gb', '.gbc'], recurse: false },
	{ id: 'savs', name: 'Savs', extensions: ['.sav'], recurse: false },
	{ id: 'kits', name: 'Kits', extensions: ['.kit'], recurse: false },
	{ id: 'samples', name: 'Samples', extensions: ['.wav', '.mp3', '.ogg', '.aiff'], recurse: true },
];

async function ensureExists(fileSystem: FileSystemWorkerAPI, dirName: string) {
	if (!(await fileSystem.isDirectory(`/${dirName}`))) {
		console.log(`Creating /${dirName} directory...`);
		await fileSystem.createDirectory(`/${dirName}`);
	}
}

async function getFileList(fileSystem: FileSystemWorkerAPI): Promise<Record<string, FileSystemNode[]>> {
	const result: Record<string, FileSystemNode[]> = {};

	for (const section of sections) {
		await ensureExists(fileSystem, section.id);
		result[section.id] = (await fileSystem.listPath(`/${section.id}`, section.recurse, section.id === 'savs' ? '.sav' : undefined)).children || [];
	}

	return {
		roms: (await fileSystem.listPath(`/roms`)).children || [],
		savs: (await fileSystem.listPath(`/savs`, false, '.sav')).children/*?.filter((f) => f.name.endsWith('.sav'))*/ || [],
		kits: (await fileSystem.listPath(`/kits`)).children || [],
		samples: (await fileSystem.listPath(`/samples`, true)).children || [],
	};
}

async function createImportDialog(fileSystem: FileSystemWorkerAPI, section: string, extensions: string): Promise<boolean> {
	try {
		await openFileCopyDialog(fileSystem, `/${section}`, extensions);
		return true;
	} catch (error) {
		console.error(`Error adding ${section}:`, error);
	}

	return false;
}

export const ProjectExplorer: React.FC = () => {
	const { fileSystem, project } = useRetroPlug();
	const { setCurrentDocument } = useDocument();
	const { openModal, closeModal } = useModal();
	const [expandedSections, setExpandedSections] = useState<Set<string>>(new Set(sections.map((s) => s.id)));
	const [sectionData, setSectionData] = useState<Record<string, FileSystemNode[]>>({});
	const contextMenu = useContextMenu();
	const [version, setVersion] = useState(0);
	const [isLoaded, setIsLoaded] = useState(false);

	useEffect(() => {
		setIsLoaded(false);
		getFileList(fileSystem)
			.then((data) => {
				setSectionData(data);
				// Small delay to ensure smooth fade-in animation
				setTimeout(() => setIsLoaded(true), 100);
			})
			.catch(console.error);
	}, [fileSystem, version]);

	const toggleSection = (section: string) => {
		setExpandedSections((prev) => {
			const newSet = new Set(prev);
			if (newSet.has(section)) {
				newSet.delete(section);
			} else {
				newSet.add(section);
			}
			return newSet;
		});
	};

	const handleDoubleClick = useCallback(async (section: string, item: string) => {
		console.log(`Double clicked on ${item} in section ${section}`);

		if (section === 'savs') {
			function setDocument(name: string) {
				setCurrentDocument({
					id: name,
					title: name,
					content: project,
					type: 'emulator',
					isDirty: false,
				});
			}

			// is there a project file?
			const projectFile = item.replace('.sav', '.rplg');
			if (await fileSystem.fileExists(`/savs/${projectFile}`)) {
				// TODO: Check if the rom exists!

				project.loadFromFile(`/savs/${projectFile}`);
				setDocument(item);
			} else {
				openModal({
					title: 'Choose a rom',
					content: (
						<RomSelectDialog
							savName={item}
							onSelect={(path) => {
								project.loadFromPaths(['/roms/' + path, '/savs/' + item]);
								setDocument(item);
								closeModal();
							}}
							onClose={closeModal}
						/>
					),
				});
			}
		}
	}, []);

	function createImportMenuItem(fileSystem: FileSystemWorkerAPI, section: string, extensions: string): MenuItem {
		return {
			id: `add-${section}`,
			label: `Add ${section.charAt(0).toUpperCase() + section.slice(1)}`,
			onClick: async () => {
				if (await createImportDialog(fileSystem, section, extensions)) {
					setVersion((v) => v + 1);
				}
			}
		};
	}

	function createSampleFolderDialog() {
		openModal({
			title: 'Create sample folder',
			content: (
				<CreateFolderDialog
					onSelect={async (path) => {
						closeModal();
						await fileSystem.createDirectory(`/samples/${path}`);
						setVersion((v) => v + 1);
					}}
					onClose={closeModal}
				/>
			),
		});
	}

	function createDownloadMenuItem(section: string, item: string): MenuItem {
		return {
			id: `download-${section}-${item}`,
			label: `Download`,
			onClick: async () => {
				try {
					const fileData = await fileSystem.readPath(`/${section}/${item}`);
					downloadArrayBuffer(fileData, item);
				} catch (error) {
					console.error(`Error downloading ${item}:`, error);
				}
			}
		};
	}

	const handleContextMenu = useCallback((id: string, idx: number, item: string | null, event: React.MouseEvent) => {
		event.preventDefault();
		event.stopPropagation();

		const menuItems: MenuItem[] = [];

		if (id == 'samples' && !item) {
			menuItems.push(
				{
					id: 'create-sample-folder',
					label: 'Create Folder',
					onClick: createSampleFolderDialog,
				},
			);
		} else if (id === 'savs' && item) {
			menuItems.push(
				{
					id: 'package-sav',
					label: 'Export with ROM',
					onClick: async () => {
						console.log('Export!');
					}
				},
				{
					id: 'render-sav',
					label: 'Render...',
					onClick: async () => {
						console.log('Render SAV!');
					}
				}
			);
		} else if (id === 'roms' && !item) {
			menuItems.push(createImportMenuItem(fileSystem, 'roms', sections[idx].extensions.join(',')));
		} else if (id === 'savs' && !item) {
			menuItems.push(createImportMenuItem(fileSystem, 'savs', sections[idx].extensions.join(',')));
		} else if (id === 'kits' && !item) {
			menuItems.push(createImportMenuItem(fileSystem, 'kits', sections[idx].extensions.join(',')));
		} else if (id === 'samples' && item) {
			menuItems.push(createImportMenuItem(fileSystem, 'samples', sections[idx].extensions.join(',')));
		}

		if (item) {
			menuItems.push(createDownloadMenuItem(id, item));
		}

		if (menuItems.length > 0) {
			contextMenu.showContextMenu(event, menuItems);
		}
	}, [fileSystem, openModal, closeModal, contextMenu, setVersion]);

	const handleDragStart = useCallback((event: React.DragEvent, section: string, itemPath: string) => {
		console.log(`Drag started on ${itemPath} in section ${section}`);

		if (section !== 'kits' && section !== 'samples') return;

		const filePath = `/${section}/${itemPath}`;
		// Set the data that will be transferred during drag
		event.dataTransfer.setData('text/plain', JSON.stringify([filePath]));
		event.dataTransfer.effectAllowed = 'move';

		// Add visual feedback
		event.currentTarget.classList.add('opacity-50');
	}, []);

	const handleDragEnd = useCallback((event: React.DragEvent) => {
		// Remove visual feedback
		event.currentTarget.classList.remove('opacity-50');
	}, []);

	const handleAddItemsClick = useCallback(async (event: React.MouseEvent, sectionId: number) => {
		event.preventDefault();
		event.stopPropagation();

		const section = sections[sectionId];
		if (section.id === 'samples') {
			createSampleFolderDialog();
		} else {
			const extensions = sections[sectionId].extensions.join(',');
			if (await createImportDialog(fileSystem, sections[sectionId].id, extensions)) {
				setVersion((v) => v + 1);
			}
		}
	}, [setVersion, fileSystem, closeModal]);

	const handleBackupClick = useCallback(() => {
		// TODO: Implement backup functionality
		console.log('Backup button clicked');
	}, []);

	return (
		<div className="flex h-full w-full flex-col bg-gray-900">
			<div className={`flex-1 overflow-y-auto transition-opacity duration-500 ${isLoaded ? 'opacity-100' : 'opacity-0'}`}>
				{sections.map((section, idx) => (
					<div key={section.id + section.name}>
						<div
							className="flex cursor-pointer items-center bg-gray-800 px-2 py-1 text-sm font-medium text-white transition-colors duration-200 hover:bg-gray-700"
							onClick={() => toggleSection(section.id)}
							onContextMenu={(event) => handleContextMenu(section.id, idx, null, event)}
						>
							<span className="mr-1 cursor-pointer text-xs text-white">
								<div className="mr-2 flex h-3 w-3 items-center justify-center">
									{expandedSections.has(section.id) ? (
										<div className="h-0 w-0 border-t-6 border-r-4 border-l-4 border-t-white border-r-transparent border-l-transparent" />
									) : (
										<div className="h-0 w-0 border-t-4 border-b-4 border-l-6 border-t-transparent border-b-transparent border-l-white" />
									)}
								</div>
							</span>
							<span className="mr-2 text-xs text-white">{getIcon(section.id)}</span>
							<span className="flex-1 font-medium">{section.name}</span>
							<button
								className="rounded-sm px-2 text-sm font-bold text-green-400 transition-colors duration-200 hover:bg-green-600/20 hover:text-green-300"
								onClick={(event) => handleAddItemsClick(event, idx)}
								title="Add Items"
							>
								+
							</button>
						</div>

						{expandedSections.has(section.id) && (
							<div>
								{sectionData[section.id]?.map((item, index) => (
									<FileTreeNode
										key={`${section.id}-${item.id || index}`}
										node={item}
										sectionId={section.id}
										depth={1}
										onDoubleClick={handleDoubleClick}
										onContextMenu={(sectionId, itemPath, event) => handleContextMenu(sectionId, idx, itemPath, event)}
										onDragStart={handleDragStart}
										onDragEnd={handleDragEnd}
									/>
								))}
							</div>
						)}
					</div>
				))}
			</div>
			<div className="border-t border-gray-700 bg-gray-900">
				<button
					className="flex w-full items-center justify-center gap-2 bg-slate-600 px-3 py-1.5 text-xs font-medium text-white transition-colors duration-200 hover:bg-slate-500 focus:outline-none focus:bg-slate-500"
					onClick={handleBackupClick}
					title="Create backup of project files"
				>
					<span className="text-white">▩</span>
					<span>Backup</span>
				</button>
			</div>
			<ContextMenu
				items={contextMenu.items}
				position={contextMenu.position}
				visible={contextMenu.isVisible}
				onClose={contextMenu.hideContextMenu}
				onItemClick={contextMenu.handleItemClick}
			/>
		</div>
	);
};
