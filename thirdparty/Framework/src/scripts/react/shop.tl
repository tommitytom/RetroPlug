local React = require("react")
local r = require("react-elements")

local record Product
	category: string
	price: string
	stocked: boolean
	name: string
end

local PRODUCTS: {Product} = {
	{ category = "Fruits", price = "$1", stocked = true, name = "Apple" },
	{ category = "Fruits", price = "$1", stocked = true, name = "Dragonfruit" },
	{ category = "Fruits", price = "$2", stocked = false, name = "Passionfruit" },
	{ category = "Vegetables", price = "$2", stocked = true, name = "Spinach" },
	{ category = "Vegetables", price = "$4", stocked = false, name = "Pumpkin" },
	{ category = "Vegetables", price = "$1", stocked = true, name = "Peas" }
}

local record ProductRowProps
	product: Product
end
local ProductRow = React.component(function (props: ProductRowProps): Element
	return r.tr {
		r.td { props.product.name },
		r.td { tostring(props.product.price) }
	}
end)

local record ProductCategoryRowProps
	category: string
end
local ProductCategoryRow = React.component(function (props: ProductCategoryRowProps): Element
	return r.tr {
		r.th {
			colSpan = 2,
			props.category
		}
	}
end)

local record ProductTableProps
	products: {Product}
	filterText: string
	inStockOnly: boolean
end
local ProductTable = React.component(function (props: ProductTableProps): Element
	local rows: {Element} = {}
	local lastCategory: string = nil

	local function productValid(prod: Product): boolean
		if prod.name:lower():find(props.filterText:lower()) == nil then
			return false
		end

		if props.inStockOnly and not prod.stocked then
			return false
		end

		return true
	end

	for _, v in ipairs(props.products) do
		if productValid(v) == true then
			if v.category ~= lastCategory then
				table.insert(rows, ProductCategoryRow { category = v.category, key = v.category })
			end

			table.insert(rows, ProductRow { product = v, key = v.name })
			lastCategory = v.category
		end
	end

	return r.table {
		r.thead {
			r.tr {
				r.th { "Name" },
				r.th { "Price" }
			}
		},
		r.tbody { table.unpack(rows) }
	}
end)

local record SearchBarProps
	products: {Product}
	filterText: string
	inStockOnly: boolean
	onFilterTextChange: function(string)
	onInStockOnlyChange: function(boolean)
end
local SearchBar = React.component(function (props: SearchBarProps): Element
	return r.form {
		r.input {
			type = "text",
			value = props.filterText,
			placeholder = "Search...",
			onChange = function (e: string)
				--props.onFilterTextChange(e.target.value)
				props.onFilterTextChange(e)
			end
		},
		r.label {
			r.input {
				type = "checkbox",
				checked = props.inStockOnly,
				onChange = function (_: string)
					--props.onInStockOnlyChange(e.target.checked)
				end
			},
			"Only show products in stock"
		}
	}
end)

local record FilterableProductTableState
	products: {Product}
	filterText: string
	inStockOnly: boolean
end
local FilterableProductTable = React.component(function (props: FilterableProductTableState): Element
	local filterText, setFilterText = React.useState('')
	local inStockOnly, setInStockOnly = React.useState(false)

	return (
		r.div {
			SearchBar {
				filterText = filterText,
				inStockOnly = inStockOnly,
				onFilterTextChange = function(e: string) setFilterText(e) end,
				onInStockOnlyChange = function(e: boolean) setInStockOnly(e) end,
			},
			ProductTable {
				products = props.products,
				filterText = filterText,
				inStockOnly = inStockOnly
			}
		}
	);
end)

local App = React.component(function (_: PropsMap): Element
	return FilterableProductTable { products = PRODUCTS }
end)

React.render(App {})
