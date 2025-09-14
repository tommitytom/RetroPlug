import { ReactNode, useEffect, useState } from 'react';

import { createLsdjStore, type LsdjStore } from '../stores/LsdjStore';
import type { ILsdjKit, ILsdjRom } from '../types/LsdjTypes';
import { LsdjStoreContext } from './LsdjStoreContext';
import { LsdjController } from '../wrapper/Lsdj';
import type { SystemId } from '../utils/NativeUtil';

export interface LsdjStoreProviderProps {
	children: ReactNode;
	lsdj: LsdjController;
	system: SystemId;
	initialRom?: ILsdjRom;
	initialKit?: ILsdjKit;
}

export const LsdjStoreProvider: React.FC<LsdjStoreProviderProps> = ({ children, lsdj, system, initialRom, initialKit }) => {
	const [store] = useState<LsdjStore>(() => createLsdjStore(lsdj, system, initialRom, initialKit));

	// Update the store when key dependencies change
	useEffect(() => {
		store.getState().updateSystem(lsdj, system, initialRom, initialKit);
	}, [store, lsdj, system, initialRom, initialKit]);

	return <LsdjStoreContext.Provider value={store}>{children}</LsdjStoreContext.Provider>;
};
