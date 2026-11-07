// Beside the import nothing resolves: an unresolvable import must not hide the rest of the package's refusals.
import { z } from "zod";

export const other = z;
