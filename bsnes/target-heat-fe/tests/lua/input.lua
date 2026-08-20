frames = 0

function on_frame()
  frames = frames + 1
  assert(input.held(1, "B"))
  assert(input.value(1, "Right") == 1)
  assert(input.value(1, 4) == 1)
  assert(not input.held(1, "A"))
end
