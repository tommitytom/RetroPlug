// Routes the JS-side `console.*` family through the C++ bridge's stderr
// shim. The tjs runtime ships no native console, so every console.log /
// warn / error in the bundle is a silent no-op without this file.
//
// Import as the very first line of PluginUI.tsx so the polyfill runs
// before any module-scope console call (input.ts / memory.ts / etc.).
// Each call lands as `[js:<level>] <message>` on the standalone's stderr.
//
// The bridge function is __log(level: string, msg: string) → undefined.
// It's registered in src/PluginJsBridge.cpp alongside __rpcSend.

interface PluginNamespace {
    __log?: (level: string, msg: string) => void;
}

const ns = (globalThis as any)[Symbol.for("plugin")] as PluginNamespace | undefined;

function fmt(args: unknown[]): string {
    return args.map((a) => {
        if (typeof a === "string") return a;
        if (a === null || a === undefined) return String(a);
        if (typeof a === "object") {
            try { return JSON.stringify(a); } catch { return String(a); }
        }
        return String(a);
    }).join(" ");
}

if (ns?.__log) {
    const make = (level: string) =>
        (...args: unknown[]) => ns.__log!(level, fmt(args));
    (globalThis as any).console = {
        log:   make("log"),
        warn:  make("warn"),
        error: make("error"),
        info:  make("info"),
        debug: make("debug"),
        trace: make("trace"),
    };
    // Marker so a fresh log file makes it obvious the polyfill ran.
    // Cheap (one syscall at bundle init) and useful when diagnosing
    // "why aren't my console.logs showing up".
    ns.__log("info", "[runtime/console] polyfill installed");
}
