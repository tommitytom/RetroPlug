// rp:fx-a: a host entry, evaluated as the control plane is, importing the shared modules by name.
import { box } from "fx:state";
import { bump } from "fx:dep";
bump();
globalThis.__fxA = box;
