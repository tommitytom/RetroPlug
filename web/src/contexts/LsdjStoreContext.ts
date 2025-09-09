import { createContext } from "react";
import { createLsdjStore } from "../stores/LsdjStore";

type LsdjStore = ReturnType<typeof createLsdjStore>;
export const LsdjStoreContext = createContext<LsdjStore | null>(null);
