// Generic enum operations that work with any enum
export class EnumUtils {
	// 1. Convert from string to enum
	static stringToEnum<T extends Record<string | number, string | number>>(
		enumObj: T,
		str: string
	): T[keyof T] | undefined {
		if (str in enumObj && isNaN(Number(str))) {
			return enumObj[str as keyof T];
		}
		return undefined;
	}

	// Alternative: Direct conversion (throws if invalid)
	static stringToEnumDirect<T extends Record<string | number, string | number>>(
		enumObj: T,
		str: string
	): T[keyof T] {
		return enumObj[str as keyof T];
	}

	// 2. Convert from enum to string
	static enumToString<T extends Record<string | number, string | number>>(
		enumObj: T,
		value: T[keyof T]
	): string {
		// For numeric enums, use reverse lookup
		if (typeof value === 'number') {
			return enumObj[value] as string;
		}
		// For string enums, find the key
		return Object.keys(enumObj).find(key => enumObj[key as keyof T] === value) || '';
	}

	// 3. Get string[] of all available keys
	static getAllKeys<T extends Record<string | number, string | number>>(
		enumObj: T
	): string[] {
		return Object.keys(enumObj).filter(key => isNaN(Number(key)));
	}

	// Get all enum values
	static getAllValues<T extends Record<string | number, string | number>>(
		enumObj: T
	): Array<T[keyof T]> {
		const keys = Object.keys(enumObj);
		// For numeric enums, filter out string keys and return numbers
		if (keys.some(key => !isNaN(Number(enumObj[key as keyof T])))) {
			return Object.values(enumObj).filter(val => typeof val === 'number') as Array<T[keyof T]>;
		}
		// For string enums, return all values
		return Object.values(enumObj) as Array<T[keyof T]>;
	}

	// Check if string is valid enum key
	static isValidKey<T extends Record<string | number, string | number>>(
		enumObj: T,
		str: string
	): str is Extract<keyof T, string> {
		return str in enumObj && isNaN(Number(str));
	}

	// Check if value is valid enum value
	static isValidValue<T extends Record<string | number, string | number>>(
		enumObj: T,
		value: any
	): value is T[keyof T] {
		return Object.values(enumObj).includes(value);
	}
}
