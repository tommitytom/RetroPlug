export function deepEqual<T>(a: T, b: T): boolean {
	if (a === b) return true;

	if (a == null || b == null) return false;

	if (Array.isArray(a) && Array.isArray(b)) {
		if (a.length !== b.length) return false;
		for (let i = 0; i < a.length; i++) {
			if (!deepEqual(a[i], b[i])) return false;
		}
		return true;
	}

	if (Array.isArray(a) || Array.isArray(b)) return false;

	if (typeof a === 'object' && typeof b === 'object') {
		const keysA = Object.keys(a);
		const keysB = Object.keys(b);

		if (keysA.length !== keysB.length) return false;

		for (const key of keysA) {
			if (!keysB.includes(key)) return false;
			if (!deepEqual((a as any)[key], (b as any)[key])) return false;
		}
		return true;
	}

	return false;
}
