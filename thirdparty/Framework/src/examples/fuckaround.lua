local fun = require("fun")

local CannonTypes = {
	"Standard",
	"PeaShooter",
	"Hornet",
	"Quedy",
	"Launcher",
	"MulletWill",
	"Pengoo",
	"BubbleGum",
	"CornDog"
}

fun.each(print, fun.map(function(i, v) return i, v end, fun.enumerate(CannonTypes)))

fun.each(print, fun.map(function(i, v) return i end, CannonTypes))

fun.enumerate(CannonTypes):map(function(i, v) return i, v end):each(print)

fun.iter(CannonTypes):map(function(i) return i end):each(print)
