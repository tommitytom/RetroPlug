export enum PadButtonType {
	//Axis converted to button presses
	LeftStickLeft,
	LeftStickRight,
	LeftStickDown,
	LeftStickUp,
	RightStickLeft,
	RightStickRight,
	RightStickDown,
	RightStickUp,

	//Actual button presses
	Start,
	Select,
	Left,
	Right,
	Up,
	Down,
	A,
	B,
	X,
	Y,
	L1,
	R1,
	L2,
	R2,
	L3,
	R3,
	Home,
	Button17,
	Button18,
	Button19,
	Button20,
	Button21,
	Button22,
	Button23,
	Button24,
	Button25,
	Button26,
	Button27,
	Button28,
	Button29,
	Button30,
	Button31,

	COUNT
};

export enum VirtualKey {
	Unknown = 0x00,
	Backspace = 0x08,
	Tab = 0x09,
	Clear = 0x0C,
	Enter = 0x0D,
	Shift = 0x10,
	Ctrl = 0x11,
	Alt = 0x12,
	Pause = 0x13,
	Caps = 0x14,
	Esc = 0x1B,
	Space = 0x20,
	PageUp = 0x21,
	PageDown = 0x22,
	End = 0x23,
	Home = 0x24,
	LeftArrow = 0x25,
	UpArrow = 0x26,
	RightArrow = 0x27,
	DownArrow = 0x28,
	Select = 0x29,
	Print = 0x2A,
	Execute = 0x2B,
	PrintScreen = 0x2C,
	Insert = 0x2D,
	Delete = 0x2E,
	Help = 0x2F,
	Num0 = 0x30,
	Num1 = 0x31,
	Num2 = 0x32,
	Num3 = 0x33,
	Num4 = 0x34,
	Num5 = 0x35,
	Num6 = 0x36,
	Num7 = 0x37,
	Num8 = 0x38,
	Num9 = 0x39,
	A = 0x41,
	B = 0x42,
	C = 0x43,
	D = 0x44,
	E = 0x45,
	F = 0x46,
	G = 0x47,
	H = 0x48,
	I = 0x49,
	J = 0x4A,
	K = 0x4B,
	L = 0x4C,
	M = 0x4D,
	N = 0x4E,
	O = 0x4F,
	P = 0x50,
	Q = 0x51,
	R = 0x52,
	S = 0x53,
	T = 0x54,
	U = 0x55,
	V = 0x56,
	W = 0x57,
	X = 0x58,
	Y = 0x59,
	Z = 0x5A,
	LeftWin = 0x5B,
	RightWin = 0x5C,
	Sleep = 0x5F,
	NumPad0 = 0x60,
	NumPad1 = 0x61,
	NumPad2 = 0x62,
	NumPad3 = 0x63,
	NumPad4 = 0x64,
	NumPad5 = 0x65,
	NumPad6 = 0x66,
	NumPad7 = 0x67,
	NumPad8 = 0x68,
	NumPad9 = 0x69,
	Multiply = 0x6A,
	Add = 0x6B,
	Separator = 0x6C,
	Subtract = 0x6D,
	Decimal = 0x6E,
	Divide = 0x6F,
	F1 = 0x70,
	F2 = 0x71,
	F3 = 0x72,
	F4 = 0x73,
	F5 = 0x74,
	F6 = 0x75,
	F7 = 0x76,
	F8 = 0x77,
	F9 = 0x78,
	F10 = 0x79,
	F11 = 0x7A,
	F12 = 0x7B,
	F13 = 0x7C,
	F14 = 0x7D,
	F15 = 0x7E,
	F16 = 0x7F,
	F17 = 0x80,
	F18 = 0x81,
	F19 = 0x82,
	F20 = 0x83,
	F21 = 0x84,
	F22 = 0x85,
	F23 = 0x86,
	F24 = 0x87,
	NumLock = 0x90,
	Scroll = 0x91,
	LeftShift = 0xA0,
	RightShift = 0xA1,
	LeftCtrl = 0xA2,
	RightCtrl = 0xA3,
	LeftMenu = 0xA4,
	RightMenu = 0xA5,

	Oem1 = 0xBA,
	Oem2 = 0xBF,
	Oem3 = 0xC0,
	Oem4 = 0xDB,
	Oem5 = 0xDC,
	Oem6 = 0xDD,
	Oem7 = 0xDE,
	Oem8 = 0xDF,

	OemPlus = 0xBB,
	OemComma = 0xBC,
	OemMinus = 0xBD,
	OemPeriod = 0xBE,

	COUNT
};

export interface IInputConfig {
	keyboard: Record<VirtualKey, PadButtonType>;
	gamepad: Record<PadButtonType, PadButtonType>;
}

