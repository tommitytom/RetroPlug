import { create } from 'zustand';
import { devtools, subscribeWithSelector } from 'zustand/middleware';
import { immer } from 'zustand/middleware/immer';

import type { ILsdjKit, ILsdjKitEffect, ILsdjKitSample, ILsdjRom } from '../types/LsdjTypes';
import { LsdjController } from '../wrapper/Lsdj';
import { type SystemId, toUint8Array } from '../utils/NativeUtil';
import { Timer } from '../utils/Timer';

export interface LsdjStoreState {
	// State
	controller: LsdjController;
	systemId: SystemId;
	rom: ILsdjRom | null;
	kit: ILsdjKit | null; // For standalone kit editing
	selectedKitKey: string | null;
	selectedSampleKey: string | null;

	// ROM Actions
	loadRom: (rom: ILsdjRom) => void;
	clearRom: () => void;

	// Kit Actions (works for both ROM kits and standalone)
	loadStandaloneKit: (kit: ILsdjKit) => void;
	addKit: (kit: ILsdjKit) => void;
	removeKit: (kitKey: string) => void;
	updateKit: (kitKey: string, updates: Partial<ILsdjKit>) => void;
	renameKit: (kitKey: string, name: string, triggerUpdate?: boolean) => void;
	selectKit: (kitKey: string | null) => void;
	fetchKitData: (kitKey: string) => void;
	//patchSystemKit: (kitKey: string) => void;

	// Sample Actions
	addSamples: (kitKey: string, sample: ILsdjKitSample[]) => void;
	removeSample: (kitKey: string, sampleKey: string) => void;
	updateSample: (kitKey: string, sampleKey: string, updates: Partial<ILsdjKitSample>) => void;
	renameSample: (kitKey: string, sampleKey: string, name: string) => void;
	selectSample: (sampleKey: string | null) => void;

	// Effect Actions for Kits
	addKitEffect: (kitKey: string, effect: ILsdjKitEffect) => void;
	updateKitEffect: (kitKey: string, effectKey: string, updates: Partial<ILsdjKitEffect>) => void;
	removeKitEffect: (kitKey: string, effectKey: string) => void;
	reorderKitEffects: (kitKey: string, fromIndex: number, toIndex: number) => void;

	// Effect Actions for Samples
	addSampleEffect: (kitKey: string, sampleKey: string, effect: ILsdjKitEffect) => void;
	updateSampleEffect: (kitKey: string, sampleKey: string, effectKey: string, updates: Partial<ILsdjKitEffect>) => void;
	removeSampleEffect: (kitKey: string, sampleKey: string, effectKey: string) => void;
	reorderSampleEffects: (kitKey: string, sampleKey: string, fromIndex: number, toIndex: number) => void;

	// Utility
	getRom: () => ILsdjRom;
	getKits: () => ILsdjKit[] | undefined;
	getKit: (kitKey: string) => ILsdjKit | undefined;
	getSample: (kitKey: string, sampleKey: string) => ILsdjKitSample | undefined;
}

