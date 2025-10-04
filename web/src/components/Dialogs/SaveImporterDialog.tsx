import React, { useState, useRef, useEffect } from 'react';
import { useRetroPlug } from '../../contexts/RetroPlugContext';
import { fromArrayBuffer } from '../../utils/NativeUtil';

interface RomInfo {
	fileName: string;
	name: string;
	version?: string;
	tags?: string;
	isStock?: boolean;
}

interface SongInfo {
	name: string;
	version: number;
	sourceFile: string;
}

interface FileInfo {
	name: string;
	type: 'rom' | 'sav';
	file: File;
}

export const SaveImporterDialog: React.FC<{
	onImport: (roms: RomInfo[], songs: SongInfo[]) => void;
	onClose: () => void;
}> = ({ onImport, onClose }) => {
	const { module, fileSystem } = useRetroPlug();
	const [files, setFiles] = useState<FileInfo[]>([]);
	const [roms, setRoms] = useState<RomInfo[]>([]);
	const [songs, setSongs] = useState<SongInfo[]>([]);
	const [existingRoms, setExistingRoms] = useState<RomInfo[]>([]);
	const [duplicateRoms, setDuplicateRoms] = useState<RomInfo[]>([]);
	const [showDuplicates, setShowDuplicates] = useState(false);
	const [isDragOver, setIsDragOver] = useState(false);
	const fileInputRef = useRef<HTMLInputElement>(null);

	// Extract ROM information from filesystem data
	const extractRomInfoFromData = async (fileName: string, data: ArrayBuffer): Promise<RomInfo> => {
		const romData = fromArrayBuffer(module, data);
		const romName = module.getRomName(romData);
		const isLsdj = romName.toLowerCase().includes('lsdj');
		const romInfo = module.getLsdjRomInfo(romData);

		let extractedRom: RomInfo;

		if (isLsdj) {
			extractedRom = {
				fileName: fileName,
				name: romInfo.name,
				version: romInfo.version.major + '.' + romInfo.version.minor + '.' + romInfo.version.patch,
				tags: romInfo.tags,
				isStock: romInfo.isStock,
			};
		} else {
			extractedRom = {
				fileName: fileName,
				name: romName,
			};
		}

		romInfo.delete();
		romData.delete();

		return extractedRom;
	};

	// Load existing ROMs from filesystem
	const loadExistingRoms = async (): Promise<void> => {
		try {
			const romsListing = await fileSystem.listPath('/roms');
			const items = romsListing?.children;

			if (!items) return;

			const existingRomInfos: RomInfo[] = [];

			for (const item of items) {
				if (item.name.toLowerCase().endsWith('.gb')) {
					try {
						const data = await fileSystem.readPath(`/roms/${item.name}`);
						const romInfo = await extractRomInfoFromData(item.name, data);
						existingRomInfos.push(romInfo);
					} catch (error) {
						console.warn(`Failed to read ROM ${item.name}:`, error);
					}
				}
			}

			setExistingRoms(existingRomInfos);
		} catch (error) {
			console.warn('Failed to load existing ROMs:', error);
		}
	};

	// Extract ROM information
	const extractRomInfo = async (file: File): Promise<RomInfo> => {
		const romData = fromArrayBuffer(module, await file.arrayBuffer());
		const romInfo = module.getLsdjRomInfo(romData);

		const extractedRom: RomInfo = {
			fileName: file.name,
			name: romInfo.name,
			version: romInfo.version.major + '.' + romInfo.version.minor + '.' + romInfo.version.patch,
			tags: romInfo.tags,
			isStock: romInfo.isStock,
		};

		romInfo.delete();
		romData.delete();

		return extractedRom;
	};

	// Extract SAV file information
	const extractSavInfo = async (file: File): Promise<SongInfo[]> => {
		const buffer = fromArrayBuffer(module, await file.arrayBuffer());
		const sav = new module.NativeLsdjSav(buffer);
		const extractedSongs: SongInfo[] = [];

		if (sav.isValid) {
			for (let i = 0; i < sav.totalProjectCount; i++) {
				const project = sav.getProject(i);
				if (project.isValid) {
					const projectName = project.getName();
					const projectVersion = project.version;

					extractedSongs.push({
						name: projectName,
						version: projectVersion,
						sourceFile: file.name,
					});
				}

				project.delete();
			}
		}

		sav.delete();
		buffer.delete();

		return extractedSongs;
	};

	// Load existing ROMs when component mounts
	useEffect(() => {
		loadExistingRoms();
	}, []);

	const processFile = async (file: FileInfo): Promise<void> => {
		if (file.type === 'rom') {
			const romInfo = await extractRomInfo(file.file);
			// Check for duplicate ROMs (same name and version) against both imported ROMs and existing filesystem ROMs
			const isDuplicateInImported = roms.some(existingRom =>
				existingRom.fileName === romInfo.fileName &&
				existingRom.version === romInfo.version
			);
			const isDuplicateInFilesystem = existingRoms.some(existingRom =>
				existingRom.fileName === romInfo.fileName &&
				existingRom.version === romInfo.version
			);
			const isDuplicate = isDuplicateInImported || isDuplicateInFilesystem;

			if (isDuplicate) {
				// Add to duplicates list instead of ignoring
				setDuplicateRoms(prev => {
					const alreadyInDuplicates = prev.some(dupRom =>
						dupRom.fileName === romInfo.fileName &&
						dupRom.version === romInfo.version
					);
					return alreadyInDuplicates ? prev : [...prev, romInfo];
				});
			} else {
				// Add to regular ROMs list
				setRoms(prev => [...prev, romInfo]);
			}
		} else {
			const songInfos = await extractSavInfo(file.file);
			// Filter out duplicate songs (same name and version)
			setSongs(prev => {
				const newSongs = songInfos.filter(newSong =>
					!prev.some(existingSong =>
						existingSong.name === newSong.name &&
						existingSong.version === newSong.version
					)
				);
				return [...prev, ...newSongs];
			});
		}
	};

	const handleFiles = async (fileList: FileList) => {
		const validFiles = Array.from(fileList).filter(file => {
			const extension = file.name.split('.').pop()?.toLowerCase();
			return extension === 'gb' || extension === 'sav';
		});

		const newFiles = validFiles.map(file => {
			const extension = file.name.split('.').pop()?.toLowerCase();
			const type: 'rom' | 'sav' = extension === 'gb' ? 'rom' : 'sav';
			return { name: file.name, type, file };
		});

		// Process each file and extract information
		await Promise.all(newFiles.map(fileInfo => processFile(fileInfo)));

		setFiles(prev => [...prev, ...newFiles]);
	};

	const handleDragOver = (e: React.DragEvent) => {
		e.preventDefault();
		setIsDragOver(true);
	};

	const handleDragLeave = (e: React.DragEvent) => {
		e.preventDefault();
		setIsDragOver(false);
	};

	const handleDrop = (e: React.DragEvent) => {
		e.preventDefault();
		setIsDragOver(false);
		handleFiles(e.dataTransfer.files);
	};

	const handleFileInput = (e: React.ChangeEvent<HTMLInputElement>) => {
		if (e.target.files) {
			handleFiles(e.target.files);
		}
	};

	const removeFile = (index: number) => {
		setFiles(prev => prev.filter((_, i) => i !== index));
	};

	const handleImport = () => {
		if (roms.length > 0 || songs.length > 0) {
			onImport(roms, songs);
		}
	};

	// Group songs by name to detect versions
	const groupedSongs = songs.reduce((acc, song) => {
		if (!acc[song.name]) {
			acc[song.name] = [];
		}
		acc[song.name].push(song);
		return acc;
	}, {} as Record<string, SongInfo[]>);

	const removeRom = (index: number) => {
		setRoms(prev => prev.filter((_, i) => i !== index));
	};

	const removeDuplicateRom = (rom: RomInfo) => {
		setDuplicateRoms(prev => prev.filter(dupRom =>
			!(dupRom.fileName === rom.fileName && dupRom.version === rom.version)
		));
	};

	const removeSongGroup = (songName: string) => {
		setSongs(prev => prev.filter(song => song.name !== songName));
	};

	const hasFiles = roms.length > 0 || songs.length > 0 || duplicateRoms.length > 0;

	return (
		<div
			className={`w-full space-y-6 ${isDragOver ? 'bg-blue-500/5 border-2 border-dashed border-blue-500 rounded-lg p-4' : ''}`}
			onDragOver={handleDragOver}
			onDragLeave={handleDragLeave}
			onDrop={handleDrop}
		>
			{/* Drop zone - only show if no files have been added */}
			{!hasFiles && (
				<div
					className={`border-2 border-dashed rounded-lg p-8 text-center transition-colors ${
						isDragOver
							? 'border-blue-500 bg-blue-500/10'
							: 'border-gray-600 bg-gray-700/50'
					}`}
				>
				<div className="space-y-4">
					<div className="text-4xl text-gray-400">📁</div>
					<div className="space-y-2">
						<p className="text-lg font-medium text-white">
							Drop your LSDJ files here
						</p>
						<p className="text-sm text-gray-400">
							Supports .gb (ROM) and .sav (save) files
						</p>
					</div>
					<button
						type="button"
						onClick={() => fileInputRef.current?.click()}
						className="inline-flex items-center px-4 py-2 bg-blue-600 text-white rounded-lg hover:bg-blue-700 transition-colors"
					>
						Or click to browse files
					</button>
					<input
						ref={fileInputRef}
						type="file"
						multiple
						accept=".gb,.sav"
						onChange={handleFileInput}
						className="hidden"
					/>
				</div>
				</div>
			)}

			{/* Add more files hint when files exist */}
			{hasFiles && !isDragOver && (
				<div className="text-center">
					<p className="text-sm text-gray-400">
						Drop more files here to add them, or{' '}
						<button
							onClick={() => fileInputRef.current?.click()}
							className="text-blue-400 hover:text-blue-300 underline"
						>
							browse for files
						</button>
					</p>
					<input
						ref={fileInputRef}
						type="file"
						multiple
						accept=".gb,.sav"
						onChange={handleFileInput}
						className="hidden"
					/>
				</div>
			)}

			{/* ROMs and Songs Lists - Side by Side */}
			{(roms.length > 0 || duplicateRoms.length > 0 || Object.keys(groupedSongs).length > 0) && (
				<div className="grid grid-cols-1 lg:grid-cols-2 gap-4 w-full">
					{/* ROMs List */}
					<div className="space-y-2">
						<div className="flex items-center justify-between">
							<h3 className="text-sm font-medium text-white">
								ROMs ({roms.length}{duplicateRoms.length > 0 ? ` + ${duplicateRoms.length} duplicates` : ''})
							</h3>
							{duplicateRoms.length > 0 && (
								<label className="flex items-center gap-1 text-xs text-gray-300 cursor-pointer">
									<input
										type="checkbox"
										checked={showDuplicates}
										onChange={(e) => setShowDuplicates(e.target.checked)}
										className="w-3 h-3"
									/>
									Show Duplicates
								</label>
							)}
						</div>
						<div className="bg-gray-800 rounded border border-gray-700 overflow-hidden">
							<div className="max-h-80 overflow-y-auto">
								{(() => {
									const displayRoms = showDuplicates ? [...roms, ...duplicateRoms] : roms;
									return displayRoms.length > 0 ? displayRoms.map((rom, index) => {
										const isDuplicate = duplicateRoms.includes(rom);
										const actualIndex = isDuplicate ? -1 : roms.indexOf(rom); // -1 for duplicates so remove button is disabled

										return (
											<div key={`${rom.fileName}-${index}`} className={`flex items-center justify-between px-2 py-1 border-b border-gray-700 last:border-b-0 hover:bg-gray-700/30 transition-colors ${isDuplicate ? 'opacity-60' : ''}`}>
												<div className="flex items-center gap-2 flex-1 min-w-0">
													<span className={`px-1.5 py-0.5 text-xs font-medium rounded flex-shrink-0 ${
														isDuplicate
															? 'bg-yellow-600 text-white'
															: 'bg-green-600 text-white'
													}`}>
														{isDuplicate ? 'DUP' : 'ROM'}
													</span>
													<div className="flex flex-col min-w-0 flex-1">
														<span className="text-white text-xs font-medium truncate">{rom.fileName}</span>
														<div className="flex items-center gap-1 text-xs">
															<span className="text-gray-300">
																{rom.name}
															</span>
															{rom.tags && (
																<span className="text-gray-400">({rom.tags})</span>
															)}

															{isDuplicate && (
																<span className="px-1 py-0.5 text-xs rounded bg-red-600/80 text-white">
																	Will be ignored
																</span>
															)}
														</div>
													</div>
												</div>
												{!isDuplicate && (
													<button
														onClick={() => removeRom(actualIndex)}
														className="text-gray-400 hover:text-red-400 transition-colors p-0.5 flex-shrink-0 text-sm"
														title="Remove ROM"
													>
														✕
													</button>
												)}
											</div>
										);
									}) : (
										<div className="p-3 text-gray-400 text-xs text-center">No ROMs imported</div>
									);
								})()}
							</div>
						</div>
					</div>

					{/* Songs List */}
					<div className="space-y-2">
						<h3 className="text-sm font-medium text-white">Songs ({songs.length} total, {Object.keys(groupedSongs).length} unique)</h3>
						<div className="bg-gray-800 rounded border border-gray-700 overflow-hidden">
							<div className="max-h-80 overflow-y-auto">
								{Object.keys(groupedSongs).length > 0 ? Object.entries(groupedSongs).map(([songName, versions]) => (
									<div key={songName} className="flex items-center justify-between px-2 py-1 border-b border-gray-700 last:border-b-0 hover:bg-gray-700/30 transition-colors">
										<div className="flex items-center gap-2 flex-1 min-w-0">
											<span className="px-1.5 py-0.5 text-xs font-medium bg-blue-600 text-white rounded flex-shrink-0">
												SONG
											</span>
											<div className="flex flex-col min-w-0 flex-1">
												<span className="text-white text-xs font-medium truncate">{songName}</span>
												<div className="flex items-center gap-1 text-xs">
													{versions.length > 1 ? (
														<span className="px-1 py-0.5 text-xs rounded bg-yellow-600/80 text-white">
															{versions.length} versions (v{Math.min(...versions.map(v => v.version))}-v{Math.max(...versions.map(v => v.version))})
														</span>
													) : (
														<span className="text-gray-300">v{versions[0].version}</span>
													)}
													<span className="text-gray-400 text-xs">
														from {versions.length === 1 ? versions[0].sourceFile : `${versions.length} files`}
													</span>
												</div>
											</div>
										</div>
										<button
											onClick={() => removeSongGroup(songName)}
											className="text-gray-400 hover:text-red-400 transition-colors p-0.5 flex-shrink-0 text-sm"
											title="Remove all versions of this song"
										>
											✕
										</button>
									</div>
								)) : (
									<div className="p-3 text-gray-400 text-xs text-center">No songs imported</div>
								)}
							</div>
						</div>
					</div>
				</div>
			)}

			{/* Action buttons */}
			<div className="flex gap-3">
				<button
					type="button"
					onClick={onClose}
					className="flex-1 rounded-lg bg-gray-700 px-4 py-2 text-gray-200 transition-colors hover:bg-gray-600"
				>
					Cancel
				</button>
				<button
					onClick={handleImport}
					disabled={roms.length === 0 && songs.length === 0}
					className="flex-1 rounded-lg bg-blue-600 px-4 py-2 text-white transition-colors hover:bg-blue-700 disabled:cursor-not-allowed disabled:bg-gray-600 disabled:text-gray-400"
				>
					Import {roms.length + songs.length > 0 ? `(${roms.length} ROMs, ${songs.length} songs)` : ''}
				</button>
			</div>
		</div>
	);
};
