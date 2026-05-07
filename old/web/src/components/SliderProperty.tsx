import { useState, useRef } from 'react';

interface SliderInputProps {
	min?: number;
	max?: number;
	step?: number;
	defaultValue?: number;
	onChange?: (value: number) => void;
}

export const SliderProperty: React.FC<SliderInputProps> = ({
	min = 0,
	max = 100,
	step,
	defaultValue = 50,
	onChange
}) => {
	if (!step) {
		const range = max - min;
		if (range !== 0) {
			step = range / 255;
		} else {
			step = 0;
		}
	}

	const [value, setValue] = useState<number>(defaultValue);
	const sliderRef = useRef<HTMLDivElement>(null);
	const isDraggingRef = useRef<boolean>(false);
	const mouseMoveHandlerRef = useRef<((event: MouseEvent) => void) | null>(null);
	const mouseUpHandlerRef = useRef<((event: MouseEvent) => void) | null>(null);

	// Helper functions
	const clampValue = (val: number): number => {
		return Math.min(Math.max(val, min), max);
	};

	const snapToStep = (val: number): number => {
		if (step === 0) return val;
		return Math.round(val / step) * step;
	};

	const getValueFromPosition = (clientX: number): number => {
		if (!sliderRef.current) return defaultValue;

		const rect = sliderRef.current.getBoundingClientRect();
		const percentage = Math.max(0, Math.min(1, (clientX - rect.left) / rect.width));
		const rawValue = min + (percentage * (max - min));
		return snapToStep(clampValue(rawValue));
	};

	const updateValue = (newValue: number) => {
		setValue(newValue);
		onChange?.(newValue);
	};

	const handleMouseDown = (event: React.MouseEvent) => {
		event.preventDefault();
		isDraggingRef.current = true;
		document.body.style.userSelect = 'none';

		const newValue = getValueFromPosition(event.clientX);
		updateValue(newValue);

		// Create event handlers
		const handleMouseMove = (event: MouseEvent) => {
			if (!isDraggingRef.current) return;
			event.preventDefault();
			const newValue = getValueFromPosition(event.clientX);
			updateValue(newValue);
		};

		const handleMouseUp = () => {
			isDraggingRef.current = false;
			document.body.style.userSelect = '';

			if (mouseMoveHandlerRef.current) {
				document.removeEventListener('mousemove', mouseMoveHandlerRef.current);
				mouseMoveHandlerRef.current = null;
			}
			if (mouseUpHandlerRef.current) {
				document.removeEventListener('mouseup', mouseUpHandlerRef.current);
				mouseUpHandlerRef.current = null;
			}
		};

		// Store handlers in refs
		mouseMoveHandlerRef.current = handleMouseMove;
		mouseUpHandlerRef.current = handleMouseUp;

		// Add event listeners
		document.addEventListener('mousemove', handleMouseMove);
		document.addEventListener('mouseup', handleMouseUp);
	};

	const handleInputChange = (event: React.ChangeEvent<HTMLInputElement>) => {
		const newValue = Number(event.target.value);
		// Ensure the value stays within bounds
		if (newValue >= min && newValue <= max) {
			setValue(newValue);
			onChange?.(newValue);
		}
	};

	const handleInputBlur = (event: React.FocusEvent<HTMLInputElement>) => {
		const newValue = Number(event.target.value);
		// Clamp the value to bounds when input loses focus
		const clampedValue = clampValue(newValue);
		setValue(clampedValue);
		onChange?.(clampedValue);
	};

	// Calculate the percentage for the thumb position
	const percentage = ((value - min) / (max - min)) * 100;

	return (
		<>
			<div
				ref={sliderRef}
				className="flex-1 h-6 relative cursor-pointer select-none"
				onMouseDown={handleMouseDown}
			>
				{/* Track */}
				<div className="absolute top-1/2 left-0 right-0 h-1 bg-gray-600 rounded-lg transform -translate-y-1/2" />

				{/* Fill */}
				<div
					className="absolute top-1/2 left-0 h-1 bg-blue-500 rounded-lg transform -translate-y-1/2"
					style={{ width: `${percentage}%` }}
				/>

				{/* Thumb */}
				<div
					className="absolute top-1/2 w-4 h-4 bg-white border-2 border-blue-500 rounded-full transform -translate-y-1/2 -translate-x-1/2 transition-shadow duration-150 shadow-md hover:shadow-lg active:shadow-lg active:scale-110"
					style={{ left: `${percentage}%` }}
				/>
			</div>

			<input
				type="number"
				min={min}
				max={max}
				step={step}
				value={value}
				onChange={handleInputChange}
				onBlur={handleInputBlur}
				className="w-16 px-1 py-0 text-xs bg-gray-700 text-white border border-gray-600 rounded focus:outline-none focus:border-blue-500"
			/>
		</>
	);
};
