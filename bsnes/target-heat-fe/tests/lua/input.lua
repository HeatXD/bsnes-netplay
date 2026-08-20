frames = 0
expected_input = 1

function on_frame()
  frames = frames + 1
  b_held = input.held(1, "B") and 1 or 0
  right_value = input.value(1, "Right")
  right_index = input.value(1, 4)
  a_held = input.held(1, "A") and 1 or 0
end
