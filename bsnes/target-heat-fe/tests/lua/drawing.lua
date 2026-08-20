frames = 0
expected_commands = 4

function on_frame()
  frames = frames + 1
  gui.box(-8, -8, 80, 40, {fill = 0x4000ff00, outline = 0xffff0000, thickness = 2})
  gui.line(0, 0, 255, 223, 0xff00ffff, 1)
  gui.pixel(128, 112, 0xffffffff)
  gui.text(8, 8, 0x2a, {color = 0xffffffff, outline = 0xff000000, size = 12})
end
