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

return function (reconciler)
  return function (initial_value)
    local owner = {}

    local function Provider(props)
      local value = props.value

      if (value == nil) then
        value = initial_value
      end

      return {
        reconciler = reconciler,
        type = tags.type.CONTEXT_PROVIDER,
        props = {
          value = value,
          owner = owner,
          key = props.key,
          children = props.children,
        }
      }
    end

    local function Consumer(props)
      return {
        reconciler = reconciler,
        type = tags.type.CONTEXT_CONSUMER,
        props = {
          consume = props.consume,
          owner = owner,
          key = props.key,
        },
      }
    end

    owner.Provider = Provider
    owner.Consumer = Consumer

    return owner
  end
end