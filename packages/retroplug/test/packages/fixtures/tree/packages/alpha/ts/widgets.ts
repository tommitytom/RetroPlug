// A fixture declaring package's runtime registry: the generated script entry imports this module's `register` and hands
// it the contributing manifests' modules in product order.
const registered: { id: string }[] = [];

export interface WidgetRuntime {
  readonly id: string;
}

export function registerWidgets(items: readonly WidgetRuntime[]): void {
  for (const item of items) registered.push(item);
}

export function registeredWidgets(): readonly WidgetRuntime[] {
  return registered;
}
