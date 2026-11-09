// A comment naming "gb", "sms" and ".nes" is not a literal: the walk reads the tree, and a comment is trivia.
export function classify(name: string): string {
  const platform = "gb";
  const extension = ".nes";
  const pattern = "*.gba";
  // A template with a substitution still holds literals: its head and each span's text are read.
  const middle = `${name}gb${name}`;
  // A literal AFTER a substitution template, where a bare token scan that never re-scanned a span has lost its place.
  const afterTemplate = ".gbc";
  // A regular expression holding a quote does not open a string.
  const quoted = /["']/;
  // A literal after that regular expression.
  const afterRegex = ".sms";
  return [platform, extension, pattern, middle, afterTemplate, afterRegex, quoted.source].join("/");
}
