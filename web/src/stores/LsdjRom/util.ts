import type { IFilterEffect, IGainEffect } from "../../types/LsdjTypes";

export const generateKey = (): string => {
	return `${Date.now()}-${Math.random().toString(36).substring(2, 2 + 9)}`;
};

export const createGainEffect = (gain: number = 1.0): IGainEffect => ({
	id: 0,
	key: generateKey(),
	type: 'gain',
	gain,
});

export const createFilterEffect = (freq: number = 1000, q: number = 1, feedback: number = 0): IFilterEffect => ({
	id: 1,
	key: generateKey(),
	type: 'filter',
	freq,
	q,
	feedback,
});
