// fx:dep: a shared module importing another shared module by name, so the table's order matters.
import { box } from "fx:state";
export function bump() {
  box.count += 1;
  return box.count;
}
