#pragma once

// The nall side of the firewall. Nothing outside emucore/ may include it.

#include "emucore.hpp"

#include <emulator/emulator.hpp>
#include <sfc/interface/interface.hpp>
#include <nall/directory.hpp>
#include <nall/decode/zip.hpp>
using namespace nall;

#include <array>
#include <string>
#include <vector>

// SuperFamicom::ID, which the frontend never sees
constexpr uint SystemID = 0;
constexpr uint SuperFamicomID = 1;
constexpr uint GameBoyID = 2;
constexpr uint BSMemoryID = 3;
constexpr uint SufamiTurboAID = 4;
constexpr uint SufamiTurboBID = 5;

// held in memory so an archive, a loose ROM and a pak folder all look alike
struct Media {
  string location;  // a trailing '/' marks a pak folder rather than a file
  string manifest;
  vector<uint8_t> program, data, expansion, firmware;
  bool verified = false;  // found in the games database by sha256
  bool patched = false;   // a .bps or .ips was applied on load

  explicit operator bool() const { return (bool)location; }

  auto pak() const -> bool { return location.endsWith("/"); }

  auto name() const -> string { return Location::prefix(location); }
};

struct EmuCore::Impl : Emulator::Platform {
  EmuCore& owner;
  Emulator::Interface* emulator = nullptr;

  Media superFamicom, gameBoy, bsMemory, sufamiTurboA, sufamiTurboB;

  auto allMedia() -> std::array<Media*, 5> {
    return {&superFamicom, &gameBoy, &bsMemory, &sufamiTurboA, &sufamiTurboB};
  }

  Markup::Node sfcDocument;  // the base cart's manifest, parsed for firmware ids

  // heuristics summary of the base cartridge, for the Manifest window
  struct {
    string title;        // the file or folder name, used for the window title
    string headerTitle;  // the cartridge header's own title, which hotfixes match
    string region;
    string board;
    string checksum;
    uint romSize = 0;
    uint ramSize = 0;
  } info;

  string error;
  string missing;     // required files the last load could not find
  string patchError;  // a patch was found but could not be applied
  // rebuilt on load, not per call: the windows reading these ask every frame
  std::vector<EmuCore::SlotInfo> slotCache;
  std::vector<EmuCore::ManifestInfo> manifestCache;

  // cached hack settings, applied on load/power/reset since several only
  // take effect then; PPU/DSP fast mode may be overridden by a hotfix
  struct {
    bool ppuFast = true;
    bool ppuNoSpriteLimit = false;
    uint mode7Scale = 1;
    bool dspFast = true;
    bool dspCubic = false;
    bool coprocessorDelayedSync = true;
    bool coprocessorPreferHLE = false;
    bool hotfixes = true;
  } hacks;

  string activeHotfix;
  // frame skip as asked for, and whether the PPU that honours it is really on
  int frameSkip = 0;
  bool ppuFastActive = true;

  std::string savesDir;  // frontend override; empty means next to the ROM
  std::string firmwareDir;
  std::string databaseDir;
  std::string patchesDir;
  bool ipsHeadered = false;

  std::array<uint32_t, 32768> palette{};
  std::vector<uint32_t> videoOut;       // final cropped frame, tightly packed
  std::vector<uint32_t> filterScratch;  // pre-crop filter output, resized per frame
  std::vector<float> audioOut;

  bool overscanCrop = true;
  int videoGamma = 150, videoLuminance = 100, videoSaturation = 100;
  int filterIndex = 0;  // index into video.cpp's Filters[]
  std::array<std::array<int16_t, EmuCore::MaxInputs>, EmuCore::PortCount> state{};
  std::array<int, EmuCore::PortCount> connected{EmuCore::Gamepad, EmuCore::Gamepad, EmuCore::None};

  // the core rebuilds these as fresh nall vectors on every call
  std::array<std::vector<EmuCore::DeviceInfo>, EmuCore::PortCount> deviceCache;
  std::array<std::vector<EmuCore::InputInfo>, EmuCore::DeviceCount> inputCache;

  Impl(EmuCore& owner);
  ~Impl();

  // video.cpp
  auto buildPalette() -> void;

  // core.cpp
  //  recomputes per-title overrides and pushes all cached hacks to the core
  auto applyHacks() -> void;

  // vfs.cpp
  auto media(uint id) -> Media*;
  auto openMedia(uint id, string name, vfs::file::mode mode) -> shared_pointer<vfs::file>;
  auto openFirmware(const string& name, vfs::file::mode mode) -> shared_pointer<vfs::file>;
  auto savePath(const Media& media, const string& name) -> string;
  auto openSave(const Media& media, const string& name, vfs::file::mode mode)
      -> shared_pointer<vfs::file>;
  auto openSuperFamicom(string name, vfs::file::mode mode) -> shared_pointer<vfs::file>;

  // media.cpp
  auto loadSuperFamicom(const string& location) -> bool;
  template <typename Heuristic>
  auto loadCart(Media& slot, const string& location, uint minSize, const vector<string>& pakFiles,
                const vector<string>& databases) -> bool;
  // replaces a heuristic manifest with the database's when the hash matches
  auto lookupDatabase(Media& slot, const vector<string>& databases, const string& sha256,
                      const string& headerTitle = {}) -> void;
  auto applyPatches(Media& slot, vector<uint8_t>& rom, const string& location) -> void;

  // Emulator::Platform
  auto open(uint id, string name, vfs::file::mode mode, bool required)
      -> shared_pointer<vfs::file> override;
  auto load(uint id, string name, string type, vector<string> options)
      -> Emulator::Platform::Load override;
  auto videoFrame(const uint16* data, uint pitch, uint width, uint height, uint scale)
      -> void override;
  auto audioFrame(const double* samples, uint channels) -> void override;
  auto inputPoll(uint port, uint device, uint input) -> int16 override;
};
