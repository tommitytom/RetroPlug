import { useCallback, useEffect, useState } from 'react';

import { useRetroPlug } from '../contexts/RetroPlugContext';
import type { FileSystemWorkerAPI } from '../filesystem/FileSystemWorker';
import { EnumUtils } from '../utils/EnumUtil';
import { PadButtonType, toVirtualKey, VirtualKey } from '../wrapper/Input';
import { Project } from '../wrapper/Project';

const INPUT_CONFIG_VERSION = '1.0.0';

export interface IInputConfig {
	version: string;
	keyboard: Record<string, string>;
	gamepad: Record<string, string>;
}

const GAMEBOY_BUTTONS: PadButtonType[] = [
	PadButtonType.Up,
	PadButtonType.Down,
	PadButtonType.Left,
	PadButtonType.Right,
	PadButtonType.A,
	PadButtonType.B,
	PadButtonType.Select,
	PadButtonType.Start,
];

const DEFAULT_KEY_MAPPINGS: VirtualKey[] = [];
DEFAULT_KEY_MAPPINGS[PadButtonType.Up] = VirtualKey.UpArrow;
DEFAULT_KEY_MAPPINGS[PadButtonType.Down] = VirtualKey.DownArrow;
DEFAULT_KEY_MAPPINGS[PadButtonType.Left] = VirtualKey.LeftArrow;
DEFAULT_KEY_MAPPINGS[PadButtonType.Right] = VirtualKey.RightArrow;
DEFAULT_KEY_MAPPINGS[PadButtonType.A] = VirtualKey.D;
DEFAULT_KEY_MAPPINGS[PadButtonType.B] = VirtualKey.W;
DEFAULT_KEY_MAPPINGS[PadButtonType.Select] = VirtualKey.Shift;
DEFAULT_KEY_MAPPINGS[PadButtonType.Start] = VirtualKey.Enter;

const getKeyDisplayName = (key: VirtualKey): string => {
	switch (key) {
		case VirtualKey.UpArrow:
			return '↑';
		case VirtualKey.DownArrow:
			return '↓';
		case VirtualKey.LeftArrow:
			return '←';
		case VirtualKey.RightArrow:
			return '→';
	}

	return EnumUtils.enumToString(VirtualKey, key);
};

function buttonToString(button: PadButtonType): string {
	return EnumUtils.enumToString(PadButtonType, button);
}

function createKeyboardMapping(keyboard: Record<string, string>): VirtualKey[] {
	const mapping: VirtualKey[] = structuredClone(DEFAULT_KEY_MAPPINGS);

	Object.entries(keyboard).map(([keyName, buttonName]) => {
		const key = EnumUtils.stringToEnum(VirtualKey, keyName);
		const button = EnumUtils.stringToEnum(PadButtonType, buttonName);

		if (key !== undefined && button !== undefined) {
			mapping[button] = key;
		}
	});

	return mapping;
}

interface KeyMappingRowProps {
	button: PadButtonType;
	mappedKey: VirtualKey;
	isAssigning: boolean;
	onAssign: (buttonId: PadButtonType) => void;
	onClear: (buttonId: PadButtonType) => void;
}

const KeyMappingRow: React.FC<KeyMappingRowProps> = ({ button, mappedKey, isAssigning, onAssign, onClear }) => {
	return (
		<div className="flex items-center justify-between rounded-sm px-2 py-1 text-sm hover:bg-gray-700">
			<div className="flex items-center gap-2">
				<span className="min-w-[50px] font-medium text-white">{buttonToString(button)}</span>
			</div>
			<div className="flex items-center gap-1">
				<div
					className={`min-w-[70px] rounded-sm px-2 py-1 text-center font-mono text-xs ${
						isAssigning ? 'animate-pulse bg-blue-600 text-white' : 'bg-gray-600 text-gray-200'
					}`}
				>
					{isAssigning ? 'Press key...' : getKeyDisplayName(mappedKey)}
				</div>
				<button
					onClick={() => onAssign(button)}
					disabled={isAssigning}
					className="rounded-sm bg-blue-700/60 px-2 py-1 text-xs text-blue-100 transition-colors hover:bg-blue-600/80 hover:text-white disabled:bg-gray-500"
				>
					{isAssigning ? 'Listening' : 'Assign'}
				</button>
				<button
					onClick={() => onClear(button)}
					disabled={isAssigning}
					className="rounded-sm bg-red-700/50 px-2 py-1 text-xs text-red-100 transition-colors hover:bg-red-600/70 hover:text-white disabled:bg-gray-500"
				>
					Clear
				</button>
			</div>
		</div>
	);
};

