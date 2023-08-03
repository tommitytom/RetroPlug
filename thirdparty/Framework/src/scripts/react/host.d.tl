require("base")

global type HostElementHandle = integer

global record fw
	record MouseMoveEvent
		userdata
		x: number
		y: number
	end

	record MouseButtonEvent
		userdata
		x: number
		y: number
		button: integer
		pressed: boolean
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

	record HostStyle
		-- Flex properties
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

		-- Style properties
		color: Color4FT
		backgroundColor: Color4FT

		-- Events
		onMouseButton: function(MouseButtonEvent)
		onMouseMove: function(MouseMoveEvent)

		children: {string|Element}
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
