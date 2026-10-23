// Regenerates the two canonical-JSON golden tables both writers are held to. JavaScript is the
// reference, so this file is the only place their expected text comes from:
//
//   node packages/native/test/graph/golden/gen-canonical-cases.mjs
//
// canonical-cases.json    {value, text}: a value (already in JS spelling) and its canonical text.
// canonicalize-cases.json {input, text}: raw JSON text, and the canonical text of JSON.parse(input) -
//                         the cases a value table cannot hold: integers beyond 2^53 and uint64, duplicate
//                         keys, keys whose UTF-16 and UTF-8 orders differ, non-canonical number spellings.
import { writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));

// The reference spelling: JSON.stringify for scalars, keys in JavaScript's default (UTF-16) order.
const canon = (v) => {
  if (Array.isArray(v)) return "[" + v.map(canon).join(",") + "]";
  if (v && typeof v === "object")
    return "{" + Object.keys(v).sort().map((k) => JSON.stringify(k) + ":" + canon(v[k])).join(",") + "}";
  return JSON.stringify(v) ?? "null";
};

const numbers = [0, 1, -1, 0.5, -60, 12, 100, 255, 0.1, 1 / 3, 3.14159, 1e21, 1e-7, 1e-6, 0.00001, 1.5e-10,
  2.5e25, 123456789012345680000, 1e300, -1e-300, 48000, 0.70710678, 44100.5, 5e-324, 1.7976931348623157e308,
  999999999999999900000, 0.30000000000000004, 2 ** 53, 2 ** 63, 2 ** 64];
const strings = ["plain", "quote\"inside", "back\\slash", "tab\there", "new\nline", "bell\u0007", "unicode é中",
  "slash/ok", "line separator", "del\u007f", "nul\u0000"];
const objects = [{ b: 1, a: [2, { d: 1, c: 2 }] }, { z: true, a: null, m: "x" }, [], { "！": 1, "😀": 2, a: 3 }];
const valueCases = [...numbers, ...strings, ...objects].map((value) => ({ value, text: canon(value) }));

const inputs = [
  "9007199254740993", "12345678901234567", "9223372036854775807", "9223372036854775808",
  "18446744073709551615", "-9223372036854775808", "18446744073709551616",
  '{"a":1,"a":2}', '{"b":1,"a":2,"b":3}', '{"！":1,"😀":2}', '{"":1,"😀":2,"a":3}',
  "1e2", "1.0", "-0", "1E21", "0.10", ' { "x" : [ 1 , 2.50 ] } ', '"\\u00e9\\/\\u2028"', "[true,false,null]",
  '{"nested":{"b":[{"y":1,"x":2}],"a":{}}}',
];
const parseCases = inputs.map((input) => ({ input, text: canon(JSON.parse(input)) }));

writeFileSync(join(here, "canonical-cases.json"), JSON.stringify(valueCases, null, 2) + "\n");
writeFileSync(join(here, "canonicalize-cases.json"), JSON.stringify(parseCases, null, 2) + "\n");
console.log(`wrote ${valueCases.length} value cases and ${parseCases.length} parse cases`);
