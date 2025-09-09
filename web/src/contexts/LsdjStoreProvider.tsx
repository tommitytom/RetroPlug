import { ReactNode, useRef } from 'react';

import { createLsdjStore, type LsdjStore } from '../stores/LsdjStore';
import type { ILsdjKit, ILsdjRom } from '../types/LsdjTypes';
import { LsdjStoreContext } from './LsdjStoreContext';

export interface LsdjStoreProviderProps {
	children: ReactNode;
	initialRom?: ILsdjRom;
	initialKit?: ILsdjKit;
}

export const LsdjStoreProvider: React.FC<LsdjStoreProviderProps> = ({ children, initialRom, initialKit }) => {
	const storeRef = useRef<LsdjStore | null>(null);

	if (!storeRef.current) {
		storeRef.current = createLsdjStore(initialRom, initialKit);
	}

	return <LsdjStoreContext.Provider value={storeRef.current}>{children}</LsdjStoreContext.Provider>;
};
