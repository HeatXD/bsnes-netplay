# GLSL shaders

The heat frontend runs the same multipass `.shader` packages target-bsnes does,
straight from its own OpenGL context. Pick one under **Settings > Shader**, or
from the Shader row on the Video settings tab.

Packages are read from the **Shaders folder**, which is a `Shaders` directory
beside the executable unless the Paths tab points somewhere else. Each package
is a folder named `<Name>.shader` holding a `manifest.bml` and its `.vs`/`.fs`
sources; the eighteen packages shipped with bsnes are picked up unchanged.

Shaders need OpenGL 3.2. When the driver cannot supply it the frontend still
runs, the Shader menu is greyed out, and the Video tab says so.

## How it fits the rest of the video pipeline

- The CPU **Filter** runs first, then the shader chain sees whatever it
  produced. The two stack, exactly as in target-bsnes.
- The chain renders at the display's pixel resolution, so a CRT mask lands on
  real pixels rather than on the emulated frame.
- Geometry - aspect correction, Output mode, window scale - is still the
  frontend's. A shader changes what the picture looks like, not where it sits.
- **Linear filtering** applies to the raw frame. With a shader loaded the
  package's own `output filter` takes over.
- Normal screenshots keep the raw emulated frame. Screenshots configured to
  include Lua drawings capture the displayed picture, including the shader.

## Manifest support

Everything ruby's OpenGL driver reads is honoured:

| Node | Keys |
|---|---|
| `settings` | any `name: value`, substituted into `#in name` lines |
| `input` | `history`, `format`, `filter`, `wrap` |
| `program` | `vertex`, `geometry`, `fragment`, `width`, `height`, `format`, `filter`, `wrap`, `modulo`, `pixmap` |
| `output` | `filter` |

Uniforms a pass may declare: `source[]`/`sourceSize[]`, `history[]`/
`historySize[]`, `pixmap[]`/`pixmapSize[]`, `targetSize`, `outputSize`, `phase`,
`sourceLength`, `historyLength`, `pixmapLength`, and the `modelView`,
`projection` and `modelViewProjection` matrices. Vertex attributes are
`position`, `texCoord` and `vertex`. A pass that names no vertex or fragment
file gets a passthrough one.

Only samplers a pass actually declares take a texture unit, so a long chain such
as CRT-Royale stays inside the driver's limit instead of running out partway
through.

`format: srgb8` is treated as `rgba8`, as ruby treats it: the packages asking
for it linearize in the shader themselves, and a real sRGB target would apply
the curve twice.

`output width` and `output height` are ignored. In target-bsnes they letterbox
the final blit; here the frontend's Output mode already decides that, and no
shipped package sets them to anything but zero.

## Parameters

A package with a `settings` node lists its values under the Shader row on the
Video tab. Type a new one and press Enter to rebuild the chain. Values that
differ from the manifest are saved in `settings.cfg` and reapplied at startup;
**Reset parameters** puts the manifest's own values back. Only the selected
package's parameters are kept, so switching shaders starts from stock.

**Reload** rebuilds from disk, which is what to press while authoring a package.

## When something goes wrong

A package that will not compile, link or render is reported on the Video tab
with the compiler's own message, and the picture falls back to the unshaded
frame. Nothing is left half-built: a failed load releases every object it made.

`--shader-test` loads every package in the Shaders folder, renders each one at
two sizes, and checks the parameter and settings round-trips. It needs no ROM.
