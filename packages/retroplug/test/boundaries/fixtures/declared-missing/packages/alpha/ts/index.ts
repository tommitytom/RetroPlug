// A declaration that names no package is refused on its own: a typo here would otherwise read as an import the package
// declares, and only the generator's node check would name it.
export const alpha = 1;