const INPUT_CONFIG_PATH = '/config/input.json';

async function loadConfig(fileSystem: FileSystemWorkerAPI): Promise<IInputConfig | null> {
	try {
		if (!(await fileSystem.fileExists(INPUT_CONFIG_PATH))) {
			return null;
		}
		const data = await fileSystem.readPath(INPUT_CONFIG_PATH);
		return JSON.parse(new TextDecoder().decode(data as ArrayBuffer)) as IInputConfig;

		// TODO: Check for migrations
	} catch (ex) {
		console.log('Failed to load input config. Loading defaults.', ex);
		return null;
	}
}

async function saveConfig(fileSystem: FileSystemWorkerAPI, project: Project, config: IInputConfig): Promise<void> {
	const configData = JSON.stringify(config, null, 4);
	const uint8Array = new TextEncoder().encode(configData);
	const arrayBuffer = uint8Array.buffer;
	console.log('Saving input config to ' + INPUT_CONFIG_PATH);
	console.log(configData);
	await fileSystem.createDirectory('/config');
	await fileSystem.writePath(INPUT_CONFIG_PATH, arrayBuffer);
	console.log('Saved input config');
	project.loadConfigs();
}

function createInputConfigFromMappings(mappings: VirtualKey[]): IInputConfig {
	const keyboard: Record<string, string> = {};
	GAMEBOY_BUTTONS.forEach((button) => {
		const key = mappings[button] || VirtualKey.Unknown;
		if (key !== VirtualKey.Unknown) {
			keyboard[EnumUtils.enumToString(VirtualKey, key)] = EnumUtils.enumToString(PadButtonType, button);
		}
	});

	return { version: INPUT_CONFIG_VERSION, keyboard, gamepad: {} };
}

async function saveConfigFromMappings(fileSystem: FileSystemWorkerAPI, project: Project, keyMappings: VirtualKey[]): Promise<void> {
	try {
		const config = createInputConfigFromMappings(keyMappings);
		await saveConfig(fileSystem, project, config);
	} catch (ex) {
		console.error('Failed to save input config.', ex);
	}
}

