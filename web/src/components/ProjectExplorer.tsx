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
import { openFileCopyDialog } from '../utils/FileUtil';
import { file } from 'jszip';

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

interface ISection {
	id: string;
	name: string;
}

const sections: ISection[] = [
	{ id: 'roms', name: 'Roms' },
	{ id: 'savs', name: 'Savs' },
	{ id: 'kits', name: 'Kits' },
	{ id: 'samples', name: 'Samples' },
];

async function ensureExists(fileSystem: FileSystemWorkerAPI, dirName: string) {
	if (!(await fileSystem.isDirectory(`/${dirName}`))) {
		console.log(`Creating /${dirName} directory...`);
		await fileSystem.createDirectory(`/${dirName}`);
	}
}

async function getFileList(fileSystem: FileSystemWorkerAPI): Promise<Record<string, string[]>> {
	for (const section of sections) {
		await ensureExists(fileSystem, section.id);
	}
	return {
		roms: (await fileSystem.listPath(`/roms`)).children?.map((f) => f.name) || [],
		savs: (await fileSystem.listPath(`/savs`)).children?.map((f) => f.name)?.filter((f) => f.endsWith('.sav')) || [],
		kits: (await fileSystem.listPath(`/kits`)).children?.map((f) => f.name) || [],
		samples: (await fileSystem.listPath(`/samples`)).children?.map((f) => f.name) || [],
	};
}

export const ProjectExplorer: React.FC = () => {
	const { fileSystem, project } = useRetroPlug();
	const { setCurrentDocument } = useDocument();
	const { openModal, closeModal } = useModal();
	const [expandedSections, setExpandedSections] = useState<Set<string>>(new Set(sections.map((s) => s.id)));
	const [sectionData, setSectionData] = useState<Record<string, string[]>>({});
	const contextMenu = useContextMenu();
	const [version, setVersion] = useState(0);

	useEffect(() => {
		getFileList(fileSystem)
			.then((data) => {
				setSectionData(data);
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

	const handleContextMenu = useCallback((id: string, item: string | null, event: React.MouseEvent) => {
		event.preventDefault();
		event.stopPropagation();

		const menuItems: MenuItem[] = [];

		if (id == 'samples' && !item) {
			menuItems.push(
				{
					id: 'create-sample-folder',
					label: 'Create Folder',
					onClick: () => {
						openModal({
							title: 'Choose a name',
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
					},
				},
			);
		} else if (id === 'samples' && item) {
			menuItems.push(
				{
					id: 'add-samples',
					label: 'Add Samples',
					onClick: async () => {
						try {
							await openFileCopyDialog(fileSystem, `/samples/${item}`, '.wav,.mp3,.ogg,.aiff');
							setVersion((v) => v + 1);
						} catch (error) {
							console.error('Error adding samples:', error);
						}
					}
				}
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
		} else if (id === 'savs' && !item) {
			menuItems.push(
				{
					id: 'import-sav',
					label: 'Import...',
					onClick: async () => {
						try {
							await openFileCopyDialog(fileSystem, `/savs`, '.sav');
							setVersion((v) => v + 1);
						} catch (error) {
							console.error('Error adding savs:', error);
						}
					}
				},
			);
		} else if (id === 'roms' && !item) {
			menuItems.push(
				{
					id: 'import-rom',
					label: 'Import...',
					onClick: async () => {
						try {
							await openFileCopyDialog(fileSystem, `/roms`, '.gb,.gbc');
							setVersion((v) => v + 1);
						} catch (error) {
							console.error('Error adding roms:', error);
						}
					}
				},
			);
		} else if (id === 'kits' && !item) {
			menuItems.push(
				{
					id: 'import-kit',
					label: 'Import...',
					onClick: async () => {
						try {
							await openFileCopyDialog(fileSystem, `/kits`, '.kit');
							setVersion((v) => v + 1);
						} catch (error) {
							console.error('Error adding kits:', error);
						}
					}
				},
			);
		}

		if (menuItems.length > 0) {
			contextMenu.showContextMenu(event, menuItems);
		}
	}, []);

	const handleDragEnd = useCallback((event: React.DragEvent) => {
		// Remove visual feedback
		event.currentTarget.classList.remove('opacity-50');
	}, []);

	const handleDragStart = useCallback((event: React.DragEvent, section: string, item: string) => {
		if (section !== 'kits') return;

		const filePath = `/${section}/${item}`;
		// Set the data that will be transferred during drag
		event.dataTransfer.setData('text/plain', JSON.stringify([filePath]));
		event.dataTransfer.effectAllowed = 'move';

		// Add visual feedback
		event.currentTarget.classList.add('opacity-50');
	}, []);

	return (
		<div className="flex h-full w-full flex-col bg-gray-900">
			<div className="flex items-center bg-gray-800 px-2 py-1 text-sm font-medium text-white">
				<span className="font-medium">▢</span>
				<span className="ml-2 font-medium">Project Explorer</span>
			</div>
			<div className="flex-1 overflow-y-auto">
				{sections.map((section) => (
					<div key={section.id + section.name}>
						<div
							className="flex cursor-pointer items-center py-1 pl-2 text-sm text-gray-300 transition-colors duration-200 hover:bg-gray-700 hover:text-white"
							onClick={() => toggleSection(section.id)}
							onContextMenu={(event) => handleContextMenu(section.id, null, event)}
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
							<span className="mr-2 text-xs text-gray-500">
								{sectionData[section.id] ? sectionData[section.id].length : 0}
							</span>
						</div>

						{expandedSections.has(section.id) && (
							<div>
								{sectionData[section.id]?.map((item, index) => (
									<div
										key={index}
										draggable
										className="flex cursor-pointer items-center py-1 pl-6 text-sm text-gray-300 transition-colors duration-200 hover:bg-gray-700 hover:text-white"
										onDoubleClick={() => handleDoubleClick(section.id, item)}
										onContextMenu={(event) => handleContextMenu(section.id, item, event)}
										onDragStart={(event) => handleDragStart(event, section.id, item)}
										onDragEnd={handleDragEnd}
									>
										<span className="mr-2 text-xs text-white">{getFileIcon(item)}</span>
										<span className="flex-1">{item}</span>
									</div>
								))}
							</div>
						)}
					</div>
				))}
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