export function toVirtualKey(ev: KeyboardEvent): VirtualKey {
	// Use the key property for most cases, falling back to code for special cases
	const key = ev.key;
	const code = ev.code;

	// Handle special keys first
	switch (key) {
		case 'Backspace': return VirtualKey.Backspace;
		case 'Tab': return VirtualKey.Tab;
		case 'Clear': return VirtualKey.Clear;
		case 'Enter': return VirtualKey.Enter;
		case 'Shift': return VirtualKey.Shift;
		case 'Control': return VirtualKey.Ctrl;
		case 'Alt': return VirtualKey.Alt;
		case 'Pause': return VirtualKey.Pause;
		case 'CapsLock': return VirtualKey.Caps;
		case 'Escape': return VirtualKey.Esc;
		case ' ': return VirtualKey.Space;
		case 'PageUp': return VirtualKey.PageUp;
		case 'PageDown': return VirtualKey.PageDown;
		case 'End': return VirtualKey.End;
		case 'Home': return VirtualKey.Home;
		case 'ArrowLeft': return VirtualKey.LeftArrow;
		case 'ArrowUp': return VirtualKey.UpArrow;
		case 'ArrowRight': return VirtualKey.RightArrow;
		case 'ArrowDown': return VirtualKey.DownArrow;
		case 'Select': return VirtualKey.Select;
		case 'Print': return VirtualKey.Print;
		case 'Execute': return VirtualKey.Execute;
		case 'PrintScreen': return VirtualKey.PrintScreen;
		case 'Insert': return VirtualKey.Insert;
		case 'Delete': return VirtualKey.Delete;
		case 'Help': return VirtualKey.Help;
	}

	// Handle number keys (0-9)
	if (key >= '0' && key <= '9') {
		return VirtualKey.Num0 + (key.charCodeAt(0) - '0'.charCodeAt(0));
	}

	// Handle letter keys (A-Z)
	if (key.length === 1 && key >= 'a' && key <= 'z') {
		return VirtualKey.A + (key.toUpperCase().charCodeAt(0) - 'A'.charCodeAt(0));
	}
	if (key.length === 1 && key >= 'A' && key <= 'Z') {
		return VirtualKey.A + (key.charCodeAt(0) - 'A'.charCodeAt(0));
	}

	// Handle function keys
	if (key.startsWith('F') && key.length >= 2) {
		const fNum = parseInt(key.substring(1));
		if (fNum >= 1 && fNum <= 24) {
			return VirtualKey.F1 + (fNum - 1);
		}
	}

	// Handle numpad keys
	if (code.startsWith('Numpad')) {
		const numpadKey = code.substring(6);
		switch (numpadKey) {
			case '0': return VirtualKey.NumPad0;
			case '1': return VirtualKey.NumPad1;
			case '2': return VirtualKey.NumPad2;
			case '3': return VirtualKey.NumPad3;
			case '4': return VirtualKey.NumPad4;
			case '5': return VirtualKey.NumPad5;
			case '6': return VirtualKey.NumPad6;
			case '7': return VirtualKey.NumPad7;
			case '8': return VirtualKey.NumPad8;
			case '9': return VirtualKey.NumPad9;
			case 'Multiply': return VirtualKey.Multiply;
			case 'Add': return VirtualKey.Add;
			case 'Subtract': return VirtualKey.Subtract;
			case 'Decimal': return VirtualKey.Decimal;
			case 'Divide': return VirtualKey.Divide;
		}
	}

	// Handle modifier keys with location specificity
	if (code === 'ShiftLeft') return VirtualKey.LeftShift;
	if (code === 'ShiftRight') return VirtualKey.RightShift;
	if (code === 'ControlLeft') return VirtualKey.LeftCtrl;
	if (code === 'ControlRight') return VirtualKey.RightCtrl;
	if (code === 'AltLeft') return VirtualKey.LeftMenu;
	if (code === 'AltRight') return VirtualKey.RightMenu;
	if (code === 'MetaLeft' || code === 'OSLeft') return VirtualKey.LeftWin;
	if (code === 'MetaRight' || code === 'OSRight') return VirtualKey.RightWin;

	// Handle special character keys
	switch (key) {
		case 'NumLock': return VirtualKey.NumLock;
		case 'ScrollLock': return VirtualKey.Scroll;
		case ';': case ':': return VirtualKey.Oem1;
		case '/': case '?': return VirtualKey.Oem2;
		case '`': case '~': return VirtualKey.Oem3;
		case '[': case '{': return VirtualKey.Oem4;
		case '\\': case '|': return VirtualKey.Oem5;
		case ']': case '}': return VirtualKey.Oem6;
		case "'": case '"': return VirtualKey.Oem7;
		case '+': case '=': return VirtualKey.OemPlus;
		case ',': case '<': return VirtualKey.OemComma;
		case '-': case '_': return VirtualKey.OemMinus;
		case '.': case '>': return VirtualKey.OemPeriod;
	}

	// Handle some additional special keys
	switch (key) {
		case 'Sleep': return VirtualKey.Sleep;
	}

	// Default to Unknown if no match found
	return VirtualKey.Unknown;
}