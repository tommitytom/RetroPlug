import { Effect } from "../effects/Effect";

export interface EffectInstance {
	id: string;
	name: string;
	effect: Effect;
}
