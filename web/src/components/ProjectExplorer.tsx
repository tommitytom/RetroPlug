import { ChevronDown, ChevronRight } from "lucide-react";
import { useCallback, useEffect, useState } from "react";
import { useRetroPlug } from "../contexts/RetroPlugContext";
import type { FileSystemWorkerAPI } from "../filesystem/FileSystemWorker";

interface ISection {
	id: string;
	name: string;
}

const sections: ISection[] = [{ id: 'roms', name: 'Roms' }, { id: 'savs', name: 'Savs' }, { id: 'kits', name: 'Kits' }, { id: 'samples', name: 'Samples' }];

async function ensureExists(fileSystem: FileSystemWorkerAPI, dirName: string) {
	if (!await fileSystem.isDirectory(`/${dirName}`)) {
		console.log(`Creating /${dirName} directory...`);
		await fileSystem.createDirectory(`/${dirName}`);
	}
}

async function getFileList(fileSystem: FileSystemWorkerAPI): Promise<Record<string, string[]>> {
	for (const section of sections) {
		await ensureExists(fileSystem, section.id);
	}
	return {
		roms: (await fileSystem.listPath(`/roms`)).children?.map(f => f.name) || [],
		savs: (await fileSystem.listPath(`/savs`)).children?.map(f => f.name)?.filter(f => f.endsWith('.sav')) || [],
		kits: (await fileSystem.listPath(`/kits`)).children?.map(f => f.name) || [],
		samples: (await fileSystem.listPath(`/samples`)).children?.map(f => f.name) || [],
	};
}

export const ProjectExplorer: React.FC = () => {
	const { fileSystem } = useRetroPlug();
	const [expandedSections, setExpandedSections] = useState<Set<string>>(new Set(sections.map(s => s.id)));
	const [sectionData, setSectionData] = useState<Record<string, string[]>>({});

	useEffect(() => {
		getFileList(fileSystem).then((data) => {
			setSectionData(data);
		}).catch(console.error);
	}, [fileSystem]);

	const toggleSection = (section: string) => {
		setExpandedSections(prev => {
			const newSet = new Set(prev);
			if (newSet.has(section)) {
				newSet.delete(section);
			} else {
				newSet.add(section);
			}
			return newSet;
		});
	};

	const handleDoubleClick = useCallback((section: string, item: string) => {
		console.log(`Double clicked on ${item} in section ${section}`);
	}, []);

	return (
		<div className="flex-1 overflow-y-auto">
			{sections.map((section) => (
				<div key={section.id + section.name} className="border-b border-gray-700">
					<button
						onClick={() => toggleSection(section.id)}
						className="flex w-full items-center justify-between px-4 py-3 transition-colors hover:bg-gray-800"
					>
						<div className="flex items-center gap-2">
							{expandedSections.has(section.id) ? (
								<ChevronDown className="h-4 w-4 text-gray-400" />
							) : (
								<ChevronRight className="h-4 w-4 text-gray-400" />
							)}
							<span className="font-medium text-gray-200">{section.name}</span>
						</div>
						<span className="text-xs text-gray-500">{sectionData[section.id] ? sectionData[section.id].length : 0}</span>
					</button>

					<div
						className={`overflow-hidden transition-all duration-300 ease-in-out ${
							expandedSections.has(section.id) ? 'max-h-96' : 'max-h-0'
						}`}
					>
						<div className="px-4 pb-2">
							{sectionData[section.id]?.map((item, index) => (
								<div
									key={index}
									className="cursor-pointer rounded px-3 py-2 text-sm text-gray-400 transition-colors hover:bg-gray-800 hover:text-gray-200"
									onDoubleClick={() => handleDoubleClick(section.id, item)}
								>
									{item}
								</div>
							))}
						</div>
					</div>
				</div>
			))}
		</div>
	);
};
