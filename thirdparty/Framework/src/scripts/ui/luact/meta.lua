--[[
  @license
  MIT License

  Copyright (c) 2020 Alexis Munsayac
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.


  @author Alexis Munsayac <alexis.munsayac@gmail.com>
  @copyright Alexis Munsayac 2020
--]]
local update = require "luact.fiber.update"
local tags = require "luact.tags"
local assign_table = require "luact.utils.assign_table"

return function (reconciler)
  local BaseMeta = {}
  BaseMeta.__index = BaseMeta

  function BaseMeta:component_will_mount()
  end

  function BaseMeta:component_will_update()
  end

  function BaseMeta:component_will_unmount()
  end

  function BaseMeta:component_did_update()
  end

  function BaseMeta:component_did_mount()
  end

  function BaseMeta:component_did_update()
  end

  function BaseMeta:render()
  end

  function BaseMeta:set_state(action)
    if (type(action) == "function") then
      action = action(self.state)
    end

    -- schedule update
    self.state = assign_table(self.state, action)
    update(reconciler)
  end

  return function (setup)
    local Meta = setmetatable({}, BaseMeta)
    Meta.__index = Meta
    setup(Meta)

    function Meta.new(props)
      return setmetatable({
        props = props,
      }, Meta)
    end

    return function (props)
      return {
        type = tags.type.META,
        props = props,
        constructor = Meta
      }
    end
  end
end