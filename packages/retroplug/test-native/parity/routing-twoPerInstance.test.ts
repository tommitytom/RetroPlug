// twoPerInstance routing; the cases are in routing.ts.
import { runGroup } from "./group";
import { routingCases } from "./routing";

runGroup("routing-twoPerInstance", routingCases("twoPerInstance"));
