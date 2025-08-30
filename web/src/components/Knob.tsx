import React, { useState, useRef, useEffect, useCallback } from 'react';

interface KnobProps {
	min?: number;
	max?: number;
	value?: number;
	defaultValue?: number;
	stepSize?: number;
	smallStepSize?: number;
	label?: string;
	showValue?: boolean;
	valueFormatter?: (value: number) => string;
	onChange?: (value: number) => void;
	size?: number;
}

export const Knob: React.FC<KnobProps> = ({
	min = 0,
	max = 100,
	value: controlledValue,
	defaultValue = 0,
	stepSize = 1,
	smallStepSize,
	label,
	showValue = false,
	valueFormatter = (v) => v.toFixed(1),
	onChange,
	size = 64
}) => {
	const [internalValue, setInternalValue] = useState(defaultValue);
	const [isDragging, setIsDragging] = useState(false);
	const [startY, setStartY] = useState(0);
	const [startValue, setStartValue] = useState(0);
	const knobRef = useRef<SVGSVGElement>(null);

	const value = controlledValue !== undefined ? controlledValue : internalValue;
	const effectiveSmallStepSize = smallStepSize ?? stepSize / 2;

	// Convert value to rotation angle (270 degree sweep, from -225 to 45 degrees)
	// This puts the minimum at bottom-left (7 o'clock) and maximum at bottom-right (5 o'clock)
	const valueToAngle = (val: number): number => {
		const normalized = (val - min) / (max - min);
		return normalized * 270 - 225;
	};

	const angle = valueToAngle(value);

	// Snap value to step size
	const snapToStep = (val: number, step: number): number => {
		const snapped = Math.round(val / step) * step;
		return Math.max(min, Math.min(max, snapped));
	};

	const handleMouseDown = useCallback((e: React.MouseEvent) => {
		e.preventDefault();
		setIsDragging(true);
		setStartY(e.clientY);
		setStartValue(value);
	}, [value]);

	const handleMouseMove = useCallback((e: MouseEvent) => {
		if (!isDragging) return;

		const deltaY = startY - e.clientY;
		const range = max - min;
		const step = e.shiftKey ? effectiveSmallStepSize : stepSize;
		// Adjust sensitivity based on range and whether shift is held
		const baseSensitivity = range / 200; // 200 pixels for full range
		const sensitivity = e.shiftKey ? baseSensitivity * 0.1 : baseSensitivity;

		const rawChange = deltaY * sensitivity;
		const newValue = snapToStep(startValue + rawChange, step);

		if (controlledValue === undefined) {
			setInternalValue(newValue);
		}
		onChange?.(newValue);
	}, [isDragging, startY, startValue, min, max, stepSize, effectiveSmallStepSize, controlledValue, onChange]);

	const handleMouseUp = useCallback(() => {
		setIsDragging(false);
	}, []);

	useEffect(() => {
		if (isDragging) {
			document.addEventListener('mousemove', handleMouseMove);
			document.addEventListener('mouseup', handleMouseUp);
			document.body.style.cursor = 'ns-resize';
			document.body.style.userSelect = 'none';

			return () => {
				document.removeEventListener('mousemove', handleMouseMove);
				document.removeEventListener('mouseup', handleMouseUp);
				document.body.style.cursor = '';
				document.body.style.userSelect = '';
			};
		}
	}, [isDragging, handleMouseMove, handleMouseUp]);

	return (
		<div className="inline-flex flex-col items-center gap-0.5">
			{label && (
				<label className="text-sm font-medium text-gray-700 dark:text-gray-300">
					{label}
				</label>
			)}

			<svg
				ref={knobRef}
				width={size}
				height={size}
				className={`cursor-ns-resize select-none ${isDragging ? 'cursor-grabbing' : 'cursor-grab'}`}
				onMouseDown={handleMouseDown}
			>
				{/* Outer ring */}
				<circle
					cx={size / 2}
					cy={size / 2}
					r={size / 2 - 4}
					fill="none"
					stroke="currentColor"
					strokeWidth="2"
					className="text-gray-300 dark:text-gray-600"
				/>

				{/* Track arc */}
				<path
					d={`
						M ${size / 2 + (size / 2 - 8) * Math.cos((-225) * Math.PI / 180)}
						  ${size / 2 + (size / 2 - 8) * Math.sin((-225) * Math.PI / 180)}
						A ${size / 2 - 8} ${size / 2 - 8} 0 1 1
						  ${size / 2 + (size / 2 - 8) * Math.cos((45) * Math.PI / 180)}
						  ${size / 2 + (size / 2 - 8) * Math.sin((45) * Math.PI / 180)}
					`}
					fill="none"
					stroke="currentColor"
					strokeWidth="3"
					strokeLinecap="round"
					className="text-gray-400 dark:text-gray-500"
				/>

				{/* Value arc */}
				{angle > -225 && (
					<path
						d={`
							M ${size / 2 + (size / 2 - 8) * Math.cos((-225) * Math.PI / 180)}
							  ${size / 2 + (size / 2 - 8) * Math.sin((-225) * Math.PI / 180)}
							A ${size / 2 - 8} ${size / 2 - 8} 0 ${angle > -45 ? 1 : 0} 1
							  ${size / 2 + (size / 2 - 8) * Math.cos(angle * Math.PI / 180)}
							  ${size / 2 + (size / 2 - 8) * Math.sin(angle * Math.PI / 180)}
						`}
						fill="none"
						stroke="currentColor"
						strokeWidth="3"
						strokeLinecap="round"
						className="text-blue-500 dark:text-blue-400"
					/>
				)}

				{/* Center knob */}
				<circle
					cx={size / 2}
					cy={size / 2}
					r={size / 3}
					fill="currentColor"
					className="text-gray-700 dark:text-gray-300"
				/>

				{/* Inner circle */}
				<circle
					cx={size / 2}
					cy={size / 2}
					r={size / 3 - 4}
					fill="currentColor"
					className="text-gray-600 dark:text-gray-400"
				/>

				{/* Position indicator */}
				<line
					x1={size / 2}
					y1={size / 2}
					x2={size / 2 + (size / 3 - 8) * Math.cos(angle * Math.PI / 180)}
					y2={size / 2 + (size / 3 - 8) * Math.sin(angle * Math.PI / 180)}
					stroke="currentColor"
					strokeWidth="3"
					strokeLinecap="round"
					className="text-white dark:text-gray-900"
				/>

				{/* Tick marks */}
				{[-225, -180, -135, -90, -45, 0, 45].map((tickAngle) => (
					<line
						key={tickAngle}
						x1={size / 2 + (size / 2 - 12) * Math.cos(tickAngle * Math.PI / 180)}
						y1={size / 2 + (size / 2 - 12) * Math.sin(tickAngle * Math.PI / 180)}
						x2={size / 2 + (size / 2 - 16) * Math.cos(tickAngle * Math.PI / 180)}
						y2={size / 2 + (size / 2 - 16) * Math.sin(tickAngle * Math.PI / 180)}
						stroke="currentColor"
						strokeWidth="1"
						className="text-gray-400 dark:text-gray-500"
					/>
				))}
			</svg>

			{showValue && (
				<div className="text-sm font-mono text-gray-700 dark:text-gray-300">
					{valueFormatter(value)}
				</div>
			)}
		</div>
	);
};
