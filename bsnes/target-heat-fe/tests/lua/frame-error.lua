expected_frame_error = 1

function on_frame()
  gui.pixel(10, 10, 0xffffffff)
  error("intentional frame failure")
end
