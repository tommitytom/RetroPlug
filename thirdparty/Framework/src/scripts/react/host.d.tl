require("base")

global type HostElementHandle = integer

global record fw
	record PointF32T
		userdata
		x: number
		y: number
	end

	record MouseMoveEvent
		userdata
		position: PointF32T
	end

	record MouseButtonEvent
		userdata
		position: PointF32T
		button: integer
		down: boolean
	end

	record MouseEnterEvent
		userdata
		position: PointF32T
	end

	record KeyEvent
		userdata
		key: integer
		action: integer
		down: boolean
	end

	record MouseLeaveEvent
		userdata
	end

	record Color4FT
		userdata
		r: number
		g: number
		b: number
		a: number
	end

	Color4F: function(number, number, number, number): Color4FT

	record LengthValueT
		userdata
		type: any
		value: number
	end

	LengthValue: function(number): LengthValueT

	record FlexValueT
		userdata
		unit: any
		value: number
	end

	FlexValue: function(number): FlexValueT

	record FlexRectT
		userdata
		top: FlexValueT
		left: FlexValueT
		bottom: FlexValueT
		right: FlexValueT
	end

	FlexRect: function(FlexValueT|number, FlexValueT|number, FlexValueT|number, FlexValueT|number): FlexRectT

	record FlexBorderT
		userdata
		top: number
		left: number
		bottom: number
		right: number
	end

	FlexBorder: function(number, number, number, number): FlexBorderT

	record FontFamilyValueT
		userdata
		familyName: string
		generic: any
	end

	FontFamilyValue: function(number, number, number, number): FontFamilyValueT

	record VirtualKey
		Unknown: integer
		Backspace: integer
		Tab: integer
		Clear: integer
		Enter: integer
		Shift: integer
		Ctrl: integer
		Alt: integer
		Pause: integer
		Caps: integer
		Esc: integer
		Space: integer
		PageUp: integer
		PageDown: integer
		End: integer
		Home: integer
		LeftArrow: integer
		UpArrow: integer
		RightArrow: integer
		DownArrow: integer
		Select: integer
		Print: integer
		Execute: integer
		PrintScreen: integer
		Insert: integer
		Delete: integer
		Help: integer
		Num0: integer
		Num1: integer
		Num2: integer
		Num3: integer
		Num4: integer
		Num5: integer
		Num6: integer
		Num7: integer
		Num8: integer
		Num9: integer
		A: integer
		B: integer
		C: integer
		D: integer
		E: integer
		F: integer
		G: integer
		H: integer
		I: integer
		J: integer
		K: integer
		L: integer
		M: integer
		N: integer
		O: integer
		P: integer
		Q: integer
		R: integer
		S: integer
		T: integer
		U: integer
		V: integer
		W: integer
		X: integer
		Y: integer
		Z: integer
		LeftWin: integer
		RightWin: integer
		Sleep: integer
		NumPad0: integer
		NumPad1: integer
		NumPad2: integer
		NumPad3: integer
		NumPad4: integer
		NumPad5: integer
		NumPad6: integer
		NumPad7: integer
		NumPad8: integer
		NumPad9: integer
		Multiply: integer
		Add: integer
		Separator: integer
		Subtract: integer
		Decimal: integer
		Divide: integer
		F1: integer
		F2: integer
		F3: integer
		F4: integer
		F5: integer
		F6: integer
		F7: integer
		F8: integer
		F9: integer
		F10: integer
		F11: integer
		F12: integer
		F13: integer
		F14: integer
		F15: integer
		F16: integer
		F17: integer
		F18: integer
		F19: integer
		F20: integer
		F21: integer
		F22: integer
		F23: integer
		F24: integer
		NumLock: integer
		Scroll: integer
		LeftShift: integer
		RightShift: integer
		LeftCtrl: integer
		RightCtrl: integer
		LeftMenu: integer
		RightMenu: integer

		Oem1: integer
		Oem2: integer
		Oem3: integer
		Oem4: integer
		Oem5: integer
		Oem6: integer
		Oem7: integer
		Oem8: integer

		OemPlus: integer
		OemComma: integer
		OemMinus: integer
		OemPeriod: integer
	end

	record FlexAlign
		Auto: integer
		FlexStart: integer
		Center: integer
		FlexEnd: integer
		Stretch: integer
		Baseline: integer
		SpaceBetween: integer
		SpaceAround: integer
	end

	record FlexDimension
		Width: integer
		Height: integer
	end

	record LayoutDirection
		Inherit: integer
		LTR: integer
		RTL: integer
	end

	record FlexDisplay
		Flex: integer
		None: integer
	end

	record FlexEdge
		Left: integer
		Top: integer
		Right: integer
		Bottom: integer
		Start: integer
		End: integer
		Horizontal: integer
		Vertical: integer
		All: integer
	end

	record FlexDirection
		Column: integer
		ColumnReverse: integer
		Row: integer
		RowReverse: integer
	end

	record FlexGutter
		Column: integer
		Row: integer
		All: integer
	end

	record FlexJustify
		FlexStart: integer
		Center: integer
		FlexEnd: integer
		SpaceBetween: integer
		SpaceAround: integer
		SpaceEvenly: integer
	end

	record FlexMeasureMode
		Undefined: integer
		Exactly: integer
		AtMost: integer
	end

	record FlexNodeType
		Default: integer
		Text: integer
	end

	record FlexOverflow
		Visible: integer
		Hidden: integer
		Scroll: integer
	end

	record FlexPositionType
		Static: integer
		Relative: integer
		Absolute: integer
	end

	record FlexUnit
		Undefined: integer
		Point: integer
		Percent: integer
		Auto: integer
	end

	record FlexWrap
		NoWrap: integer
		Wrap: integer
		WrapReverse: integer
	end

	record MouseButton
		Unknown: integer
		Left: integer
		Right: integer
		Middle: integer
	end

	record ButtonType
		Right: integer
		Left: integer
		Up: integer
		Down: integer
		A: integer
		B: integer
		Select: integer
		Start: integer
	end

	record CursorType
		Arrow: integer
		Hand: integer
		IBeam: integer
		Crosshair: integer
		ResizeEW: integer
		ResizeNS: integer
		ResizeNWSE: integer
		ResizeNESW: integer
		NotAllowed: integer
		--ResizeH: integer
		--ResizeV: integer
	end


	record LengthType
		Default: integer
		Small: integer
		Large: integer
		Dynamic: integer
		Em: integer
		Rem: integer
		Pixel: integer
		Point: integer
	end

	record BorderStyleType
		None: integer
		Hidden: integer
		Dotted: integer
		Dashed: integer
		Solid: integer
		Double: integer
		Groove: integer
		Ridge: integer
		Inset: integer
		Outset: integer
	end

	record HostStyle
		flexDirection: integer
		justifyContent: integer
		flexAlignItems: integer
		flexAlignSelf: integer
		flexAlignContent: integer
		layoutDirection: integer
		flexWrap: integer
		flexGrow: number
		flexShrink: number
		flexBasis: integer
		minWidth: FlexValueT|number
		maxWidth: FlexValueT|number
		minHeight: FlexValueT|number
		maxHeight: FlexValueT|number
		width: FlexValueT|number|string
		height: FlexValueT|number|string
		aspectRatio: number
		position: FlexRectT

		overflow: integer
		flexPositionType: integer
		color: Color4FT
		backgroundColor: Color4FT
		cursor: integer
		fontFamily: FontFamilyValueT|string
		fontSize: LengthValueT|number

		border: FlexBorderT|number

		borderWidth: LengthValueT|number
		borderBottomWidth: LengthValueT|number
		borderTopWidth: LengthValueT|number
		borderLeftWidth: LengthValueT|number
		borderRightWidth: LengthValueT|number

		borderColor: Color4FT
		borderBottomColor: Color4FT
		borderTopColor: Color4FT
		borderLeftColor: Color4FT
		borderRightColor: Color4FT

		borderStyle: BorderStyleType
		borderBottomStyle: BorderStyleType
		borderTopStyle: BorderStyleType
		borderLeftStyle: BorderStyleType
		borderRightStyle: BorderStyleType

		margin: FlexRectT|number

		marginBottom: FlexValueT|number
		marginTop: FlexValueT|number
		marginLeft: FlexValueT|number
		marginRight: FlexValueT|number

		padding: FlexRectT|number

		paddingBottom: FlexValueT|number
		paddingTop: FlexValueT|number
		paddingLeft: FlexValueT|number
		paddingRight: FlexValueT|number
	end

	record HostElement
		{Element|string}

		-- Style properties
		className: string|{string}
		style: HostStyle
		tabIndex: integer

		ref: any

		-- Events
		onMouseButton: function(MouseButtonEvent)
		onMouseMove: function(MouseMoveEvent)
		onMouseEnter: function(MouseEnterEvent)
		onMouseLeave: function()
		onMouseOver: function(MouseEnterEvent)
		onMouseOut: function()
		onMouseFocus: function()
		onMouseBlur: function()

		onKey: function(KeyEvent):boolean|nil

		children: {Element|string}
	end

	record HostEvents
		onMouseMove: MouseMoveEvent
	end

	record HostDocument
		userdata
		createElement: function(tag: string): HostElementHandle
		createTextNode: function(text: string): HostElementHandle
		appendChild: function(node: HostElementHandle, child: HostElementHandle)
		removeChild: function(node: HostElementHandle, child: HostElementHandle, destroy: boolean)
		getStyle: function(node: HostElementHandle): HostStyle
		getRootElement: function(): HostElementHandle
	end

	document: HostDocument
	getTime: function(): number
	loadStyle: function(string)
end