// ============= Store Creator =============
export const createLsdjStore = (
	controller: LsdjController,
	systemId: SystemId,
	initialRom?: ILsdjRom,
	initialKit?: ILsdjKit,
) =>
	create<LsdjStoreState>()(
		devtools(
			subscribeWithSelector(
				immer((set, get) => ({
					// Initial State
					controller,
					systemId: systemId,
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

					addKit: (kit) =>
						set((state) => {
							if (state.rom) {
								state.rom.kits.push(kit);
							}
						}),

					removeKit: (kitKey) =>
						set((state) => {
							if (state.rom) {
								state.rom.kits = state.rom.kits.filter((k) => k.key !== kitKey);
								// Clear selection if the removed kit was selected
								if (state.selectedKitKey === kitKey) {
									state.selectedKitKey = null;
									state.selectedSampleKey = null;
								}
							}
						}),

					updateKit: (kitKey, updates) =>
						set((state) => {
							if (state.rom) {
								const kit = state.rom.kits.find((k) => k.key === kitKey);
								if (kit) {
									Object.assign(kit, updates);
									state.controller.updateKit(state.systemId, kit.id, kit);
								}
							} else if (state.kit && state.kit.key === kitKey) {
								Object.assign(state.kit, updates);
								state.controller.updateKit(state.systemId, state.kit.id, state.kit);
							}
						}),

					renameKit: (kitKey, name, triggerUpdate) =>
						set((state) => {
							if (state.rom) {
								const kit = state.rom.kits.find((k) => k.key === kitKey);
								if (kit) {
									kit.name = name;
									if (triggerUpdate) {
										state.controller.updateKit(state.systemId, kit.id, kit);
									}
								}
							} else if (state.kit && state.kit.key === kitKey) {
								state.kit.name = name;
								if (triggerUpdate) {
									state.controller.updateKit(state.systemId, state.kit.id, state.kit);
								}
							}
						}),

					selectKit: (kitKey) =>
						set((state) => {
							state.selectedKitKey = kitKey;
						}),

					fetchKitData: (kitKey) =>
						set((state) => {
							const lsdj = state.controller;
							const kit = state.rom?.kits.find((k) => k.key === kitKey);
							if (kit && state.systemId !== null) {
								const kitData = lsdj.getKitData(state.systemId, kit.id)!;
								kit.data = toUint8Array(kitData);
							}
						}),

					// Sample Actions
					addSamples: (kitKey, samples) =>
						set((state) => {
							const kit =
								state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
							if (kit) {
								if (!kit.samples) kit.samples = [];
								kit.samples.push(...samples);
								state.controller.updateKit(state.systemId, kit.id, kit);
							}
						}),

					removeSample: (kitKey, sampleKey) =>
						set((state) => {
							const kit =
								state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
							if (kit && kit.samples) {
								kit.samples = kit.samples.filter((s) => s.key !== sampleKey);
								// Clear selection if the removed sample was selected
								if (state.selectedSampleKey === sampleKey) {
									state.selectedSampleKey = null;
								}

								state.controller.updateKit(state.systemId, kit.id, kit);
							}
						}),

					updateSample: (kitKey, sampleKey, updates) =>
						set((state) => {
							const kit =
								state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
							if (kit) {
								const sample = kit.samples?.find((s) => s.key === sampleKey);
								if (sample) {
									Object.assign(sample, updates);
								}
								state.controller.updateKit(state.systemId, kit.id, kit);
							}
						}),

					renameSample: (kitKey, sampleKey, name) =>
						set((state) => {
							const kit =
								state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
							if (kit) {
								const sample = kit.samples?.find((s) => s.key === sampleKey);
								if (sample) sample.name = name;
								state.controller.updateKit(state.systemId, kit.id, kit);
							}
						}),

					selectSample: (sampleKey) =>
						set((state) => {
							state.selectedSampleKey = sampleKey;
						}),

					/*
					patchSystemKit: (kitKey: string) =>
						set((state) => {
							const lsdj = state.controller;
							const kit = state.rom?.kits.find((k) => k.key === kitKey);

							if (kit && state.systemId !== null) {
								lsdj.updateKit(state.systemId, kit.id, kit);

								// Capture values before the delay
								const capturedSystemId = state.systemId;
								const capturedKitId = kit.id;

								setTimeout(() => {
									// Use captured values instead of state
									const kitData = lsdj.getKitData(capturedSystemId, capturedKitId)!;
									if (kitData && kitData.size() > 0) {
										// Update the store with fresh state
										set((currentState) => {
											const currentKit = currentState.rom?.kits.find((k) => k.key === kitKey);
											if (currentKit) {
												console.log('setting kit data');

												currentKit.data = toUint8Array(kitData);
											}
										});
									}
								}, 1000); // Your desired delay
							}
						}),
*/
					// Kit Effect Actions
					addKitEffect: (kitKey, effect) =>
						set((state) => {
							const kit =
								state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
							if (kit && kit.effects) {
								const ditherIdx = kit.effects.findIndex((e) => e.effect.type === 'DitherEffect');
								if (ditherIdx !== -1) {
									kit.effects.splice(ditherIdx, 0, effect); // Insert before DitherEffect
								} else {
									kit.effects.push(effect);
								}

								state.controller.updateKit(state.systemId, kit.id, kit);
							}
						}),

					updateKitEffect: (kitKey, effectKey, updates) =>
						set((state) => {
							const kit =
								state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
							if (kit) {
								const effect = kit.effects?.find((e) => e.key === effectKey);
								if (effect) {
									Object.assign(effect.effect, updates);
									state.controller.updateKit(state.systemId, kit.id, kit);
								}
							}
						}),

					removeKitEffect: (kitKey, effectKey) =>
						set((state) => {
							const kit =
								state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
							if (kit && kit.effects) {
								kit.effects = kit.effects.filter((e) => e.key !== effectKey);
								state.controller.updateKit(state.systemId, kit.id, kit);
							}
						}),

					reorderKitEffects: (kitKey, fromIndex, toIndex) =>
						set((state) => {
							const kit =
								state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
							if (kit && kit.effects && kit.effects.length > fromIndex && kit.effects.length > toIndex) {
								const [removed] = kit.effects.splice(fromIndex, 1);
								kit.effects.splice(toIndex, 0, removed);
								state.controller.updateKit(state.systemId, kit.id, kit);
							}
						}),

					// Sample Effect Actions
					addSampleEffect: (kitKey, sampleKey, effect) =>
						set((state) => {
							const kit =
								state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
							if (kit) {
								const sample = kit.samples?.find((s) => s.key === sampleKey);
								if (sample && sample.effects) {
									sample.effects.push(effect);
									state.controller.updateKit(state.systemId, kit.id, kit);
								}
							}
						}),

					updateSampleEffect: (kitKey, sampleKey, effectKey, updates) =>
						set((state) => {
							const kit =
								state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
							if (kit) {
								const sample = kit.samples?.find((s) => s.key === sampleKey);
								if (sample) {
									const effect = sample.effects?.find((e) => e.key === effectKey);
									if (effect) {
										Object.assign(effect, updates);
										state.controller.updateKit(state.systemId, kit.id, kit);
									}
								}
							}
						}),

					removeSampleEffect: (kitKey, sampleKey, effectKey) =>
						set((state) => {
							const kit =
								state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
							if (kit) {
								const sample = kit.samples?.find((s) => s.key === sampleKey);
								if (sample) {
									sample.effects = sample.effects?.filter((e) => e.key !== effectKey);
									state.controller.updateKit(state.systemId, kit.id, kit);
								}
							}
						}),

					reorderSampleEffects: (kitKey, sampleKey, fromIndex, toIndex) =>
						set((state) => {
							const kit =
								state.rom?.kits.find((k) => k.key === kitKey) || (state.kit?.key === kitKey ? state.kit : null);
							if (kit) {
								const sample = kit.samples?.find((s) => s.key === sampleKey);
								if (
									sample &&
									sample.effects &&
									sample.effects?.length > fromIndex &&
									sample.effects?.length > toIndex
								) {
									const [removed] = sample.effects?.splice(fromIndex, 1);
									sample.effects?.splice(toIndex, 0, removed);
									state.controller.updateKit(state.systemId, kit.id, kit);
								}
							}
						}),

					// Utility
					getRom: () => {
						const state = get();
						return state.rom!;
					},

					getKits: () => {
						const state = get();
						return state.rom?.kits;
					},

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
		),
	);

export type LsdjStore = ReturnType<typeof createLsdjStore>;
