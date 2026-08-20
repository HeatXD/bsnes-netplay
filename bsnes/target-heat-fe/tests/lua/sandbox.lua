assert(debug == nil)
assert(io == nil)
assert(os == nil)
assert(package == nil)
assert(require == nil)
assert(dofile == nil)
assert(loadfile == nil)

frames = 0

function on_frame()
  frames = frames + 1
end
