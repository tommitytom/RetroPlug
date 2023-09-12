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
local tags = require "luact.tags"
local Reconciler = require "luact.reconciler"
local render = require "luact.fiber.render"
local work_loop = require "luact.fiber.work_loop"
local frame = require "luact.timers.frame"
local timeout = require "luact.timers.timeout"
local idle = require "luact.timers.idle"
local Meta = require "luact.meta"
local init_context = require "luact.context.create"

local function init(reconciler)
  reconciler = setmetatable(reconciler, Reconciler)

  local function Fragment(props)
    return {
      reconciler = reconciler,
      type = tags.type.FRAGMENT,
      props = props,
    }
  end

  local function component(renderer)
    return function (props)
      return {
        reconciler = reconciler,
        type = tags.type.COMPONENT,
        constructor = renderer,
        props = props,
      }
    end
  end

  local function basic(renderer)
    return function (props)
      return {
        reconciler = reconciler,
        type = tags.type.BASIC,
        constructor = renderer,
        props = props,
      }
    end
  end

  local function memo(renderer)
    return function (props)
      return {
        reconciler = reconciler,
        type = tags.type.MEMO,
        constructor = renderer,
        props = props,
      }
    end
  end

  local function memo_basic(renderer)
    return function (props)
      return {
        reconciler = reconciler,
        type = tags.type.MEMO_BASIC,
        constructor = renderer,
        props = props,
      }
    end
  end

  local function Element(constructor, props)
    return {
      reconciler = reconciler,
      type = tags.type.HOST,
      constructor = constructor,
      props = props,
    }
  end

  local function ErrorBoundary(props)
    return {
      reconciler = reconciler,
      type = tags.type.ERROR_BOUNDARY,
      props = props,
    }
  end

  return {
    Fragment = Fragment,
    ErrorBoundary = ErrorBoundary,
    component = component,
    basic = basic,
    memo = memo,
    memo_basic = memo_basic,
    Element = Element,
    render = function (element, container)
      render(reconciler, element, container)
    end,
    work_loop = work_loop(reconciler),
    create_meta = Meta(reconciler),
    create_context = init_context(reconciler),
  }
end

return {
  init = init,

  use_callback = require "luact.hooks.use_callback",
  use_constant = require "luact.hooks.use_constant",
  use_context = require "luact.hooks.use_context",
  use_force_update = require "luact.hooks.use_force_update",
  use_layout_effect = require "luact.hooks.use_layout_effect",
  use_memo = require "luact.hooks.use_memo",
  use_reducer = require "luact.hooks.use_reducer",
  use_ref = require "luact.hooks.use_ref",
  use_render_count = require "luact.hooks.use_render_count",
  use_state = require "luact.hooks.use_state",

  update_frame = function (dt)
    frame.update(dt)
    timeout.update(dt)
  end,
  prevent_idle = function ()
    idle.prevent()
  end,
}