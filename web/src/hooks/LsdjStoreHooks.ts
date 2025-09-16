import { useContext, useEffect, useRef, useState } from "react";
import type { LsdjStoreState } from "../stores/LsdjStore";
import { LsdjStoreContext } from "../contexts/LsdjStoreContext";
import type { ILsdjKit, ILsdjPatchedKit, INamedKit } from "../types/LsdjTypes";

export const useLsdjStore = <T,>(selector: (state: LsdjStoreState) => T): T => {
	const store = useContext(LsdjStoreContext);
	if (!store) {
		throw new Error('useLsdjStore must be used within LsdjStoreProvider');
	}
	return store(selector);
};

// Optimized hook for kit list that only re-renders when kit structure changes, not internal content
export const useKitList = () => {
	const store = useContext(LsdjStoreContext);
	if (!store) {
		throw new Error('useKitList must be used within LsdjStoreProvider');
	}

	const [kits, setKits] = useState<ILsdjKit[]>(() => store.getState().rom?.kits || []);

	useEffect(() => {
		const unsubscribe = store.subscribe(
			(state) => state.rom?.kits || [],
			(newKits) => {
				console.log('kits changed');

				setKits(newKits);
			},
			{
				equalityFn: (a, b) => {
					// Only re-render if the number of kits changes or kit identity/name changes
					if (a.length !== b.length) return false;
					return a.every((kitA, index) => {
						const kitB = b[index];
						return kitA?.key === kitB?.key &&
							   kitA?.id === kitB?.id;
					});
				}
			}
		);

		return unsubscribe;
	}, [store]);

	return kits;
};

// Optimized hook for individual kit that only re-renders when basic kit properties change, not effects/samples
export const useKit = (kitKey: string) => {
	const store = useContext(LsdjStoreContext);
	if (!store) {
		throw new Error('useKit must be used within LsdjStoreProvider');
	}

	const [kit, setKit] = useState<ILsdjKit | undefined>(() => {
		const state = store.getState();
		return state.rom?.kits.find(k => k.key === kitKey) ||
			   (state.kit?.key === kitKey ? state.kit : undefined);
	});

	useEffect(() => {
		const unsubscribe = store.subscribe(
			(state) => {
				return state.rom?.kits.find(k => k.key === kitKey) ||
					   (state.kit?.key === kitKey ? state.kit : undefined);
			},
			(newKit) => {
				setKit(newKit);
			},
			{
				equalityFn: (a, b) => {
					// Only re-render if basic kit properties change, not effects or samples
					if (!a && !b) return true;
					if (!a || !b) return false;
					return a.key === b.key &&
						   a.id === b.id &&
						   a.kit.type === b.kit.type &&
						   (a.kit as INamedKit).name === (b.kit as INamedKit).name &&
						   (a.kit as ILsdjPatchedKit).path === (b.kit as ILsdjPatchedKit).path &&
						   a.data === b.data;
				}
			}
		);

		return unsubscribe;
	}, [store, kitKey]);

	return kit;
};

// Hook to subscribe to kit list changes
export const useKitListChanges = (callback: (kitId: string, kit: ILsdjKit|null) => void) => {
	const store = useContext(LsdjStoreContext);
	const callbackRef = useRef(callback);
	callbackRef.current = callback;

	const previousKitsRef = useRef<Set<string>>(null);

	useEffect(() => {
		if (!store) return;

		if (!previousKitsRef.current) {
			const currentState = store.getState();
			previousKitsRef.current = new Set(currentState.rom?.kits.map(kit => kit.key!));
		}

		const unsubscribe = store.subscribe(
			(state) => state.rom?.kits || [],
			(kits) => {
				const prev = previousKitsRef.current!;
				const added = kits.filter(kit => !prev.has(kit.key!));
				const removed = Array.from(prev).filter(key => !kits.some(kit => kit.key === key));

				added.forEach(kit => callbackRef.current(kit.key!, kit));
				removed.forEach(key => callbackRef.current(key, null));

				previousKitsRef.current = new Set(kits.map(kit => kit.key!));
			},
			{
				equalityFn: (a, b) => a.length === b.length && a.every((kit, index) => kit.key === b[index]?.key),
			}
		);

		return unsubscribe;
	}, [store]);
};

// Hook to subscribe to specific kit changes
export const useKitChanges = (kitKeys: string[], callback: (kitKey: string, kit?: ILsdjKit) => void) => {
	const store = useContext(LsdjStoreContext);
	const callbackRef = useRef(callback);
	callbackRef.current = callback;

	useEffect(() => {
		if (!store || kitKeys.length === 0) return;

		const unsubscribes = kitKeys.map(kitKey => {
			return store.subscribe(
				(state) => {
					return state.rom!.kits.find(kit => kit.key === kitKey);
				},
				(kit) => callbackRef.current(kitKey, kit),
				{
					equalityFn: (a, b) => JSON.stringify(a) === JSON.stringify(b),
				}
			);
		});

		return () => {
			unsubscribes.forEach(unsubscribe => unsubscribe());
		};
	}, [store, kitKeys]);
};
