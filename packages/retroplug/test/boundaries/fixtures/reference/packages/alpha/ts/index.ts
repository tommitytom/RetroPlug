// A package that reaches the shell through a triple-slash reference path, which a compiler follows exactly as it
// follows an import: the same relative rule refuses it.
/// <reference path="../../retroplug/src/a.ts" />

export const alpha = 1;
