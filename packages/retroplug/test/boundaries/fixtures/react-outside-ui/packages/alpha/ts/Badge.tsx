// A .tsx where a package's plain tree is. React belongs to the package's ts/ui/ tree alone, whatever the component
// draws, so the placement is what this tree gets wrong; the fragment needs no import, which leaves the placement the
// one refusal.
export function Badge() {
  return <></>;
}
