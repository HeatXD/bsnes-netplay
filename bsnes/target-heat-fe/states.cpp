// Quick save states: nine slots plus undo, redo and auto-resume.

#include "app.hpp"

#include <cstring>

namespace {
// "HFS" and a format version, so a state from a future layout is refused
constexpr uint8_t Magic[4] = {'H', 'F', 'S', '1'};
constexpr size_t HeaderSize = 8;  // magic, then the payload length

// the slots that are not numbered; anything walking every state walks these too
constexpr struct { const char* name; const char* label; } SpecialSlots[] = {
  {"undo", "undo state"}, {"redo", "redo state"}, {"auto", "auto-resume state"},
};

void writeLE32(uint8_t* out, uint32_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
  out[2] = (uint8_t)(value >> 16);
  out[3] = (uint8_t)(value >> 24);
}

uint32_t readLE32(const uint8_t* in) {
  return (uint32_t)in[0] | (uint32_t)in[1] << 8 | (uint32_t)in[2] << 16 | (uint32_t)in[3] << 24;
}
}  // namespace

std::string App::statesDir() const {
  return settings.statesDir.empty() ? configDir() + "States" : settings.statesDir;
}

// per game rather than one flat folder, so nine slots do not bury the rest
std::string App::stateFolder() const {
  return statesDir() + "/" + gameTitle;
}

std::string App::statePath(const std::string& name) const {
  return stateFolder() + "/" + name + ".bst";
}

std::string App::slotName(int slot) { return std::to_string(slot); }

std::string App::stateLabel(const std::string& name) {
  for(const auto& special : SpecialSlots) {
    if(name == special.name) return special.label;
  }
  return "state " + name;
}

int64_t App::stateTime(const std::string& name) const {
  if(!core.loaded()) return 0;
  return fileTime(statePath(name));
}

bool App::hasState(const std::string& name) const { return stateTime(name) != 0; }

// quiet leaves the status line to the user's own action
bool App::saveState(const std::string& name, bool quiet) {
  if(!core.loaded()) return false;

  const std::vector<uint8_t> payload = core.serialize();
  if(payload.empty()) {
    if(!quiet) showMessage("could not capture " + stateLabel(name));
    return false;
  }

  std::vector<uint8_t> file(HeaderSize + payload.size());
  std::memcpy(file.data(), Magic, sizeof(Magic));
  writeLE32(file.data() + 4, (uint32_t)payload.size());
  std::memcpy(file.data() + HeaderSize, payload.data(), payload.size());

  if(!ensureDir(statesDir()) || !ensureDir(stateFolder())
  || !writeBytes(statePath(name), file.data(), file.size())) {
    if(!quiet) showMessage("could not write " + stateLabel(name));
    return false;
  }

  if(!quiet) showMessage("saved " + stateLabel(name));
  return true;
}

bool App::loadState(const std::string& name) {
  if(!core.loaded()) return false;

  const std::vector<uint8_t> file = readBytes(statePath(name));
  if(file.empty()) {
    showMessage(stateLabel(name) + " is empty");
    return false;
  }
  if(file.size() <= HeaderSize || std::memcmp(file.data(), Magic, sizeof(Magic)) != 0
  || readLE32(file.data() + 4) != file.size() - HeaderSize) {
    showMessage(stateLabel(name) + " is not a state this build can read");
    return false;
  }

  // undo goes back to what is being replaced; undoing itself feeds redo
  if(name != "undo") saveState("undo", true);
  else saveState("redo", true);

  const std::vector<uint8_t> payload(file.begin() + HeaderSize, file.end());
  if(!core.unserialize(payload)) {
    showMessage(stateLabel(name) + " does not match this game");
    return false;
  }

  paused = false;
  showMessage("loaded " + stateLabel(name));
  return true;
}

bool App::removeState(const std::string& name) {
  if(!core.loaded() || !hasState(name)) return false;
  return SDL_RemovePath(statePath(name).c_str());
}

void App::removeAllStates() {
  for(int slot = 1; slot <= StateSlots; slot++) removeState(slotName(slot));
  for(const auto& special : SpecialSlots) removeState(special.name);
  // the folder goes too when nothing is left in it
  SDL_RemovePath(stateFolder().c_str());
  showMessage("removed every state for " + gameTitle);
}

void App::setStateSlot(int slot) {
  // the slots wrap, so one hotkey held down walks the whole set
  stateSlot = slot < 1 ? StateSlots : slot > StateSlots ? 1 : slot;
  const bool filled = hasState(slotName(stateSlot));
  showMessage("state slot " + slotName(stateSlot) + (filled ? "" : " (empty)"));
}
