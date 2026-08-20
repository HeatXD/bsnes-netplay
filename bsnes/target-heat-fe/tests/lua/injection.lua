frames = 0
before_frames = 0
expected_injection = 1

assert(not pcall(input.set, 1, "A", 40000))
input.set(1, "A", true)

function on_before_frame()
  before_frames = before_frames + 1
  if before_frames == 1 then
    input.set(1, "B", false)
    input.set(1, "Right", 0)
  else
    input.clear(1, "A")
    input.clear(1, "B")
    input.clear(1, "Right")
    input.set(1, "X", true)
    input.clear_all()
  end
end

function on_frame()
  frames = frames + 1
  if frames == 1 then
    first_a = input.held(1, "A") and 1 or 0
    first_b = input.held(1, "B") and 1 or 0
    first_right = input.held(1, "Right") and 1 or 0
    first_y = input.held(1, "Y") and 1 or 0
  else
    second_a = input.held(1, "A") and 1 or 0
    second_b = input.held(1, "B") and 1 or 0
    second_right = input.held(1, "Right") and 1 or 0
  end
end
