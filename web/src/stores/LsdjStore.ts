import type { WritableDraft } from 'immer';
import { create } from 'zustand';
import { devtools, subscribeWithSelector } from 'zustand/middleware';
import { immer } from 'zustand/middleware/immer';

import type { IEffect } from '../effects/Effect';
import type { ILsdjEditableKit, ILsdjEmptyKit, ILsdjKit, ILsdjKitBase, ILsdjKitSample, ILsdjRom, INamedKit } from '../types/LsdjTypes';
import { type SystemId, toUint8Array } from '../utils/NativeUtil';
import { replaceObject } from '../utils/ObjectUtils';
import { LsdjController } from '../wrapper/Lsdj';

export interface LsdjStoreState {
	// State
	controller: LsdjController;
	systemId: SystemId;
	rom: ILsdjRom | null;
	kit: ILsdjKit | null; // For standalone kit editing

	// Core System Actions
	updateController: (controller: LsdjController, systemId: SystemId) => void;
	updateSystem: (controller: LsdjController, systemId: SystemId, initialRom?: ILsdjRom, initialKit?: ILsdjKit) => void;

	// ROM Actions
	loadRom: (rom: ILsdjRom) => void;
	clearRom: () => void;

	// Kit Actions (works for both ROM kits and standalone)
	loadStandaloneKit: (kit: ILsdjKit) => void;
	addKit: (kit: ILsdjKit) => void;
	removeKit: (kitKey: string) => void;
	updateKit: (kitKey: string, updates: ILsdjKitBase) => void;
	renameKit: (kitKey: string, name: string, triggerUpdate?: boolean) => void;
	fetchKitData: (kitKey: string) => void;
	//patchSystemKit: (kitKey: string) => void;

	// Sample Actions
	addSamples: (kitKey: string, sample: ILsdjKitSample[]) => void;
	removeSample: (kitKey: string, sampleKey: string) => void;
	updateSample: (kitKey: string, sampleKey: string, updates: Partial<ILsdjKitSample>) => void;
	renameSample: (kitKey: string, sampleKey: string, name: string) => void;
	reorderSamples: (kitKey: string, fromIndex: number, toIndex: number) => void;

	// Effect Actions for Kits
	addKitEffect: (kitKey: string, effect: IEffect) => void;
	updateKitEffect: (kitKey: string, effectKey: string, updates: Partial<IEffect>) => void;
	removeKitEffect: (kitKey: string, effectKey: string) => void;
	reorderKitEffects: (kitKey: string, fromIndex: number, toIndex: number) => void;

	// Effect Actions for Samples
	addSampleEffect: (kitKey: string, sampleKey: string, effect: IEffect) => void;
	updateSampleEffect: (kitKey: string, sampleKey: string, effectKey: string, updates: Partial<IEffect>) => void;
	removeSampleEffect: (kitKey: string, sampleKey: string, effectKey: string) => void;
	reorderSampleEffects: (kitKey: string, sampleKey: string, fromIndex: number, toIndex: number) => void;

	// Utility
	getRom: () => ILsdjRom;
	getKits: () => ILsdjKit[] | undefined;
	getKit: (kitKey: string) => ILsdjKit | undefined;
	getSample: (kitKey: string, sampleKey: string) => ILsdjKitSample | undefined;
}

function getKit(state: WritableDraft<LsdjStoreState>, kitKey: string) {
	if (state.rom) {
		return state.rom.kits.find((k) => k.key === kitKey);
	} else if (state.kit && state.kit.key === kitKey) {
		return state.kit;
	}
	return undefined;
}

function getEditableKit(state: WritableDraft<LsdjStoreState>, kitKey: string) {
	const kit = getKit(state, kitKey);
	if (kit && kit.kit.type === 'editable') {
		return kit as ILsdjKit<ILsdjEditableKit>;
	}
	return undefined;
}

