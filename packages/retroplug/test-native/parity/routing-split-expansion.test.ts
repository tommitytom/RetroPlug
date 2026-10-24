// The split routings on each BlipToaster expansion build; split.ts has the why. Under PinSplit the
// cartridge's chip is the third pin. Under ChannelSplit the tap carries only the 2A03's five core
// channels, so the chip reaches no lane at all, and that is recorded as it is.
import { runGroup, type ParityCase } from "./group";
import { range, single } from "./split";
import { ROMS } from "./scenes";

const cases: ParityCase[] = [];
for (const build of ["mmc5", "n163", "s5b", "vrc6", "vrc7"] as const)
  for (const mode of ["pinSplit", "channelSplit"] as const)
    cases.push(single(`${build}-${mode}-o8`, mode, 8, range(3), (rig) => rig.add(ROMS[build]), range(8)));

runGroup("routing-split-expansion", cases);
