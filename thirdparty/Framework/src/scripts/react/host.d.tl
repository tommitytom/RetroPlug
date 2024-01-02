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
		width: FlexValueT|number
		height: FlexValueT|number
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
	end

	record HostElement
		{Element|string}

		-- Style properties
		className: string|{string}
		style: HostStyle

		-- Events
		onMouseButton: function(MouseButtonEvent)
		onMouseMove: function(MouseMoveEvent)
		onMouseEnter: function(MouseEnterEvent)
		onMouseLeave: function()
		onMouseOver: function(MouseEnterEvent)
		onMouseOut: function()
		onMouseFocus: function()
		onMouseBlur: function()

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
end
