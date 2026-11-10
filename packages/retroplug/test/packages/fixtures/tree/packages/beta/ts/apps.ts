// The launcher package's runtime registry, as the fixture tree sees it: the generated script entry calls
// registerControllerApps before the nodes, so this stub records what it was handed. test/packages/run-generator.mjs
// aliases @retroplug/launchpad/apps here.
const registered: { id: string }[] = [];

export function registerControllerApps(apps: readonly { id: string }[]): void {
  for (const app of apps) registered.push(app);
}

export function controllerApp(id: string): { id: string } | undefined {
  return registered.find((app) => app.id === id);
}
