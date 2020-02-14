local MenuTest = component({ name = "MenuTest", version = "1.0.0" })

function MenuTest:init()
    self.syncMode = 0
    self.autoPlay = true
end

function MenuTest:onMenu(menu)
    menu:subMenu("LSDj")
            :subMenu("Sync")
                :multiSelect({
                    "Off",
                    "MIDI Sync",
                    "MIDI Sync (Arduinoboy)",
                    "MIDI Map",
                }, self.syncMode, function(idx) self.syncMode = idx end)
                :separator()
                :select("Autoplay", self.autoPlay, function(value) self.autoPlay = value end)
end

return MenuTest
