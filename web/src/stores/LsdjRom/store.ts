import { create } from 'zustand';
import { devtools } from 'zustand/middleware';
import { immer } from 'zustand/middleware/immer';

import type { ILsdjKit, ILsdjKitSample, ILsdjRom, LsdjEffect } from "../../types/LsdjTypes";

export interface LsdjStoreState {
	// State
	rom: ILsdjRom | null;
	kit: ILsdjKit | null; // For standalone kit editing
	selectedKitKey: string | null;
	selectedSampleKey: string | null;

	// ROM Actions
	loadRom: (rom: ILsdjRom) => void;
	clearRom: () => void;

	// Kit Actions (works for both ROM kits and standalone)
	loadStandaloneKit: (kit: ILsdjKit) => void;
	updateKit: (kitKey: string, updates: Partial<ILsdjKit>) => void;
	renameKit: (kitKey: string, name: string) => void;
	selectKit: (kitKey: string | null) => void;

	// Sample Actions
	updateSample: (kitKey: string, sampleKey: string, updates: Partial<ILsdjKitSample>) => void;
	renameSample: (kitKey: string, sampleKey: string, name: string) => void;
	selectSample: (sampleKey: string | null) => void;

	// Effect Actions for Kits
	addKitEffect: (kitKey: string, effect: LsdjEffect) => void;
	updateKitEffect: (kitKey: string, effectKey: string, updates: Partial<LsdjEffect>) => void;
	removeKitEffect: (kitKey: string, effectKey: string) => void;
	reorderKitEffects: (kitKey: string, fromIndex: number, toIndex: number) => void;

	// Effect Actions for Samples
	addSampleEffect: (kitKey: string, sampleKey: string, effect: LsdjEffect) => void;
	updateSampleEffect: (kitKey: string, sampleKey: string, effectKey: string, updates: Partial<LsdjEffect>) => void;
	removeSampleEffect: (kitKey: string, sampleKey: string, effectKey: string) => void;
	reorderSampleEffects: (kitKey: string, sampleKey: string, fromIndex: number, toIndex: number) => void;

	// Utility
	getKit: (kitKey: string) => ILsdjKit | undefined;
	getSample: (kitKey: string, sampleKey: string) => ILsdjKitSample | undefined;
}

// ============= Store Creator =============
export const createLsdjStore = (initialRom?: ILsdjRom, initialKit?: ILsdjKit) =>
	create<LsdjStoreState>()(
		devtools(
			immer((set, get) => ({
				// Initial State
				rom: initialRom || null,
				kit: initialKit || null,
				selectedKitKey: null,
				selectedSampleKey: null,

				// ROM Actions
				loadRom: (rom) =>
					set((state) => {
						state.rom = rom;
						state.kit = null; // Clear standalone kit when loading ROM
					}),

				clearRom: () =>
					set((state) => {
						state.rom = null;
						state.selectedKitKey = null;
						state.selectedSampleKey = null;
					}),

				// Kit Actions
				loadStandaloneKit: (kit) =>
					set((state) => {
						state.kit = kit;
						state.rom = null; // Clear ROM when loading standalone kit
						state.selectedKitKey = kit.key;
					}),

				updateKit: (kitKey, updates) =>
					set((state) => {
						if (state.rom) {
							const kit = state.rom.kits.find((k) => k.key === kitKey);
							if (kit) {
								Object.assign(kit, updates);
							}
						} else if (state.kit && state.kit.key === kitKey) {
							Object.assign(state.kit, updates);
						}
					}),

				renameKit: (kitKey, name) =>
					set((state) => {
						if (state.rom) {
							const kit = state.rom.kits.find((k) => k.key === kitKey);
							if (kit) kit.name = name;
						} else if (state.kit && state.kit.key === kitKey) {
							state.kit.name = name;
						}
					}),

				selectKit: (kitKey) =>
					set((state) => {
						state.selectedKitKey = kitKey;
					}),

				// Sample Actions
				updateSample: (kitKey, sampleKey, updates) =>
					set((state) => {
						const kit = state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
						if (kit) {
							const sample = kit.samples?.find((s) => s.key === sampleKey);
							if (sample) {
								Object.assign(sample, updates);
							}
						}
					}),

				renameSample: (kitKey, sampleKey, name) =>
					set((state) => {
						const kit = state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
						if (kit) {
							const sample = kit.samples?.find((s) => s.key === sampleKey);
							if (sample) sample.name = name;
						}
					}),

				selectSample: (sampleKey) =>
					set((state) => {
						state.selectedSampleKey = sampleKey;
					}),

				// Kit Effect Actions
				addKitEffect: (kitKey, effect) =>
					set((state) => {
						const kit = state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
						if (kit) {
							kit.effects?.push(effect);
						}
					}),

				updateKitEffect: (kitKey, effectKey, updates) =>
					set((state) => {
						const kit = state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
						if (kit) {
							const effect = kit.effects?.find((e) => e.key === effectKey);
							if (effect) {
								Object.assign(effect, updates);
							}
						}
					}),

				removeKitEffect: (kitKey, effectKey) =>
					set((state) => {
						const kit = state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
						if (kit && kit.effects) {
							kit.effects = kit.effects.filter((e) => e.key !== effectKey);
						}
					}),

				reorderKitEffects: (kitKey, fromIndex, toIndex) =>
					set((state) => {
						const kit = state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
						if (kit && kit.effects && kit.effects.length > fromIndex && kit.effects.length > toIndex) {
							const [removed] = kit.effects.splice(fromIndex, 1);
							kit.effects.splice(toIndex, 0, removed);
						}
					}),

				// Sample Effect Actions
				addSampleEffect: (kitKey, sampleKey, effect) =>
					set((state) => {
						const kit = state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
						if (kit) {
							const sample = kit.samples?.find((s) => s.key === sampleKey);
							if (sample && sample.effects) {
								sample.effects?.push(effect);
							}
						}
					}),

				updateSampleEffect: (kitKey, sampleKey, effectKey, updates) =>
					set((state) => {
						const kit = state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
						if (kit) {
							const sample = kit.samples?.find((s) => s.key === sampleKey);
							if (sample) {
								const effect = sample.effects?.find((e) => e.key === effectKey);
								if (effect) {
									Object.assign(effect, updates);
								}
							}
						}
					}),

				removeSampleEffect: (kitKey, sampleKey, effectKey) =>
					set((state) => {
						const kit = state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
						if (kit) {
							const sample = kit.samples?.find((s) => s.key === sampleKey);
							if (sample) {
								sample.effects = sample.effects?.filter((e) => e.key !== effectKey);
							}
						}
					}),

				reorderSampleEffects: (kitKey, sampleKey, fromIndex, toIndex) =>
					set((state) => {
						const kit = state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
						if (kit) {
							const sample = kit.samples?.find((s) => s.key === sampleKey);
							if (sample && sample.effects && sample.effects?.length > fromIndex && sample.effects?.length > toIndex) {
								const [removed] = sample.effects?.splice(fromIndex, 1);
								sample.effects?.splice(toIndex, 0, removed);
							}
						}
					}),

				// Utility
				getKit: (kitKey) => {
					const state = get();
					if (state.rom) {
						return state.rom.kits.find((k) => k.key === kitKey);
					} else if (state.kit && state.kit.key === kitKey) {
						return state.kit;
					}
					return undefined;
				},

				getSample: (kitKey, sampleKey) => {
					const kit = get().getKit(kitKey);
					if (kit?.samples) {
						return kit.samples?.find((s) => s.key === sampleKey);
					}
				},
			})),
		),
	);

export type LsdjStore = ReturnType<typeof createLsdjStore>;
