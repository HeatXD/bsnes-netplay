#include "app.hpp"

#include <algorithm>
#include <cstdint>

namespace {
constexpr uint8_t Signature1[] = {'B', 'S', 'V', '1'};
constexpr uint8_t Signature2[] = {'B', 'S', 'V', '2'};
constexpr size_t MovieHeaderSize = 9 + EmuCore::PortCount;
const SDL_DialogFileFilter MovieFilters[] = {
  {"bsnes movies", "bsv"},
  {"All files", "*"},
};

uint32_t read32(const uint8_t* data) {
  return (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16
       | (uint32_t)data[3] << 24;
}

void append32(std::vector<uint8_t>& data, uint32_t value) {
  for(int shift = 0; shift < 32; shift += 8) {
    data.push_back(value >> shift);
  }
}

void powerForMovie(EmuCore& core, int configuredEntropy) {
  core.setOption("Hacks/Entropy", "None");
  core.power();
  core.setOption("Hacks/Entropy", EntropyNames[configuredEntropy]);
}
}

void App::openMovieDialog() {
  openPick(movieOpenPick, MovieFilters, nullptr);
}

void App::beginMovieRecording(bool fromBeginning) {
  if(!core.loaded() || movieActive()) {
    return;
  }
  // a power cycle resets every peer's machine independently and desyncs the
  // session; recording from the state already agreed on with peers is fine
  if(fromBeginning && netplayActive()) {
    showMessage("cannot power cycle during netplay -- use Record from Current State");
    return;
  }
  if(scripting.running()) {
    showMessage("stop Lua scripting before recording a movie");
    return;
  }
  resetTimeline();
  movieState.clear();
  movieInput.clear();
  moviePosition = 0;
  for(int port = 0; port < EmuCore::PortCount; port++) {
    movieDevices[port] = core.connectedDevice(port);
  }
  movieDeterministic = netplayActive();
  if(fromBeginning) {
    powerForMovie(core, settings.hackEntropy);
  } else if(netplayActive()) {
    const std::vector<uint8_t> liveState = core.serialize(false);
    movieState = core.serialize(true);
    if(liveState.empty() || movieState.empty() || !core.unserialize(liveState)) {
      movieState.clear();
      showMessage("movie recording could not preserve the netplay state");
      return;
    }
  } else {
    movieState = core.serialize(true);
    if(movieState.empty()) {
      showMessage("movie recording could not capture the current state");
      return;
    }
  }
  movieMode = MovieMode::Recording;
  showMessage("movie recording started");
}

bool App::playMovieFile(const std::string& path) {
  if(!core.loaded() || movieActive() || netplayActive()) {
    return false;
  }
  if(scripting.running()) {
    showMessage("stop Lua scripting before playing a movie");
    return false;
  }
  const std::vector<uint8_t> file = readBytes(path);
  const bool version1 = file.size() >= 8
                     && std::equal(std::begin(Signature1), std::end(Signature1), file.begin());
  const bool version2 = file.size() >= MovieHeaderSize
                     && std::equal(std::begin(Signature2), std::end(Signature2), file.begin());
  if(!version1 && !version2) {
    showMessage("movie signature not supported");
    return false;
  }

  const size_t headerSize = version2 ? MovieHeaderSize : 8;
  movieDeterministic = false;
  const uint32_t stateSize = read32(file.data() + 4);
  if(stateSize > file.size() - headerSize || ((file.size() - headerSize - stateSize) & 1)) {
    showMessage("movie data is truncated");
    return false;
  }
  if(file.size() == headerSize + stateSize) {
    showMessage("movie contains no input");
    return false;
  }
  if(version2) {
    for(int port = 0; port < EmuCore::PortCount; port++) {
      const int device = file[8 + port];
      if(device < EmuCore::None || device >= EmuCore::DeviceCount) {
        showMessage("movie controller setup not supported");
        return false;
      }
    }
    for(int port = 0; port < EmuCore::PortCount; port++) {
      const int device = file[8 + port];
      moviePreviousDevices[port] = core.connectedDevice(port);
      movieDevices[port] = device;
      core.connect(port, device);
    }
    movieDevicesChanged = true;
    movieDeterministic = (file[8 + EmuCore::PortCount] & 1) != 0;
    if(movieDeterministic) netplayApplyDeterministicSettings();
  }
  std::vector<uint8_t> state(file.begin() + headerSize, file.begin() + headerSize + stateSize);
  if(state.empty()) {
    powerForMovie(core, settings.hackEntropy);
  } else {
    if(version2) core.power();  // refreshes serialization sizes for the movie's devices
    if(!core.unserialize(state)) {
      restoreMovieDevices();
      showMessage("movie state does not match this game");
      return false;
    }
  }

  movieInput.clear();
  for(size_t offset = headerSize + stateSize; offset < file.size(); offset += 2) {
    const uint16_t value = (uint16_t)file[offset] | (uint16_t)file[offset + 1] << 8;
    movieInput.push_back((int16_t)value);
  }
  resetTimeline();
  movieState = std::move(state);
  moviePosition = 0;
  movieMode = MovieMode::Playing;
  movieDevicesChanged = false;
  showMessage("movie playback started");
  return true;
}

bool App::writeMovieFile(std::string path) {
  if(path.empty()) {
    return false;
  }
  const bool hasExtension = path.size() >= 4
                         && SDL_strcasecmp(path.c_str() + path.size() - 4, ".bsv") == 0;
  if(!hasExtension) {
    path += ".bsv";
  }
  if(movieState.size() > UINT32_MAX) {
    return false;
  }

  std::vector<uint8_t> file;
  file.reserve(MovieHeaderSize + movieState.size() + movieInput.size() * 2);
  file.insert(file.end(), std::begin(Signature2), std::end(Signature2));
  append32(file, (uint32_t)movieState.size());
  for(int port = 0; port < EmuCore::PortCount; port++) file.push_back((uint8_t)movieDevices[port]);
  file.push_back(movieDeterministic ? 1 : 0);
  file.insert(file.end(), movieState.begin(), movieState.end());
  for(int16_t input : movieInput) {
    const uint16_t value = (uint16_t)input;
    file.push_back(value);
    file.push_back(value >> 8);
  }
  return writeBytes(path, file.data(), file.size());
}

void App::stopMovie() {
  if(movieMode == MovieMode::Playing) {
    clearMovie();
    showMessage("movie playback stopped");
    return;
  }
  if(movieMode != MovieMode::Recording || movieSavePending) {
    return;
  }

  movieMode = MovieMode::Inactive;
  movieSavePending = true;
  Guard guard(movieSavePick.mutex);
  movieSavePick.open = true;
  SDL_ShowSaveFileDialog(onPicked, &movieSavePick, shell.window, MovieFilters, 2, nullptr);
}

void App::clearMovie() {
  movieMode = MovieMode::Inactive;
  movieSavePending = false;
  movieState.clear();
  movieInput.clear();
  moviePosition = 0;
}

void App::restoreMovieDevices() {
  if(movieDevicesChanged) {
    for(int port = 0; port < EmuCore::PortCount; port++) core.connect(port, moviePreviousDevices[port]);
  }
  pushEnhancements();
  core.power();
  movieDevicesChanged = false;
}

int16_t App::pollMovieInput(int, int, int, int16_t physical) {
  // GekkoNet can run this frame repeatedly for rollback and presentation
  // runahead; only its single real timeline advance belongs in the movie
  if(netplayActive() && !netplay.recordInput) return physical;
  if(movieMode == MovieMode::Recording) {
    movieInput.push_back(physical);
    return physical;
  }
  if(movieMode != MovieMode::Playing) {
    return physical;
  }
  if(moviePosition >= movieInput.size()) {
    clearMovie();
    showMessage("movie playback finished");
    return physical;
  }
  const int16_t value = movieInput[moviePosition++];
  if(moviePosition == movieInput.size()) {
    clearMovie();
    showMessage("movie playback finished");
  }
  return value;
}
