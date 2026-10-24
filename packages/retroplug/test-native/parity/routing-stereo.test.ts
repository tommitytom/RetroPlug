// stereo routing; the cases are in routing.ts.
import { runGroup } from "./group";
import { routingCases } from "./routing";

runGroup("routing-stereo", routingCases("stereo"));
