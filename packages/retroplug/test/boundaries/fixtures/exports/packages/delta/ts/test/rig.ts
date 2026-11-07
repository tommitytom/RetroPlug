// A test module a package publishes through `exports`, which no other package may reach: a test file may import the
// package's devDependencies, and a source reaching through this export would get them transitively.
export const rig = 1;
