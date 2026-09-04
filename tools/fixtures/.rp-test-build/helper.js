// A sibling helper imported by the tests as "./helper.js" - the specifier the emitted file will use.
// Its own types must strip too, so this covers helper stripping as well as the import resolving.
                                                   
export function tap(cases        )         {
  const lines           = ["TAP version 13", `1..${cases.length}`];
  let failed = 0;
  cases.forEach((c, i) => {
    if (!c.ok) failed++;
    lines.push(`${c.ok ? "ok" : "not ok"} ${i + 1} - ${c.name}`);
  });
  console.log(lines.join("\n"));
  return failed === 0 ? 0 : 1;
}
