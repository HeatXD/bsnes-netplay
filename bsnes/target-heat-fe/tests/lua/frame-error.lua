expected_frame_error = 1
input.set(1, "A", true)

function on_frame()
  gui.pixel(10, 10, 0xffffffff)
  error("intentional frame failure")
end
