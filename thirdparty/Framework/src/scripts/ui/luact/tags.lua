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
local WORK_TAGS = {
  NO_EFFECT = "NO_EFFECT",
  PERFORMED_WORK = "PERFORMED_WORK",
  PLACEMENT = "PLACEMENT",
  UPDATE = "UPDATE",
  DELETE = "DELETE",
  REPLACEMENT = "REPLACEMENT",
}

local TYPE_TAGS = {
  COMPONENT = "COMPONENT",
  HOST = "HOST",
  ROOT = "ROOT",
  FRAGMENT = "FRAGMENT",
  BASIC = "BASIC",
  MEMO = "MEMO",
  MEMO_BASIC = "MEMO_BASIC",
  META = "META",
  ERROR_BOUNDARY = "ERROR_BOUNDARY",
  CONTEXT_PROVIDER = "CONTEXT_PROVIDER",
  CONTEXT_CONSUMER = "CONTEXT_CONSUMER",
}

local HOOK_TAGS = {
  CALLBACK = "CALLBACK",
  CONSTANT = "CONSTANT",
  CONTEXT = "CONTEXT",
  DEPENDENCY = "DEPENDENCY",
  DISPATCH = "DISPATCH",
  EFFECT = "EFFECT",
  FORCE_UPDATE = "FORCE_UPDATE",
  LAYOUT_EFFECT = "LAYOUT_EFFECT",
  MEMO = "MEMO",
  REF = "REF",
  STATE = "STATE",
}

return {
  work = WORK_TAGS,
  type = TYPE_TAGS,
  hook = HOOK_TAGS,
}
