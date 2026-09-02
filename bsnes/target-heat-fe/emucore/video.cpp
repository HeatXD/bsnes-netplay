// Palette, CPU filters, and the frame the core hands up each vblank.

#include "impl.hpp"

#include <filter/filter.hpp>

#include <cmath>

namespace {
struct FilterEntry {
  const char* name;
  Filter::Size size;
  Filter::Render render;
  // input dimensions the filter was written for; 0 means always eligible.
  // frames outside this (hires, HD mode 7) fall back to None
  uint maxWidth, maxHeight;
};

constexpr FilterEntry Filters[] = {
    {"None", &Filter::None::size, &Filter::None::render, 0, 0},
    {"Scanlines (Light)", &Filter::ScanlinesLight::size, &Filter::ScanlinesLight::render, 512, 240},
    {"Scanlines (Dark)", &Filter::ScanlinesDark::size, &Filter::ScanlinesDark::render, 512, 240},
    {"Scanlines (Black)", &Filter::ScanlinesBlack::size, &Filter::ScanlinesBlack::render, 512, 240},
    {"Pixellate 2x", &Filter::Pixellate2x::size, &Filter::Pixellate2x::render, 512, 480},
    {"Scale2x", &Filter::Scale2x::size, &Filter::Scale2x::render, 256, 240},
    {"2xSaI", &Filter::_2xSaI::size, &Filter::_2xSaI::render, 256, 240},
    {"Super 2xSaI", &Filter::Super2xSaI::size, &Filter::Super2xSaI::render, 256, 240},
    {"Super Eagle", &Filter::SuperEagle::size, &Filter::SuperEagle::render, 256, 240},
    {"LQ2x", &Filter::LQ2x::size, &Filter::LQ2x::render, 256, 240},
    {"HQ2x", &Filter::HQ2x::size, &Filter::HQ2x::render, 256, 240},
    {"NTSC (RF)", &Filter::NTSC_RF::size, &Filter::NTSC_RF::render, 512, 480},
    {"NTSC (Composite)", &Filter::NTSC_Composite::size, &Filter::NTSC_Composite::render, 512, 480},
    {"NTSC (S-Video)", &Filter::NTSC_SVideo::size, &Filter::NTSC_SVideo::render, 512, 480},
    {"NTSC (RGB)", &Filter::NTSC_RGB::size, &Filter::NTSC_RGB::render, 512, 480},
};
constexpr int FilterCount = sizeof(Filters) / sizeof(Filters[0]);
}  // namespace

// saturation mixes channels via a per-pixel grayscale, then gamma, then
// luminance
auto EmuCore::Impl::buildPalette() -> void {
  const double gamma = videoGamma / 100.0;
  const double luminance = videoLuminance / 100.0;
  const double saturation = videoSaturation / 100.0;
  auto clamp16 = [](double v) -> uint16 { return v > 65535.0 ? uint16(65535) : uint16(v); };

  if(saturation == 1.0) {
    // fast path: with saturation pinned at 1.0 each channel only depends on
    // its own 5-bit value, so a 32-entry ramp stands in for all 32768 entries.
    uint32_t ramp[32];
    for(uint level : range(32)) {
      uint16 value = level << 3 | level >> 2;
      value = value << 8 | value;
      if(value <= 32767) { value = uint16(32767 * pow(value / 32767.0, gamma)); }
      if(luminance != 1.0) { value = clamp16(value * luminance); }
      ramp[level] = value >> 8;
    }
    for(uint color : range(32768)) {
      palette[color] = 0xff000000 | ramp[(color >> 10) & 31] << 16 | ramp[(color >> 5) & 31] << 8 |
                       ramp[(color >> 0) & 31];
    }
    return;
  }

  // slow path: saturation mixes channels, so the ramp shortcut no longer
  // applies and all 32768 entries need computing individually.
  for(uint color : range(32768)) {
    uint16 r = (color >> 10) & 31;
    uint16 g = (color >> 5) & 31;
    uint16 b = (color >> 0) & 31;
    r = r << 3 | r >> 2;
    r = r << 8 | r;
    g = g << 3 | g >> 2;
    g = g << 8 | g;
    b = b << 3 | b >> 2;
    b = b << 8 | b;

    uint16 grayscale = clamp16((r + g + b) / 3.0);
    double inverse = 1.0 - saturation > 0.0 ? 1.0 - saturation : 0.0;
    r = clamp16(r * saturation + grayscale * inverse);
    g = clamp16(g * saturation + grayscale * inverse);
    b = clamp16(b * saturation + grayscale * inverse);

    if(gamma != 1.0) {
      if(r <= 32767) { r = uint16(32767 * pow(r / 32767.0, gamma)); }
      if(g <= 32767) { g = uint16(32767 * pow(g / 32767.0, gamma)); }
      if(b <= 32767) { b = uint16(32767 * pow(b / 32767.0, gamma)); }
    }

    if(luminance != 1.0) {
      r = clamp16(r * luminance);
      g = clamp16(g * luminance);
      b = clamp16(b * luminance);
    }

    palette[color] = 0xff000000 | (r >> 8) << 16 | (g >> 8) << 8 | (b >> 8);
  }
}

