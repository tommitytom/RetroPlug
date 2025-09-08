import { createContext } from "react";
import { createLsdjStore } from "./store";

type LsdjStore = ReturnType<typeof createLsdjStore>;
export const LsdjStoreContext = createContext<LsdjStore | null>(null);
