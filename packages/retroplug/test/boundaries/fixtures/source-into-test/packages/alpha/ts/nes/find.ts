// Source that re-exports one of the package's own test modules: the manifest reads clean (the test file's own import of
// the devDependency is a test file's to make), so the leak is this line, not the dependency.
export { support } from "../test/support";
