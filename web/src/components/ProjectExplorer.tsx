import { useCallback, useEffect, useState } from 'react';
import { useRetroPlug } from '../contexts/RetroPlugContext';
import type { FileSystemWorkerAPI } from '../filesystem/FileSystemWorker';
import { ContextMenu } from './Menu/ContextMenu';
import { useContextMenu } from '../hooks/useContextMenu';
import type { MenuItem } from './Menu/types';
import { useDocument } from '../contexts/DocumentContext';
import { useModal } from '../contexts/ModalContext';

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

const RomSelectDialog: React.FC<{ savName: string; onSelect: (path: string) => void; onClose: () => void }> = ({
	savName,
	onSelect,
	onClose,
}) => {
	const { fileSystem } = useRetroPlug();
	const [romList, setRomList] = useState<string[]>([]);
	const [selectedRom, setSelectedRom] = useState<string | null>(null);

	useEffect(() => {
		fileSystem.listPath('/roms').then((res) => {
			setRomList(res.children?.map((f) => f.name) || []);
		});
	}, []);

	const handleSubmit = (e: React.FormEvent) => {
		e.preventDefault();
		if (!selectedRom) {
			onClose();
		} else {
			onSelect(selectedRom);
		}
	};

	return (
		<div>
			<form onSubmit={handleSubmit} className="space-y-6">
				<div>
					<label className="block text-sm font-medium text-white mb-3">Choose a rom for {savName}:</label>
					<select
						name="rom"
						value={selectedRom || ''}
						onChange={(e) => setSelectedRom(e.target.value)}
						className="w-full rounded-lg bg-gray-700 border border-gray-600 px-3 py-2 text-white placeholder-gray-400 focus:border-blue-500 focus:ring-1 focus:ring-blue-500 focus:outline-none transition-colors"
					>
						<option value="" className="bg-gray-700 text-gray-400">
							Select a ROM...
						</option>
						{romList.map((rom) => (
							<option key={rom} value={rom} className="bg-gray-700 text-white">
								{rom}
							</option>
						))}
					</select>
				</div>
				<div className="flex gap-3">
					<button
						type="button"
						onClick={onClose}
						className="flex-1 rounded-lg bg-gray-700 px-4 py-2 text-gray-200 transition-colors hover:bg-gray-600"
					>
						Cancel
					</button>
					<button
						type="submit"
						disabled={!selectedRom}
						className="flex-1 rounded-lg bg-blue-600 px-4 py-2 text-white transition-colors hover:bg-blue-700 disabled:bg-gray-600 disabled:cursor-not-allowed disabled:text-gray-400"
					>
						Select
					</button>
				</div>
			</form>
		</div>
	);
};

export const ProjectExplorer: React.FC = () => {
	const { fileSystem, project } = useRetroPlug();
	const { setCurrentDocument } = useDocument();
	const { openModal, closeModal } = useModal();
	const [expandedSections, setExpandedSections] = useState<Set<string>>(new Set(sections.map((s) => s.id)));
	const [sectionData, setSectionData] = useState<Record<string, string[]>>({});
	const contextMenu = useContextMenu();

	useEffect(() => {
		getFileList(fileSystem)
			.then((data) => {
				setSectionData(data);
			})
			.catch(console.error);
	}, [fileSystem]);

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
				setDocument(projectFile);
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
					)
				});
			}
		}
	}, []);

	const getIcon = (section: string) => {
		switch (section) {
			case 'roms':
				return '🎮';
			case 'savs':
				return '💾';
			case 'kits':
				return '🥁';
			case 'samples':
				return '🎵';
			default:
				return '📁';
		}
	};

	const getFileIcon = (fileName: string) => {
		const ext = fileName.split('.').pop()?.toLowerCase();
		switch (ext) {
			case 'gb':
			case 'gbc':
			case 'rom':
				return '🎮';
			case 'sav':
				return '💾';
			case 'kit':
				return '🥁';
			case 'wav':
			case 'mp3':
			case 'ogg':
			case 'aiff':
				return '🎵';
			default:
				return '📄';
		}
	};

	const handleContextMenu = useCallback((id: string, item: string, event: React.MouseEvent) => {
		event.preventDefault();
		event.stopPropagation();

		if (id === 'savs') {
			const menuItems: MenuItem[] = [];
		}
	}, []);

	return (
		<div className="flex h-full w-full flex-col bg-gray-900">
			<div className="flex items-center bg-gray-800 px-2 py-1 text-sm font-medium text-white">
				<span className="font-medium">📁</span>
				<span className="ml-2 font-medium">Project Explorer</span>
			</div>
			<div className="flex-1 overflow-y-auto">
				{sections.map((section) => (
					<div key={section.id + section.name}>
						<div
							className="flex cursor-pointer items-center py-1 pl-2 text-sm text-gray-300 transition-colors duration-200 hover:bg-gray-700 hover:text-white"
							onClick={() => toggleSection(section.id)}
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
										className="flex cursor-pointer items-center py-1 pl-6 text-sm text-gray-300 transition-colors duration-200 hover:bg-gray-700 hover:text-white"
										onDoubleClick={() => handleDoubleClick(section.id, item)}
										onContextMenu={(event) => handleContextMenu(section.id, item, event)}
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
