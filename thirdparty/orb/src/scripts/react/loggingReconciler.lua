type DomElementHandle = number;

type Reconciler = {
	appendChild: (DomElementHandle, DomElementHandle) -> (),
	removeChild: (DomElementHandle, DomElementHandle) -> (),
	addEventListener: (DomElementHandle, string, any) -> (),
	removeEventListener: (DomElementHandle, string, any) -> (),
	getStyle: (DomElementHandle) -> (any),
	createElement: (string) -> (DomElementHandle),
	createTextNode: (string) -> (DomElementHandle),
	getRootElement: () -> (DomElementHandle)
}

local loggingReconciler: Reconciler = {
	count = 0,

	appendChild = function (node, child) print("", "appendChild"); end,
	removeChild = function (node, child) print("", "removeChild"); end,
	addEventListener = function (node, child, func) print("", "addEventListener"); end,
	removeEventListener = function (node, child, func) print("", "removeEventListener"); end,
	getStyle = function (node) print("", "getStyle"); return {} end,
	createElement = function (tag) print("", "createElement"); return 0 end,
	createTextNode = function (text) print("", "createTextNode"); return 0 end,
	getRootElement = function () print("", "getRootElement"); return 0 end
}

return loggingReconciler
