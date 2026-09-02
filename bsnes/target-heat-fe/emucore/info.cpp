// The heuristics summary the Cartridge window reads.

#include "impl.hpp"

namespace {
// the NEC board names no variant, so heuristics' firmwareNEC() picks it off
// the cartridge title; mirror it or the field contradicts the manifest
auto necVariant(const string& title) -> string {
  if(title == "PILOTWINGS") { return "DSP-1"; }
  if(title == "DUNGEON MASTER") { return "DSP-2"; }
  if(title == "SDガンダムGX") { return "DSP-3"; }
  if(title == "PLANETS CHAMP TG3000") { return "DSP-4"; }
  if(title == "TOP GEAR 3000") { return "DSP-4"; }
  return "DSP-1B";
}

// board() encodes the expansion chip as its first "-"-separated component
auto chipName(string board, const string& title) -> string {
  auto first = board.trimRight("#A", 1L).split("-")(0);
  if(first == "ARM") { return "ST018 (ARM6)"; }
  if(first == "NEC") { return necVariant(title); }
  if(first == "EXNEC") { return "ST010/ST011"; }
  if(first == "GSU") { return "SuperFX"; }
  if(first == "HITACHI") { return "Cx4"; }
  if(first == "SA1") { return "SA-1"; }
  if(first == "SDD1") { return "S-DD1"; }
  if(first == "SPC7110" || first == "EXSPC7110") { return "SPC7110"; }
  if(first == "BS") { return "BS-X"; }
  if(first == "OBC1") { return "OBC1"; }
  if(first == "GB") { return "Super Game Boy"; }
  return "None";
}

auto sizeText(uint bytes) -> string {
  if(!bytes) { return "None"; }
  if(bytes >= 1024 * 1024) { return {bytes / (1024 * 1024), " MiB"}; }
  return {bytes / 1024, " KiB"};
}
}  // namespace

std::string EmuCore::title() const { return (const char*)impl->info.title; }

std::string EmuCore::headerTitle() const { return (const char*)impl->info.headerTitle; }

std::string EmuCore::manifest() const { return (const char*)impl->superFamicom.manifest; }

std::string EmuCore::region() const { return (const char*)impl->info.region; }

std::string EmuCore::board() const { return (const char*)impl->info.board; }

std::string EmuCore::romSizeText() const { return (const char*)sizeText(impl->info.romSize); }

std::string EmuCore::ramSizeText() const { return (const char*)sizeText(impl->info.ramSize); }

std::string EmuCore::expansionChip() const {
  return (const char*)chipName(impl->info.board, impl->info.headerTitle);
}

std::string EmuCore::checksum() const { return (const char*)impl->info.checksum; }

std::vector<std::string> EmuCore::hashes() const {
  std::vector<std::string> out;
  if(!loaded()) { return out; }
  for(const string& hash : impl->emulator->hashes()) { out.emplace_back((const char*)hash); }
  return out;
}

const std::vector<EmuCore::SlotInfo>& EmuCore::slots() const { return impl->slotCache; }

const std::vector<EmuCore::ManifestInfo>& EmuCore::manifestList() const {
  return impl->manifestCache;
}
