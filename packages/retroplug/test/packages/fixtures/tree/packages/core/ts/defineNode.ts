// The fixture core's node type, enough for the generated entry's type import.
export interface DefinedNode<T = unknown> {
  readonly id: string;
  readonly settings?: T;
}
