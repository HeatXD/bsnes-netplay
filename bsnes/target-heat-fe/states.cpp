// Quick save states: nine slots plus undo, redo and auto-resume.

#include "app.hpp"

#include <algorithm>
#include <cstring>

namespace {
// "HFS" and a format version, so a state from a future layout is refused
constexpr uint8_t Magic[4] = {'H', 'F', 'S', '1'};
constexpr size_t HeaderSize = 8;  // magic, then the payload length

// the slots that are not numbered, and where each lives; null means a file
constexpr struct { const char* name; const char* label; std::vector<uint8_t> App::* held; }
SpecialSlots[] = {
  {"undo", "undo state",         &App::undoState},
  {"redo", "redo state",         &App::redoState},
  {"auto", "auto-resume state",  nullptr},
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

bool writeStatePayload(const std::string& path, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> file(HeaderSize + payload.size());
  std::memcpy(file.data(), Magic, sizeof(Magic));
  writeLE32(file.data() + 4, (uint32_t)payload.size());
  std::memcpy(file.data() + HeaderSize, payload.data(), payload.size());
  return ensureDir(parentDir(path)) && writeBytes(path, file.data(), file.size());
}

bool readStatePayload(const std::string& path, std::vector<uint8_t>& payload) {
  payload = readBytes(path);
  if(payload.size() <= HeaderSize || std::memcmp(payload.data(), Magic, sizeof(Magic)) != 0
  || readLE32(payload.data() + 4) != payload.size() - HeaderSize) return false;
  payload.erase(payload.begin(), payload.begin() + HeaderSize);
  return true;
}
}  // namespace

std::string App::statesDir() const {
  return settings.statesDir.empty() ? configDir() + "States" : settings.statesDir;
}

// per game rather than one flat folder, so nine slots do not bury the rest
std::string App::stateFolder() const {
  return statesDir() + "/" + gameTitle;
}

// a memory slot has a path too, but only so the legacy file can be cleaned up
std::string App::statePath(const std::string& name) const {
  return stateFolder() + "/" + name + ".bst";
}

std::string App::slotName(int slot) { return std::to_string(slot); }

std::string App::stateLabel(const std::string& name) {
  for(const auto& special : SpecialSlots) {
    if(name == special.name) return special.label;
  }
  const std::string managed = "Managed/";
  if(name.compare(0, managed.size(), managed) == 0) return name.substr(managed.size());
  return "state " + name;
}

std::vector<uint8_t>* App::memorySlot(const std::string& name) {
  for(const auto& special : SpecialSlots) {
    if(name == special.name && special.held) return &(this->*special.held);
  }
  return nullptr;
}

const std::vector<uint8_t>* App::memorySlot(const std::string& name) const {
  return const_cast<App*>(this)->memorySlot(name);
}

int64_t App::stateTime(const std::string& name) const {
  if(!core.loaded() || memorySlot(name)) return 0;
  return fileTime(statePath(name));
}

bool App::hasState(const std::string& name) const {
  if(const std::vector<uint8_t>* held = memorySlot(name)) return !held->empty();
  return stateTime(name) != 0;
}

// quiet leaves the status line to the user's own action
bool App::saveState(const std::string& name, bool quiet) {
  if(!core.loaded()) return false;

  std::vector<uint8_t> payload = core.serialize();
  if(payload.empty()) {
    if(!quiet) showMessage("could not capture " + stateLabel(name));
    return false;
  }

  if(std::vector<uint8_t>* held = memorySlot(name)) {
    *held = std::move(payload);
    if(!quiet) showMessage("saved " + stateLabel(name));
    return true;
  }

  if(!writeStatePayload(statePath(name), payload)) {
    if(!quiet) showMessage("could not write " + stateLabel(name));
    return false;
  }

  if(!quiet) showMessage("saved " + stateLabel(name));
  return true;
}

bool App::saveStateFile(const std::string& path, bool quiet) {
  if(!core.loaded()) return false;
  const std::vector<uint8_t> payload = core.serialize();
  const bool saved = !payload.empty() && writeStatePayload(path, payload);
  if(!quiet) showMessage(saved ? "saved state file " + fileName(path)
                               : "could not save state file " + fileName(path));
  return saved;
}

bool App::loadState(const std::string& name) {
  if(!core.loaded()) return false;
  if(movieActive()) {
    showMessage("stop the movie before loading a state");
    return false;
  }

  std::vector<uint8_t> file;  // only a disk slot fills this
  const std::vector<uint8_t>* payload = memorySlot(name);

  if(!payload) {
    file = readBytes(statePath(name));
    if(!file.empty()) {
      if(file.size() <= HeaderSize || std::memcmp(file.data(), Magic, sizeof(Magic)) != 0
      || readLE32(file.data() + 4) != file.size() - HeaderSize) {
        showMessage(stateLabel(name) + " is not a state this build can read");
        return false;
      }
      // the header is dropped in place rather than copied out
      file.erase(file.begin(), file.begin() + HeaderSize);
    }
    payload = &file;
  }

  if(payload->empty()) {
    showMessage(stateLabel(name) + " is empty");
    return false;
  }

  // undo holds what is being replaced, redo what undoing replaced; either way
  // the snapshot targets the other buffer, so it never disturbs payload
  saveState(name == "undo" ? "redo" : "undo", true);

  if(!core.unserialize(*payload)) {
    showMessage(stateLabel(name) + " does not match this game");
    return false;
  }

  resetTimeline();
  paused = false;
  showMessage("loaded " + stateLabel(name));
  return true;
}

bool App::loadStateFile(const std::string& path) {
  if(!core.loaded()) return false;
  if(movieActive()) {
    showMessage("stop the movie before loading a state");
    return false;
  }
  std::vector<uint8_t> payload;
  if(!readStatePayload(path, payload)) {
    showMessage("could not read state file " + fileName(path));
    return false;
  }

  saveState("undo", true);
  if(!core.unserialize(payload)) {
    showMessage("state file " + fileName(path) + " does not match this game");
    return false;
  }

  resetTimeline();
  paused = false;
  showMessage("loaded state file " + fileName(path));
  return true;
}

bool App::removeState(const std::string& name) {
  if(!core.loaded()) return false;
  if(std::vector<uint8_t>* held = memorySlot(name)) {
    const bool had = !held->empty();
    held->clear();
    return had;
  }
  if(!hasState(name)) return false;
  return SDL_RemovePath(statePath(name).c_str());
}

bool App::renameState(const std::string& from, const std::string& to) {
  if(!core.loaded() || !hasState(from) || hasState(to)) return false;
  return ensureDir(parentDir(statePath(to)))
      && SDL_RenamePath(statePath(from).c_str(), statePath(to).c_str());
}

std::vector<StateEntry> App::availableStates(bool managed) const {
  std::vector<StateEntry> states;
  if(!core.loaded()) return states;
  if(!managed) {
    for(int slot = 1; slot <= StateSlots; slot++) {
      const std::string name = slotName(slot);
      states.push_back({name, "Slot " + name, stateTime(name)});
    }
    return states;
  }

  const std::string folder = stateFolder() + "/Managed";
  int count = 0;
  char** names = SDL_GlobDirectory(folder.c_str(), "*.bst", SDL_GLOB_CASEINSENSITIVE, &count);
  for(int i = 0; names && i < count; i++) {
    const std::string file = names[i];
    const std::string label = fileStem(file);
    const std::string name = "Managed/" + label;
    states.push_back({name, label, stateTime(name)});
  }
  if(names) SDL_free(names);
  std::sort(states.begin(), states.end(), [](const StateEntry& a, const StateEntry& b) {
    return SDL_strcasecmp(a.label.c_str(), b.label.c_str()) < 0;
  });
  return states;
}

void App::removeAllStates() {
  for(int slot = 1; slot <= StateSlots; slot++) removeState(slotName(slot));
  for(const StateEntry& state : availableStates(true)) removeState(state.name);
  SDL_RemovePath((stateFolder() + "/Managed").c_str());
  undoState.clear();
  redoState.clear();
  // the folder is removed by name, so every file any build ever wrote must be
  // listed here -- including the undo.bst and redo.bst of older ones
  for(const auto& special : SpecialSlots) SDL_RemovePath(statePath(special.name).c_str());
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
