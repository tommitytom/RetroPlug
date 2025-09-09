import { ReactNode, useRef } from 'react';

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
	const storeRef = useRef<LsdjStore | null>(null);

	if (!storeRef.current) {
		storeRef.current = createLsdjStore(lsdj, system, initialRom, initialKit);
	}

	return <LsdjStoreContext.Provider value={storeRef.current}>{children}</LsdjStoreContext.Provider>;
};
