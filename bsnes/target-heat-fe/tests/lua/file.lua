frames = 0
expected_file = 1

assert(io == nil and os == nil and package == nil and require == nil)
assert(file.write("frames.txt", "start\n"))
local escaped = pcall(file.write, "../escape.txt", "bad")
assert(not escaped)

function on_frame()
  frames = frames + 1
  assert(file.append("frames.txt", frames .. "\n"))
  assert(file.read("frames.txt") == "start\n" .. table.concat((function()
    local lines = {}
    for i = 1, frames do lines[i] = i .. "\n" end
    return lines
  end)()))
end
