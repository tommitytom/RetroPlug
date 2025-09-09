import { createContext } from "react";
import { type LsdjStore } from "../stores/LsdjStore";

export const LsdjStoreContext = createContext<LsdjStore | null>(null);
