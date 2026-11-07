// A test file may use its package's devDependencies, a declared type package's globals included.
/// <reference types="node" />
import { harness } from "@retroplug/testing";

export const test = harness;
