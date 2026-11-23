// The console lookups follow the registry (docs/downloadable-packs-m2a-plan.md, Task 2): a console and a real cart
// committed after the built-ins classify, place and drive like a built-in one, and a commit the console checks refuse
// changes nothing. The console here is a fixture: its platform, extension, magic and node type are this file's own.
import { test, expect } from "@retroplug/testing";
import type { ConsoleDescriptor, RealCart } from "@retroplug/core/consoles";
import { commitContributions } from "../../src/contributions/registry";
import { consolesRegistry, realCartsRegistry } from "../../src/contributions/consoleRegistries";
import {
  classifyRom,
  consoleDescriptors,
  consoleFor,
  defaultCoreFor,
  detectPlatform,
  platformForExtension,
  realCartOf,
  registeredCores,
  registeredPlatforms,
  registeredRealCarts,
  romExtensions,
  sniffLengths,
  systemNodeTypeFor,
} from "../../src/consoles";

const MAGIC = [0x52, 0x50, 0x4b, 0x21]; // "RPK!"

const fixtureConsole = {
  id: "packfixture",
  core: "packfixture-core",
  nodeType: "rp.packfixture.system",
  platforms: [{ id: "packfixture", label: "Pack Fixture", extensions: [".rpk"] }],
  detect: {
    prefixBytes: 4,
    rank: 900,
    run: (bytes: Uint8Array) => (MAGIC.every((b, i) => bytes[i] === b) ? "packfixture" : null),
  },
} as unknown as ConsoleDescriptor;

const fixtureCart = { platform: "packfixture", cartNode: "rp.packfixture.cart" } as unknown as RealCart;

test("the built-in consoles are seeded before anything reads them", () => {
  expect(consolesRegistry.seeded).toBe(true);
  expect(consoleDescriptors().length).toBeGreaterThan(0);
  expect(consoleDescriptors()).toBe(consolesRegistry.items());
});

test("a console committed later is classified, placed and listed", () => {
  const before = registeredPlatforms().length;
  expect(detectPlatform(new Uint8Array(MAGIC))).toBe("unknown");
  commitContributions([
    { registry: consolesRegistry, pkg: "packfixture", items: [fixtureConsole] },
    { registry: realCartsRegistry, pkg: "packfixture", items: [fixtureCart] },
  ]);
  expect(registeredPlatforms().length).toBe(before + 1);
  expect(detectPlatform(new Uint8Array(MAGIC))).toBe("packfixture");
  expect(classifyRom({ readFilePrefix: () => new Uint8Array(MAGIC) }, "/x/song.bin")).toBe("packfixture");
  expect(consoleFor("rp.packfixture.system")?.id).toBe("packfixture");
  expect(systemNodeTypeFor("packfixture")).toBe("rp.packfixture.system");
  expect(defaultCoreFor("packfixture")).toBe("packfixture-core");
  expect(registeredCores().includes("packfixture-core")).toBe(true);
  expect(romExtensions().includes(".rpk")).toBe(true);
  expect(platformForExtension(".rpk")).toBe("packfixture");
  expect(sniffLengths().includes(4)).toBe(true);
  expect(realCartOf("packfixture")?.cartNode).toBe("rp.packfixture.cart");
  expect(registeredRealCarts().some((cart) => cart.cartNode === "rp.packfixture.cart")).toBe(true);
});

test("a console declaring a platform already registered is refused, and nothing moves", () => {
  const count = consoleDescriptors().length;
  const clash = { ...fixtureConsole, id: "packfixture-two", nodeType: "rp.packfixture.two" } as ConsoleDescriptor;
  expect(() => commitContributions([{ registry: consolesRegistry, pkg: "packfixture-two", items: [clash] }])).toThrow(
    "two console descriptors declare the platform packfixture: packfixture and packfixture-two",
  );
  expect(consoleDescriptors().length).toBe(count);
});

test("a real cart naming a platform no console registers is refused", () => {
  const orphan = { platform: "nowhere", cartNode: "rp.packfixture.orphan" } as unknown as RealCart;
  expect(() => commitContributions([{ registry: realCartsRegistry, pkg: "packfixture", items: [orphan] }])).toThrow(
    "the real cart rp.packfixture.orphan names the platform nowhere, which no console descriptor registers",
  );
});