auto EmuCore::Impl::videoFrame(const uint16* data, uint pitch, uint width, uint height, uint scale)
    -> void {
  if(!owner.onVideo) { return; }

  // crop before filtering, so the filter sees exactly the
  // visible picture and its edge rows are the ones actually shown
  if(overscanCrop) {
    uint multiplier = height / 240;
    data += 8 * (pitch >> 1) * multiplier;
    height -= 16 * multiplier;
  }

  // HD mode 7 and hires/interlaced frames outside a filter's working size
  // fall back to the identity filter
  const FilterEntry* filter = &Filters[filterIndex];
  bool eligible =
      scale == 1 && filter->maxWidth && width <= filter->maxWidth && height <= filter->maxHeight;
  if(filterIndex != 0 && !eligible) { filter = &Filters[0]; }

  uint filterWidth = width, filterHeight = height;
  filter->size(filterWidth, filterHeight);

  filterScratch.resize((size_t)filterWidth * filterHeight);
  filter->render(palette.data(), filterScratch.data(), filterWidth * (uint)sizeof(uint32_t), data,
                 pitch, width, height);

  const uint32_t* src = filterScratch.data();
  const uint outWidth = filterWidth, outHeight = filterHeight;

  // HD mode 7 outruns the filters' worst case, so grow instead of clamping;
  // a clamp here would hand the frontend the frame's top-left corner
  if(videoOut.size() < (size_t)outWidth * outHeight) {
    videoOut.resize((size_t)outWidth * outHeight);
  }

  for(uint y : range(outHeight)) {
    memory::copy(videoOut.data() + y * outWidth, src + y * filterWidth,
                 outWidth * sizeof(uint32_t));
  }

  owner.onVideo(videoOut.data(), (int)outWidth, (int)outHeight);
}

void EmuCore::setOverscanCrop(bool crop) { impl->overscanCrop = crop; }

void EmuCore::setPaletteAdjust(int gammaPercent, int luminancePercent, int saturationPercent) {
  impl->videoGamma = gammaPercent;
  impl->videoLuminance = luminancePercent;
  impl->videoSaturation = saturationPercent;
  impl->buildPalette();
}

void EmuCore::setFilter(const std::string& name) {
  for(int i = 0; i < FilterCount; i++) {
    if(name == Filters[i].name) {
      impl->filterIndex = i;
      return;
    }
  }
  impl->filterIndex = 0;
}

std::vector<std::string> EmuCore::filterNames() const {
  std::vector<std::string> names;
  for(int i = 0; i < FilterCount; i++) { names.push_back(Filters[i].name); }
  return names;
}
