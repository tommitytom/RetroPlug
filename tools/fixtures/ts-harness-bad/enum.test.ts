// Non-erasable: an enum emits runtime code, so stripping must REFUSE rather than silently pass it
// through as invalid JavaScript (ts-blank-space does exactly that if onError is not wired).
enum Mode { A, B }
console.log(Mode.A);
