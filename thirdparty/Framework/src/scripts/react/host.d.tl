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
		Unknown: number
		Backspace: number
		Tab: number
		Clear: number
		Enter: number
		Shift: number
		Ctrl: number
		Alt: number
		Pause: number
		Caps: number
		Esc: number
		Space: number
		PageUp: number
		PageDown: number
		End: number
		Home: number
		LeftArrow: number
		UpArrow: number
		RightArrow: number
		DownArrow: number
		Select: number
		Print: number
		Execute: number
		PrintScreen: number
		Insert: number
		Delete: number
		Help: number
		Num0: number
		Num1: number
		Num2: number
		Num3: number
		Num4: number
		Num5: number
		Num6: number
		Num7: number
		Num8: number
		Num9: number
		A: number
		B: number
		C: number
		D: number
		E: number
		F: number
		G: number
		H: number
		I: number
		J: number
		K: number
		L: number
		M: number
		N: number
		O: number
		P: number
		Q: number
		R: number
		S: number
		T: number
		U: number
		V: number
		W: number
		X: number
		Y: number
		Z: number
		LeftWin: number
		RightWin: number
		Sleep: number
		NumPad0: number
		NumPad1: number
		NumPad2: number
		NumPad3: number
		NumPad4: number
		NumPad5: number
		NumPad6: number
		NumPad7: number
		NumPad8: number
		NumPad9: number
		Multiply: number
		Add: number
		Separator: number
		Subtract: number
		Decimal: number
		Divide: number
		F1: number
		F2: number
		F3: number
		F4: number
		F5: number
		F6: number
		F7: number
		F8: number
		F9: number
		F10: number
		F11: number
		F12: number
		F13: number
		F14: number
		F15: number
		F16: number
		F17: number
		F18: number
		F19: number
		F20: number
		F21: number
		F22: number
		F23: number
		F24: number
		NumLock: number
		Scroll: number
		LeftShift: number
		RightShift: number
		LeftCtrl: number
		RightCtrl: number
		LeftMenu: number
		RightMenu: number

		Oem1: number
		Oem2: number
		Oem3: number
		Oem4: number
		Oem5: number
		Oem6: number
		Oem7: number
		Oem8: number

		OemPlus: number
		OemComma: number
		OemMinus: number
		OemPeriod: number
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
		padding: FlexRectT|number
		margin: FlexRectT|number
		border: FlexBorderT|number
		overflow: integer
		flexPositionType: integer
		color: Color4FT
		backgroundColor: Color4FT
		cursor: integer
		fontFamily: FontFamilyValueT|string
		fontSize: LengthValueT|number

		borderBottomWidth: LengthValueT|number
		borderTopWidth: LengthValueT|number
		borderLeftWidth: LengthValueT|number
		borderRightWidth: LengthValueT|number
		borderBottomColor: Color4FT
		borderTopColor: Color4FT
		borderLeftColor: Color4FT
		borderRightColor: Color4FT
		borderBottomStyle: BorderStyleType
		borderTopStyle: BorderStyleType
		borderLeftStyle: BorderStyleType
		borderRightStyle: BorderStyleType

		marginBottom: FlexValueT|number
		marginTop: FlexValueT|number
		marginLeft: FlexValueT|number
		marginRight: FlexValueT|number

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
