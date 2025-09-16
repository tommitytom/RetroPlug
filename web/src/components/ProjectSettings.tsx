import { useState, useEffect, useCallback } from 'react';

export interface GameBoyButton {
	id: string;
	name: string;
	displayName: string;
}

export interface KeyMapping {
	[buttonId: string]: string;
}

const DEFAULT_GAMEBOY_BUTTONS: GameBoyButton[] = [
	{ id: 'up', name: 'RETRO_DEVICE_ID_JOYPAD_UP', displayName: 'Up' },
	{ id: 'down', name: 'RETRO_DEVICE_ID_JOYPAD_DOWN', displayName: 'Down' },
	{ id: 'left', name: 'RETRO_DEVICE_ID_JOYPAD_LEFT', displayName: 'Left' },
	{ id: 'right', name: 'RETRO_DEVICE_ID_JOYPAD_RIGHT', displayName: 'Right' },
	{ id: 'a', name: 'RETRO_DEVICE_ID_JOYPAD_A', displayName: 'A' },
	{ id: 'b', name: 'RETRO_DEVICE_ID_JOYPAD_B', displayName: 'B' },
	{ id: 'select', name: 'RETRO_DEVICE_ID_JOYPAD_SELECT', displayName: 'Select' },
	{ id: 'start', name: 'RETRO_DEVICE_ID_JOYPAD_START', displayName: 'Start' },
];

const DEFAULT_KEY_MAPPINGS: KeyMapping = {
	'up': 'ArrowUp',
	'down': 'ArrowDown',
	'left': 'ArrowLeft',
	'right': 'ArrowRight',
	'a': 'KeyX',
	'b': 'KeyZ',
	'select': 'ShiftRight',
	'start': 'Enter',
};

const getKeyDisplayName = (key: string): string => {
	const keyNames: { [key: string]: string } = {
		'ArrowUp': '↑',
		'ArrowDown': '↓',
		'ArrowLeft': '←',
		'ArrowRight': '→',
		'KeyA': 'A', 'KeyB': 'B', 'KeyC': 'C', 'KeyD': 'D', 'KeyE': 'E',
		'KeyF': 'F', 'KeyG': 'G', 'KeyH': 'H', 'KeyI': 'I', 'KeyJ': 'J',
		'KeyK': 'K', 'KeyL': 'L', 'KeyM': 'M', 'KeyN': 'N', 'KeyO': 'O',
		'KeyP': 'P', 'KeyQ': 'Q', 'KeyR': 'R', 'KeyS': 'S', 'KeyT': 'T',
		'KeyU': 'U', 'KeyV': 'V', 'KeyW': 'W', 'KeyX': 'X', 'KeyY': 'Y',
		'KeyZ': 'Z',
		'Digit0': '0', 'Digit1': '1', 'Digit2': '2', 'Digit3': '3', 'Digit4': '4',
		'Digit5': '5', 'Digit6': '6', 'Digit7': '7', 'Digit8': '8', 'Digit9': '9',
		'Space': 'Space',
		'Enter': 'Enter',
		'ShiftLeft': 'L-Shift',
		'ShiftRight': 'R-Shift',
		'ControlLeft': 'L-Ctrl',
		'ControlRight': 'R-Ctrl',
		'AltLeft': 'L-Alt',
		'AltRight': 'R-Alt',
		'Tab': 'Tab',
		'Escape': 'Esc',
		'Backspace': 'Backspace',
	};
	return keyNames[key] || key;
};

interface KeyMappingRowProps {
	button: GameBoyButton;
	mappedKey: string;
	isAssigning: boolean;
	onAssign: (buttonId: string) => void;
	onClear: (buttonId: string) => void;
}

const KeyMappingRow: React.FC<KeyMappingRowProps> = ({
	button,
	mappedKey,
	isAssigning,
	onAssign,
	onClear
}) => {
	return (
		<div className="flex items-center justify-between py-1 px-2 hover:bg-gray-700 rounded-sm text-sm">
			<div className="flex items-center gap-2">
				<span className="font-medium text-white min-w-[50px]">{button.displayName}</span>
			</div>
			<div className="flex items-center gap-1">
				<div className={`px-2 py-1 rounded-sm text-xs font-mono min-w-[70px] text-center ${
					isAssigning
						? 'bg-blue-600 text-white animate-pulse'
						: 'bg-gray-600 text-gray-200'
				}`}>
					{isAssigning ? 'Press key...' : getKeyDisplayName(mappedKey)}
				</div>
				<button
					onClick={() => onAssign(button.id)}
					disabled={isAssigning}
					className="px-2 py-1 bg-blue-700/60 hover:bg-blue-600/80 disabled:bg-gray-500 text-blue-100 hover:text-white text-xs rounded-sm transition-colors"
				>
					{isAssigning ? 'Listening' : 'Assign'}
				</button>
				<button
					onClick={() => onClear(button.id)}
					disabled={isAssigning}
					className="px-2 py-1 bg-red-700/50 hover:bg-red-600/70 disabled:bg-gray-500 text-red-100 hover:text-white text-xs rounded-sm transition-colors"
				>
					Clear
				</button>
			</div>
		</div>
	);
};

