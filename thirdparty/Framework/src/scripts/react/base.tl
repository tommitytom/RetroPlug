global type Reconciler = record<ElementT, PropsT, EventsT>
	appendChild: function(ElementT, ElementT)
	removeChild: function(ElementT, ElementT)
	addEventListener: function(ElementT, string, function(any))
	removeEventListener: function(ElementT, string, function(any))
	setProperty: function(ElementT, string, any)
	getProperty: function(ElementT, string): any
	getStyle: function(ElementT): PropsT
	createElement: function(string): ElementT
	createTextNode: function(string): ElementT
	getRootElement: function(): ElementT
end

global type ElementFunc = function(PropsMap): Element
global type PropsMap = {string: any}

global record Element
	type: string|ElementFunc
	props: PropsMap
end

global type ActionFunc = function<T>(T): T
