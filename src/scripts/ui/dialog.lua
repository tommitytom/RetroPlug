local _dialogCallback
local _supportsMultiple

local function saveFile(filters, cb)
    assert(_dialogCallback == nil)
    _dialogCallback = cb
    _supportsMultiple = false

    if #filters > 1 then

    end

    local desc = {}
    for i, v in ipairs(filters) do
        local f = FileDialogFilters.new()
        f.name = v[1]
        f.extensions = v[2]
        table.insert(desc, f)
    end

    _requestDialog(DialogType.Save, desc)
end

function _handleDialogCallback(paths)
    if _dialogCallback ~= nil then
        if _supportsMultiple == true then
            _dialogCallback(paths)
        else
            _dialogCallback(paths[1])
        end

        _dialogCallback = nil
    end
end

return {
    saveFile = saveFile
}
