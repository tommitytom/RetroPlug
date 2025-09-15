import React, { useState, useCallback } from "react";

interface EditableTextProps {
	value: string;
	onChange?: (newValue: string) => void;
	className?: string;
	placeholder?: string;
	maxLength?: number;
	validator?: (input: string) => string;
	title?: string;
	disabled?: boolean;
}

export const EditableText: React.FC<EditableTextProps> = ({
	value,
	onChange,
	className = "",
	placeholder,
	maxLength,
	validator,
	title,
	disabled = false,
}) => {
	const [isEditing, setIsEditing] = useState(false);
	const [editedValue, setEditedValue] = useState(value);

	const handleClick = useCallback((e: React.MouseEvent) => {
		if (disabled) return;

		e.preventDefault();
		e.stopPropagation();
		setIsEditing(true);
		setEditedValue(value);
	}, [disabled, value]);

	const handleInputClick = useCallback((e: React.MouseEvent) => {
		e.preventDefault();
		e.stopPropagation();
	}, []);

	const handleChange = useCallback((e: React.ChangeEvent<HTMLInputElement>) => {
		const newValue = validator ? validator(e.target.value) : e.target.value;
		setEditedValue(newValue);
	}, [validator]);

	const handleSubmit = useCallback(() => {
		const finalValue = editedValue.trim() || value; // Fallback to original value if empty
		setIsEditing(false);
		if (finalValue !== value && onChange) {
			onChange(finalValue);
		}
	}, [editedValue, value, onChange]);

	const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
		if (e.key === 'Enter') {
			e.preventDefault();
			e.stopPropagation();
			handleSubmit();
		} else if (e.key === 'Escape') {
			setIsEditing(false);
			setEditedValue(value);
		}
	}, [handleSubmit, value]);

	const handleBlur = useCallback(() => {
		handleSubmit();
	}, [handleSubmit]);

	if (isEditing) {
		return (
			<input
				type="text"
				value={editedValue}
				onChange={handleChange}
				onKeyDown={handleKeyDown}
				onBlur={handleBlur}
				onClick={handleInputClick}
				onMouseDown={handleInputClick}
				className={`bg-gray-700 text-white px-1 py-0 rounded text-sm border border-gray-600 focus:border-blue-500 focus:outline-none w-16 ${className}`}
				placeholder={placeholder}
				autoFocus
				maxLength={maxLength}
			/>
		);
	}

	return (
		<span
			className={`${disabled ? '' : 'cursor-pointer hover:bg-gray-700 px-1 py-0 rounded transition-colors duration-200'} ${className}`}
			onClick={handleClick}
			title={disabled ? undefined : (title || "Click to edit")}
		>
			{value}
		</span>
	);
};