export const ProjectSettings: React.FC = () => {
	const [keyMappings, setKeyMappings] = useState<KeyMapping>(DEFAULT_KEY_MAPPINGS);
	const [isAssigningAll, setIsAssigningAll] = useState(false);
	const [currentAssignButton, setCurrentAssignButton] = useState<string | null>(null);
	const [assignAllIndex, setAssignAllIndex] = useState(0);

	const handleKeyDown = useCallback((event: KeyboardEvent) => {
		if (!currentAssignButton) return;

		event.preventDefault();
		event.stopPropagation();

		const key = event.code;

		setKeyMappings(prev => ({
			...prev,
			[currentAssignButton]: key
		}));

		if (isAssigningAll) {
			const nextIndex = assignAllIndex + 1;
			if (nextIndex < DEFAULT_GAMEBOY_BUTTONS.length) {
				setCurrentAssignButton(DEFAULT_GAMEBOY_BUTTONS[nextIndex].id);
				setAssignAllIndex(nextIndex);
			} else {
				// Finished assigning all
				setCurrentAssignButton(null);
				setIsAssigningAll(false);
				setAssignAllIndex(0);
			}
		} else {
			setCurrentAssignButton(null);
		}
	}, [currentAssignButton, isAssigningAll, assignAllIndex]);

	useEffect(() => {
		if (currentAssignButton) {
			window.addEventListener('keydown', handleKeyDown);
			return () => window.removeEventListener('keydown', handleKeyDown);
		}
	}, [currentAssignButton, handleKeyDown]);

	const handleAssignSingle = (buttonId: string) => {
		if (currentAssignButton) return; // Already assigning
		setCurrentAssignButton(buttonId);
		setIsAssigningAll(false);
	};

	const handleAssignAll = () => {
		if (currentAssignButton) return; // Already assigning
		setIsAssigningAll(true);
		setAssignAllIndex(0);
		setCurrentAssignButton(DEFAULT_GAMEBOY_BUTTONS[0].id);
	};

	const handleClear = (buttonId: string) => {
		if (currentAssignButton) return; // Can't clear while assigning
		setKeyMappings(prev => ({
			...prev,
			[buttonId]: ''
		}));
	};

	const handleResetToDefaults = () => {
		if (currentAssignButton) return; // Can't reset while assigning
		setKeyMappings(DEFAULT_KEY_MAPPINGS);
	};

	const handleCancelAssignment = () => {
		setCurrentAssignButton(null);
		setIsAssigningAll(false);
		setAssignAllIndex(0);
	};

	return (
		<div className="w-full h-full bg-gray-800 text-white p-3">
			<div className="max-w-xl">
				<h2 className="text-lg font-medium mb-3">GameBoy Key Mapping</h2>

				<div className="mb-3 flex gap-1">
					<button
						onClick={handleAssignAll}
						disabled={currentAssignButton !== null}
						className="px-3 py-1 bg-green-700/50 hover:bg-green-600/70 disabled:bg-gray-500 text-green-100 hover:text-white text-sm rounded-sm transition-colors"
					>
						{isAssigningAll ? `Assign All (${assignAllIndex + 1}/${DEFAULT_GAMEBOY_BUTTONS.length})` : 'Assign All'}
					</button>
					<button
						onClick={handleResetToDefaults}
						disabled={currentAssignButton !== null}
						className="px-3 py-1 bg-gray-600 hover:bg-gray-500 disabled:bg-gray-500 text-gray-200 hover:text-white text-sm rounded-sm transition-colors"
					>
						Reset to Defaults
					</button>
					{currentAssignButton && (
						<button
							onClick={handleCancelAssignment}
							className="px-3 py-1 bg-orange-700/50 hover:bg-orange-600/70 text-orange-100 hover:text-white text-sm rounded-sm transition-colors"
						>
							Cancel
						</button>
					)}
				</div>

				<div className="bg-gray-900 rounded-sm border border-gray-700 overflow-hidden">
					<div className="divide-y divide-gray-700">
						{DEFAULT_GAMEBOY_BUTTONS.map(button => (
							<KeyMappingRow
								key={button.id}
								button={button}
								mappedKey={keyMappings[button.id] || ''}
								isAssigning={currentAssignButton === button.id}
								onAssign={handleAssignSingle}
								onClear={handleClear}
							/>
						))}
					</div>
				</div>

				{isAssigningAll && (
					<div className="mt-3 p-2 bg-blue-900/30 rounded-sm border border-blue-600/50">
						<p className="text-xs text-blue-200">
							<strong>Assign All Mode:</strong> Press a key for <strong>{DEFAULT_GAMEBOY_BUTTONS[assignAllIndex]?.displayName}</strong>
							{assignAllIndex < DEFAULT_GAMEBOY_BUTTONS.length - 1 && `, then continue with the next button.`}
						</p>
					</div>
				)}

				<div className="mt-4 text-xs text-gray-400">
					<p className="font-medium mb-1">Instructions:</p>
					<ul className="list-disc list-inside space-y-0.5 leading-tight">
						<li>Click "Assign" next to a button to set its key mapping</li>
						<li>Click "Assign All" to set mappings for all buttons in sequence</li>
						<li>Use "Clear" to remove a key mapping</li>
						<li>Use "Reset to Defaults" to restore original key mappings</li>
					</ul>
				</div>
			</div>
		</div>
	);
};
