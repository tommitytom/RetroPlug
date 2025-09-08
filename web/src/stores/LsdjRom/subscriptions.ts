import type { LsdjStore } from './store';

/**
 * Example subscriptions using subscribeWithSelector
 * These demonstrate how to respond to changes in specific parts of the store
 */

// Subscribe to ROM changes
export const subscribeToRomChanges = (store: LsdjStore, callback: (rom: any) => void) => {
	return store.subscribe(
		(state) => state.rom,
		callback,
		{
			// Only fire when the rom actually changes (shallow comparison)
			equalityFn: (a, b) => a === b,
		}
	);
};

// Subscribe to selected kit changes
export const subscribeToSelectedKitChanges = (store: LsdjStore, callback: (kitKey: string | null) => void) => {
	return store.subscribe(
		(state) => state.selectedKitKey,
		callback
	);
};

// Subscribe to selected sample changes
export const subscribeToSelectedSampleChanges = (store: LsdjStore, callback: (sampleKey: string | null) => void) => {
	return store.subscribe(
		(state) => state.selectedSampleKey,
		callback
	);
};

// Subscribe to specific kit changes (by key)
export const subscribeToKitChanges = (store: LsdjStore, kitKey: string, callback: (kit: any) => void) => {
	return store.subscribe(
		(state) => {
			if (state.rom) {
				return state.rom.kits.find(k => k.key === kitKey);
			} else if (state.kit && state.kit.key === kitKey) {
				return state.kit;
			}
			return null;
		},
		callback,
		{
			// Deep comparison for objects
			equalityFn: (a, b) => JSON.stringify(a) === JSON.stringify(b),
		}
	);
};

// Subscribe to kit list changes (when kits are added/removed)
export const subscribeToKitListChanges = (store: LsdjStore, callback: (kits: any[]) => void) => {
	return store.subscribe(
		(state) => state.rom?.kits || [],
		callback,
		{
			// Check if the array length or contents changed
			equalityFn: (a, b) => a.length === b.length && a.every((kit, index) => kit.key === b[index]?.key),
		}
	);
};

// Subscribe to sample list changes within a specific kit
export const subscribeToSampleListChanges = (store: LsdjStore, kitKey: string, callback: (samples: any[]) => void) => {
	return store.subscribe(
		(state) => {
			const kit = state.rom?.kits.find(k => k.key === kitKey) ||
						(state.kit?.key === kitKey ? state.kit : null);
			return kit?.samples || [];
		},
		callback,
		{
			// Check if the sample array changed
			equalityFn: (a, b) => a.length === b.length && a.every((sample, index) => sample.key === b[index]?.key),
		}
	);
};

// Subscribe to any state changes (useful for debugging)
export const subscribeToAllChanges = (store: LsdjStore, callback: (state: any) => void) => {
	return store.subscribe(
		(state) => state,
		callback
	);
};

/**
 * Example usage:
 *
 * const store = createLsdjStore();
 *
 * // Subscribe to ROM changes
 * const unsubscribeRom = subscribeToRomChanges(store, (rom) => {
 *   console.log('ROM changed:', rom);
 * });
 *
 * // Subscribe to selected kit changes
 * const unsubscribeKit = subscribeToSelectedKitChanges(store, (kitKey) => {
 *   console.log('Selected kit changed:', kitKey);
 * });
 *
 * // Don't forget to unsubscribe when done
 * // unsubscribeRom();
 * // unsubscribeKit();
 */
