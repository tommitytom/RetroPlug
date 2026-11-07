// A package that declares everything it reaches: another package by its name, a declared third-party module by a
// subpath, and its own interface by a relative path.
import { beta } from "@retroplug/beta";
import { z } from "zod/v4";
import type { Iface } from "./types";

export const alpha = (value: Iface): number => beta + z.number().parse(value.x);
