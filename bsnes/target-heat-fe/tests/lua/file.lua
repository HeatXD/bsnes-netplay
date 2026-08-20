frames = 0
expected_file = 1

assert(io == nil and os == nil and package == nil and require == nil)
assert(type(file.list()) == "table")
assert(file.write("frames.txt", "start\n"))
assert(file.exists("frames.txt"))
assert(file.write("_heat_fe_lua_test/temporary.txt", "temporary"))
assert(file.exists("_heat_fe_lua_test/temporary.txt"))

local function contains(values, wanted)
  for _, value in ipairs(values) do
    if value == wanted then return true end
  end
  return false
end

local root = file.list()
assert(contains(root, "frames.txt") and contains(root, "_heat_fe_lua_test"))
local nested = file.list("_heat_fe_lua_test")
assert(contains(nested, "temporary.txt"))
assert(file.remove("_heat_fe_lua_test/temporary.txt"))
assert(file.remove("_heat_fe_lua_test"))
assert(not file.exists("_heat_fe_lua_test"))
assert(not file.remove("missing.txt"))

local escaped = pcall(file.write, "../escape.txt", "bad")
assert(not escaped)
assert(not pcall(file.list, "../"))

function on_frame()
  frames = frames + 1
  assert(file.append("frames.txt", frames .. "\n"))
  assert(file.read("frames.txt") == "start\n" .. table.concat((function()
    local lines = {}
    for i = 1, frames do lines[i] = i .. "\n" end
    return lines
  end)()))
end
