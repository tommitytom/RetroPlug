// A package's plain source naming the React kit. The kit is a workspace package, so its declaration alone admits the
// import; React belongs to the package's ts/ui/ tree, and a file outside it reaches a view through the contract.
import { Box } from "@retroplug/ui-react";

export const box = Box;
