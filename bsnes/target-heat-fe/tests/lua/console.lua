frames = 0
expected_console = 1

console.clear()
print("start", 42, true, nil)
console.log("ready")

function on_frame()
  frames = frames + 1
  console.log("frame", frames)
end
