// reactView, the kit's way to make a view body out of a React component (docs/modular-graph-phase-f4c-design.md section
// 4.2). It is a wrap and nothing else: the `kind` is what the shell tells one body from another by, and the component is
// handed back untouched, because the shell mounts exactly the function the package wrote. What a type test can hold is
// that a component reading only some of the props the shell hands it is accepted: `width` alone is a component view as
// much as one that takes the whole props object.
import { test, expect } from "@retroplug/testing";

import { reactView } from "../ui/reactView";

/** A component of the whole props object; it draws nothing, which is all this case needs of it. */
const Draw = () => null;
/** A component that reads one prop, so a narrower prop type than the contract's is accepted. */
const DrawWidth = ({ width }: { readonly width: number }) => width;

test("reactView wraps a component as the contract's component kind", () => {
  const body = reactView(Draw);
  expect(body.kind).toBe("component");
  expect(body.component).toBe(Draw);
});

test("a component that reads one prop is a component view too", () => {
  const body = reactView(DrawWidth);
  expect(body.kind).toBe("component");
  expect(body.component).toBe(DrawWidth);
});

test("a component asking for props the shell never hands is refused", () => {
  // The point of reactView: @retroplug/ui cannot type the component, so this function does, and a component with the
  // wrong props fails here rather than in the shell. The error is the case, so the body asserts nothing at runtime.
  // @ts-expect-error a `nope` prop is not one the shell hands a component view
  reactView(({ nope }: { readonly nope: string }) => nope);
});