function getSample(state: WritableDraft<LsdjStoreState>, kitKey: string, sampleKey: string) {
	const kitContainer = getEditableKit(state, kitKey);
	if (kitContainer) {
		return kitContainer.kit.samples.find((s) => s.key === sampleKey);
	}
	return undefined;
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

					// Core System Actions
					updateController: (controller, systemId) =>
						set((state) => {
							state.controller = controller;
							state.systemId = systemId;
						}),

					updateSystem: (controller, systemId, initialRom, initialKit) =>
						set((state) => {
							state.controller = controller;
							state.systemId = systemId;
							state.rom = initialRom || null;
							state.kit = initialKit || null;
						}),

					// ROM Actions
					loadRom: (rom) =>
						set((state) => {
							state.rom = rom;
							state.kit = null; // Clear standalone kit when loading ROM
						}),

					clearRom: () =>
						set((state) => {
							state.rom = null;
						}),

					// Kit Actions
					loadStandaloneKit: (kit) =>
						set((state) => {
							state.kit = kit;
							state.rom = null; // Clear ROM when loading standalone kit
						}),

					addKit: (kit) =>
						set((state) => {
							if (state.rom) {
								state.rom.kits.push(kit);
							}
						}),

					removeKit: (kitKey) =>
						set((state) => {
							const kitContainer = getKit(state, kitKey);
							if (!kitContainer) {
								console.error("Kit not found for update:", kitKey);
								return;
							}

							state.controller.removeKit(state.systemId, kitContainer.id);
							const kitres = state.controller.getKit(state.systemId, kitContainer.id);
							if (kitres) {
								kitContainer.kit = kitres.kit;
							}
						}),

					updateKit: (kitKey, kit) =>
						set((state) => {
							const kitContainer = getKit(state, kitKey);
							if (!kitContainer) {
								console.error("Kit not found for update:", kitKey);
								return;
							}

							kitContainer.kit = kit;
							state.controller.updateKit(state.systemId, kitContainer);
						}),

					renameKit: (kitKey, name, triggerUpdate) =>
						set((state) => {
							const kitContainer = getKit(state, kitKey);
							if (kitContainer && kitContainer.kit.type !== 'empty') {
								(kitContainer.kit as INamedKit).name = name;
								if (triggerUpdate) {
									state.controller.updateKit(state.systemId, kitContainer);
								}
							}
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
							const kitContainer = getEditableKit(state, kitKey);
							if (!kitContainer) {
								console.error("Cannot add samples to non-editable kit");
								return;
							}

							kitContainer.kit.samples.push(...samples);
							state.controller.updateKit(state.systemId, kitContainer);
						}),

					removeSample: (kitKey, sampleKey) =>
						set((state) => {
							const kitContainer = getEditableKit(state, kitKey);
							if (!kitContainer) {
								console.error("Cannot add samples to non-editable kit");
								return;
							}

							kitContainer.kit.samples = kitContainer.kit.samples.filter((s) => s.key !== sampleKey);
							state.controller.updateKit(state.systemId, kitContainer);
						}),

					updateSample: (kitKey, sampleKey, updates) =>
						set((state) => {
							const kitContainer = getEditableKit(state, kitKey);
							if (!kitContainer) {
								console.error("Cannot modify samples on a non-editable kit");
								return;
							}

							const sample = kitContainer.kit.samples.find((s) => s.key === sampleKey);
							if (sample) {
								replaceObject(sample, updates);
								state.controller.updateKit(state.systemId, kitContainer);
							}
						}),

					renameSample: (kitKey, sampleKey, name) =>
						set((state) => {
							const kitContainer = getEditableKit(state, kitKey);
							const sample = kitContainer?.kit.samples.find((s) => s.key === sampleKey);
							if (kitContainer && sample) {
								sample.name = name;
								state.controller.updateKit(state.systemId, kitContainer);
							}
						}),

					reorderSamples: (kitKey, fromIndex, toIndex) =>
						set((state) => {
							const kitContainer = getEditableKit(state, kitKey);
							if (!kitContainer) {
								console.error("Cannot reorder samples on a non-editable kit");
								return;
							}

							const samples = kitContainer.kit.samples;
							if (fromIndex < 0 || fromIndex >= samples.length || toIndex < 0 || toIndex >= samples.length) {
								console.error("Invalid indices for sample reordering");
								return;
							}

							// Reorder the samples array
							const [movedSample] = samples.splice(fromIndex, 1);
							samples.splice(toIndex, 0, movedSample);

							state.controller.updateKit(state.systemId, kitContainer);
						}),

					// Kit Effect Actions
					addKitEffect: (kitKey, effect) =>
						set((state) => {
							const kitContainer = getEditableKit(state, kitKey);
							const kit = kitContainer?.kit;
							if (kit && kit.effects) {
								const ditherIdx = kit.effects.findIndex((e) => e.type === 'DitherEffect');
								if (ditherIdx !== -1) {
									kit.effects.splice(ditherIdx, 0, effect); // Insert before DitherEffect
								} else {
									kit.effects.push(effect);
								}

								state.controller.updateKit(state.systemId, kitContainer);
							}
						}),

					updateKitEffect: (kitKey, effectKey, updates) =>
						set((state) => {
							const kitContainer = getEditableKit(state, kitKey);
							const kit = kitContainer?.kit;
							if (kit) {
								const effect = kit.effects.find((e) => e.key === effectKey);
								if (effect) {
									Object.assign(effect, updates);
									state.controller.updateKit(state.systemId, kitContainer);
								}
							}
						}),

					removeKitEffect: (kitKey, effectKey) =>
						set((state) => {
							const kitContainer = getEditableKit(state, kitKey);
							const kit = kitContainer?.kit;
							if (kit && kit.effects) {
								kit.effects = kit.effects.filter((e) => e.key !== effectKey);
								state.controller.updateKit(state.systemId, kitContainer);
							}
						}),

					reorderKitEffects: (kitKey, fromIndex, toIndex) =>
						set((state) => {
							const kitContainer = getEditableKit(state, kitKey);
							const kit = kitContainer?.kit;
							if (kit && kit.effects && kit.effects.length > fromIndex && kit.effects.length > toIndex) {
								const [removed] = kit.effects.splice(fromIndex, 1);
								kit.effects.splice(toIndex, 0, removed);
								state.controller.updateKit(state.systemId, kitContainer);
							}
						}),

					// Sample Effect Actions
					addSampleEffect: (kitKey, sampleKey, effect) =>
						set((state) => {
							const kitContainer = getEditableKit(state, kitKey);
							const kit = kitContainer?.kit;
							if (kit) {
								const sample = kit.samples.find((s) => s.key === sampleKey);
								if (sample && sample.effects) {
									sample.effects.push(effect);
									state.controller.updateKit(state.systemId, kitContainer);
								}
							}
						}),

					updateSampleEffect: (kitKey, sampleKey, effectKey, updates) =>
						set((state) => {
							const kitContainer = getEditableKit(state, kitKey);
							const kit = kitContainer?.kit;
							if (kit) {
								const sample = kit.samples.find((s) => s.key === sampleKey);
								if (sample) {
									const effect = sample.effects.find((e) => e.key === effectKey);
									if (effect) {
										Object.assign(effect, updates);
										state.controller.updateKit(state.systemId, kitContainer);
									}
								}
							}
						}),

					removeSampleEffect: (kitKey, sampleKey, effectKey) =>
						set((state) => {
							const kitContainer = getEditableKit(state, kitKey);
							const kit = kitContainer?.kit;
							if (kit) {
								const sample = kit.samples.find((s) => s.key === sampleKey);
								if (sample) {
									sample.effects = sample.effects.filter((e) => e.key !== effectKey);
									state.controller.updateKit(state.systemId, kitContainer);
								}
							}
						}),

					reorderSampleEffects: (kitKey, sampleKey, fromIndex, toIndex) =>
						set((state) => {
							const kitContainer = getEditableKit(state, kitKey);
							const kit = kitContainer?.kit;
							if (kit) {
								const sample = kit.samples.find((s) => s.key === sampleKey);
								if (
									sample &&
									sample.effects &&
									sample.effects.length > fromIndex &&
									sample.effects.length > toIndex
								) {
									const [removed] = sample.effects.splice(fromIndex, 1);
									sample.effects?.splice(toIndex, 0, removed);
									state.controller.updateKit(state.systemId, kitContainer);
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
						return getKit(state, kitKey);
					},

					getSample: (kitKey, sampleKey) => {
						const state = get();
						const kitContainer = getEditableKit(state, kitKey);
						if (kitContainer) {
							return kitContainer.kit.samples.find((s) => s.key === sampleKey);
						}
					},
				})),
			),
		),
	);

export type LsdjStore = ReturnType<typeof createLsdjStore>;
