// The in-app file browser core: the glob filter, the request pub/sub, and the MenuTree builder (dirs
// navigate, files pick + pattern-filter, save mode adds a filename prompt). Pure logic — no LVGL/React.
import { test, expect } from "../testing/harness";
import { globToRegExp, matchesPatterns, requestFileBrowser, getFileBrowserRequest, resolveFileBrowser } from "../src/fileBrowser";
import { buildFileBrowserMenu } from "../ui/screens/menu/fileBrowserMenu";
import type { MenuItem } from "../ui/screens/menu/menuTree";

const find = (items: MenuItem[], id: string) => items.find((i) => i.id === id);

test("globToRegExp / matchesPatterns: * and ? are case-insensitive globs", () => {
  expect(globToRegExp("*.gb").test("song.gb")).toBe(true);
  expect(globToRegExp("*.gb").test("SONG.GB")).toBe(true); // FNM_CASEFOLD
  expect(globToRegExp("*.gb").test("song.gbc")).toBe(false);
  expect(globToRegExp("*.ss?").test("a.ss0")).toBe(true);
  expect(globToRegExp("*.ss?").test("a.ss")).toBe(false);
  expect(globToRegExp("*.rplg.zip").test("p.rplg.zip")).toBe(true);
  expect(matchesPatterns("x.nes", ["*.gb", "*.nes"])).toBe(true);
  expect(matchesPatterns("x.txt", ["*.gb", "*.nes"])).toBe(false);
  expect(matchesPatterns("anything", [])).toBe(true); // empty = match all
});

test("request pub/sub: requestFileBrowser resolves when resolveFileBrowser is called", async () => {
  const p = requestFileBrowser({ title: "Open", patterns: ["*.gb"] });
  expect(getFileBrowserRequest()?.opts.title).toBe("Open");
  resolveFileBrowser("/roms/a.gb");
  expect(getFileBrowserRequest()).toBe(null);
  expect(await p).toBe("/roms/a.gb");
  // A second, cancelled browse.
  const p2 = requestFileBrowser({ title: "Open", patterns: [] });
  resolveFileBrowser(null);
  expect(await p2).toBe(null);
});

test("buildFileBrowserMenu (open): dirs navigate, files pattern-filtered + pickable, .. present", () => {
  const listDir = (dir: string) => (dir === "/roms" ? ["b.nes", "a.gb", "sub/", "notes.txt"] : []);
  let navigated: string | null = null;
  let picked: string | null | undefined;
  const tree = buildFileBrowserMenu(listDir, { title: "Load", patterns: ["*.gb", "*.nes"] }, "/roms", {
    navigate: (d) => (navigated = d),
    pick: (p) => (picked = p),
  });

  expect(tree.title).toBe("Load: /roms");
  const ids = tree.items.map((i) => i.id);
  expect(ids.includes("fb-up")).toBeTruthy(); // parent nav (not at root)
  expect(ids.includes("fb-d-sub")).toBeTruthy(); // the subdirectory
  expect(ids.includes("fb-f-a.gb")).toBeTruthy();
  expect(ids.includes("fb-f-b.nes")).toBeTruthy();
  expect(find(tree.items, "fb-f-notes.txt")).toBe(undefined); // filtered out by the patterns

  find(tree.items, "fb-d-sub")!.onSelect!(); // navigate into the dir
  expect(navigated).toBe("/roms/sub");
  find(tree.items, "fb-f-a.gb")!.onSelect!(); // pick a file
  expect(picked).toBe("/roms/a.gb");
  find(tree.items, "fb-up")!.onSelect!(); // up
  expect(navigated).toBe("/");
});

test("buildFileBrowserMenu (save): a filename prompt resolves <dir>/<name>", () => {
  let picked: string | null | undefined;
  const tree = buildFileBrowserMenu(() => ["old.wav"], { title: "Render", patterns: ["*.wav"], saving: true, defaultName: "song.wav" }, "/out", {
    navigate: () => {},
    pick: (p) => (picked = p),
  });
  const prompt = find(tree.items, "fb-saveas")!;
  expect(prompt.kind).toBe("prompt");
  expect(prompt.prompt!.initial).toBe("song.wav");
  expect(prompt.prompt!.onConfirm("")).toBe("Enter a filename."); // empty rejected
  expect(picked).toBe(undefined);
  expect(prompt.prompt!.onConfirm("take2.wav")).toBe(null); // accepted → closes
  expect(picked).toBe("/out/take2.wav");
  // An existing file is pickable for overwrite too.
  find(tree.items, "fb-f-old.wav")!.onSelect!();
  expect(picked).toBe("/out/old.wav");
});
