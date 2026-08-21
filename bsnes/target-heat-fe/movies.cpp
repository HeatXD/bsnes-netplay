#include "app.hpp"

#include <algorithm>
#include <cstdint>

namespace {
constexpr uint8_t Signature[] = {'B', 'S', 'V', '1'};
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
}

void App::openMovieDialog() {
  openPick(movieOpenPick, MovieFilters, nullptr);
}

void App::beginMovieRecording(bool fromBeginning) {
  if(!core.loaded() || movieActive()) {
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
  if(fromBeginning) {
    core.setOption("Hacks/Entropy", "None");
    core.power();
  } else {
    movieState = core.serialize();
    if(movieState.empty()) {
      showMessage("movie recording could not capture the current state");
      return;
    }
  }
  movieMode = MovieMode::Recording;
  showMessage("movie recording started");
}

bool App::playMovieFile(const std::string& path) {
  if(!core.loaded() || movieActive()) {
    return false;
  }
  if(scripting.running()) {
    showMessage("stop Lua scripting before playing a movie");
    return false;
  }
  const std::vector<uint8_t> file = readBytes(path);
  if(file.size() < 8 || !std::equal(std::begin(Signature), std::end(Signature), file.begin())) {
    showMessage("movie format not supported");
    return false;
  }

  const uint32_t stateSize = read32(file.data() + 4);
  if(stateSize > file.size() - 8 || ((file.size() - 8 - stateSize) & 1)) {
    showMessage("movie format not supported");
    return false;
  }
  if(file.size() == 8 + stateSize) {
    showMessage("movie contains no input");
    return false;
  }
  std::vector<uint8_t> state(file.begin() + 8, file.begin() + 8 + stateSize);
  if(state.empty()) {
    core.setOption("Hacks/Entropy", "None");
    core.power();
  } else if(!core.unserialize(state)) {
    showMessage("movie state does not match this game");
    return false;
  }

  movieInput.clear();
  for(size_t offset = 8 + stateSize; offset < file.size(); offset += 2) {
    const uint16_t value = (uint16_t)file[offset] | (uint16_t)file[offset + 1] << 8;
    movieInput.push_back((int16_t)value);
  }
  resetTimeline();
  movieState = std::move(state);
  moviePosition = 0;
  movieMode = MovieMode::Playing;
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
  file.reserve(8 + movieState.size() + movieInput.size() * 2);
  file.insert(file.end(), std::begin(Signature), std::end(Signature));
  append32(file, (uint32_t)movieState.size());
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

int16_t App::pollMovieInput(int, int, int, int16_t physical) {
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
