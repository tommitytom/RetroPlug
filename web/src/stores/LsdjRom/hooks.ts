import { useContext } from "react";
import type { LsdjStoreState } from "./store";
import { LsdjStoreContext } from "./context";

export const useLsdjStore = <T,>(selector: (state: LsdjStoreState) => T): T => {
	const store = useContext(LsdjStoreContext);
	if (!store) {
		throw new Error('useLsdjStore must be used within LsdjStoreProvider');
	}
	return store(selector);
};
