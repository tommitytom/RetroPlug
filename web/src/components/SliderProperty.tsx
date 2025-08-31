import { useState } from 'react';
//import '../styles/SliderFix.css';

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

	const handleSliderChange = (event: React.ChangeEvent<HTMLInputElement>) => {
		const newValue = Number(event.target.value);
		setValue(newValue);
		onChange?.(newValue);
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
		const clampedValue = Math.min(Math.max(newValue, min), max);
		setValue(clampedValue);
		onChange?.(clampedValue);
	};

	return (
		<>
			<input
				type="range"
				min={min}
				max={max}
				step={step}
				value={value}
				onChange={handleSliderChange}
				className="flex-1 h-1 bg-gray-600 rounded-lg appearance-none cursor-pointer"
			/>
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