export const ProjectSettings: React.FC = () => {
	const { fileSystem, project } = useRetroPlug();
	const [keyMappings, setKeyMappings] = useState<VirtualKey[]>(DEFAULT_KEY_MAPPINGS);
	const [isAssigningAll, setIsAssigningAll] = useState(false);
	const [currentAssignButton, setCurrentAssignButton] = useState<PadButtonType | null>(null);
	const [assignAllIndex, setAssignAllIndex] = useState(0);
	const [loaded, setLoaded] = useState(false);

	useEffect(() => {
		loadConfig(fileSystem).then((config) => {
			if (config) {
				setKeyMappings(createKeyboardMapping(config.keyboard));
			} else {
				setKeyMappings(structuredClone(DEFAULT_KEY_MAPPINGS));
				saveConfigFromMappings(fileSystem, project, DEFAULT_KEY_MAPPINGS);
			}

			setLoaded(true);
		});
	}, [fileSystem, setKeyMappings, setLoaded]);

	const handleKeyDown = useCallback(
		(event: KeyboardEvent) => {
			event.preventDefault();
			event.stopPropagation();

			if (!currentAssignButton) return;

			const key = toVirtualKey(event); // Just to ensure the key is valid
			if (key === VirtualKey.Unknown) return;

			const next = {
				...keyMappings,
				[currentAssignButton]: key,
			};

			setKeyMappings(next);

			if (isAssigningAll) {
				const nextIndex = assignAllIndex + 1;
				if (nextIndex < GAMEBOY_BUTTONS.length) {
					setCurrentAssignButton(GAMEBOY_BUTTONS[nextIndex]);
					setAssignAllIndex(nextIndex);
				} else {
					// Finished assigning all
					setCurrentAssignButton(null);
					setIsAssigningAll(false);
					setAssignAllIndex(0);
					saveConfigFromMappings(fileSystem, project, next);
				}
			} else {
				setCurrentAssignButton(null);
				saveConfigFromMappings(fileSystem, project, next);
			}
		},
		[currentAssignButton, isAssigningAll, assignAllIndex, keyMappings, setKeyMappings],
	);

	useEffect(() => {
		if (currentAssignButton) {
			window.addEventListener('keydown', handleKeyDown);
			return () => window.removeEventListener('keydown', handleKeyDown);
		}
	}, [currentAssignButton, handleKeyDown]);

	const handleAssignSingle = (buttonId: PadButtonType) => {
		if (currentAssignButton) return; // Already assigning
		setCurrentAssignButton(buttonId);
		setIsAssigningAll(false);
	};

	const handleAssignAll = () => {
		if (currentAssignButton) return; // Already assigning
		setIsAssigningAll(true);
		setAssignAllIndex(0);
		setCurrentAssignButton(GAMEBOY_BUTTONS[0]);
	};

	const handleClear = (buttonId: PadButtonType) => {
		if (currentAssignButton) return; // Can't clear while assigning
		setKeyMappings((prev) => ({
			...prev,
			[buttonId]: VirtualKey.Unknown,
		}));
	};

	const handleResetToDefaults = () => {
		if (currentAssignButton) return; // Can't reset while assigning
		setKeyMappings(DEFAULT_KEY_MAPPINGS);
		saveConfigFromMappings(fileSystem, project, DEFAULT_KEY_MAPPINGS);
	};

	const handleCancelAssignment = () => {
		setCurrentAssignButton(null);
		setIsAssigningAll(false);
		setAssignAllIndex(0);
	};

	return (
		<div className="h-full w-full p-3 text-white">
			<div className="max-w-xl">
				<h2 className="mb-3 text-lg font-medium">Keyboard Mapping</h2>

				<div className="mb-3 flex gap-1">
					<button
						onClick={handleAssignAll}
						disabled={currentAssignButton !== null}
						className="rounded-sm bg-green-700/50 px-3 py-1 text-sm text-green-100 transition-colors hover:bg-green-600/70 hover:text-white disabled:bg-gray-500"
					>
						{isAssigningAll ? `Assign All (${assignAllIndex + 1}/${GAMEBOY_BUTTONS.length})` : 'Assign All'}
					</button>
					<button
						onClick={handleResetToDefaults}
						disabled={currentAssignButton !== null}
						className="rounded-sm bg-gray-600 px-3 py-1 text-sm text-gray-200 transition-colors hover:bg-gray-500 hover:text-white disabled:bg-gray-500"
					>
						Reset to Defaults
					</button>
					{currentAssignButton && (
						<button
							onClick={handleCancelAssignment}
							className="rounded-sm bg-orange-700/50 px-3 py-1 text-sm text-orange-100 transition-colors hover:bg-orange-600/70 hover:text-white"
						>
							Cancel
						</button>
					)}
				</div>

				<div className="overflow-hidden rounded-sm border border-gray-700 bg-gray-900">
					<div className="divide-y divide-gray-700">
						{GAMEBOY_BUTTONS.map((button) => (
							<KeyMappingRow
								key={button}
								button={button}
								mappedKey={keyMappings[button] || VirtualKey.Unknown}
								isAssigning={currentAssignButton === button}
								onAssign={handleAssignSingle}
								onClear={handleClear}
							/>
						))}
					</div>
				</div>

				{isAssigningAll && (
					<div className="mt-3 rounded-sm border border-blue-600/50 bg-blue-900/30 p-2">
						<p className="text-xs text-blue-200">
							<strong>Assign All Mode:</strong> Press a key for{' '}
							<strong>{buttonToString(GAMEBOY_BUTTONS[assignAllIndex])}</strong>
							{assignAllIndex < GAMEBOY_BUTTONS.length - 1 && `, then continue with the next button.`}
						</p>
					</div>
				)}
			</div>
		</div>
	);
};
