import { ReactNode, useRef } from "react";
import type { ILsdjKit, ILsdjRom } from "../../types/LsdjTypes";
import { createLsdjStore, type LsdjStoreState, type LsdjStore } from "./store";
import { LsdjStoreContext } from "./context";

export interface LsdjStoreProviderProps {
	children: ReactNode;
	initialRom?: ILsdjRom;
	initialKit?: ILsdjKit;
	onChange(state: LsdjStoreState, prevState: LsdjStoreState): void;
}

export const LsdjStoreProvider: React.FC<LsdjStoreProviderProps> = ({
	children,
	initialRom,
	initialKit,
}) => {
	const storeRef = useRef<LsdjStore|null>(null);

	if (!storeRef.current) {
		storeRef.current = createLsdjStore(initialRom, initialKit);
	}

	return (
		<LsdjStoreContext.Provider value={storeRef.current}>
			{children}
		</LsdjStoreContext.Provider>
	);
};
