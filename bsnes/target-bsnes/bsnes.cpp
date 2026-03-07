#include "bsnes.hpp"
#include <sfc/interface/interface.hpp>
Video video;
Audio audio;
Input input;
unique_pointer<Emulator::Interface> emulator;

auto locate(string name) -> string {
  string location = {Path::program(), name};
  if(inode::exists(location)) return location;

  location = {Path::userData(), "bsnes/", name};
  if(inode::exists(location)) return location;

  directory::create({Path::userSettings(), "bsnes/"});
  return {Path::userSettings(), "bsnes/", name};
}

#include <nall/main.hpp>
auto nall::main(Arguments arguments) -> void {
  settings.location = locate("settings.bml");

  vector<string> args;
  for(auto argument : arguments) args.append(argument);

  for(uint i = 0; i < args.size(); i++) {
    auto argument = args[i];
    if(argument == "--fullscreen") {
      program.startFullScreen = true;
    } else if(argument.beginsWith("--locale=")) {
      Application::locale().scan(locate("Locale/"));
      Application::locale().select(argument.trimLeft("--locale=", 1L));
    } else if(argument.beginsWith("--settings=")) {
      settings.location = argument.trimLeft("--settings=", 1L);
    } else if(argument == "--wl-udp-port" && i + 1 < args.size()) {
      program.wlArgs.port = (uint16_t)args[++i].natural();
    } else if(argument == "--wl-player-id" && i + 1 < args.size()) {
      program.wlArgs.playerId = (uint8_t)args[++i].natural();
    } else if(argument == "--wl-config" && i + 1 < args.size()) {
      program.wlArgs.configJson = args[++i];
      program.wlArgs.active = true;
      try {
        auto j = nlohmann::json::parse(program.wlArgs.configJson.data());
        string folder = j.at("gamesFolder").get<std::string>().c_str();
        string game   = j.at("game").get<std::string>().c_str();
        string path   = {folder, "/", game};
        if(inode::exists(path)) program.gameQueue.append({"Auto;", path});
      } catch(...) {}
    } else if(inode::exists(argument)) {
      //game without option
      program.gameQueue.append({"Auto;", argument});
    } else if(argument.find(";")) {
      //game with option
      auto game = argument.split(";", 1L);
      if(inode::exists(game.last())) program.gameQueue.append(argument);
    }
  }

  settings.load();
  Application::setName("bsnes");
  Application::setToolTips(settings.general.toolTips);

  Instances::presentation.construct();
  Instances::settingsWindow.construct();
  Instances::cheatDatabase.construct();
  Instances::cheatWindow.construct();
  Instances::stateWindow.construct();
  Instances::toolsWindow.construct();
  Instances::netplayWindow.construct();
  emulator = new SuperFamicom::Interface;
  program.create();

  Application::run();
  Instances::presentation.destruct();
  Instances::settingsWindow.destruct();
  Instances::cheatDatabase.destruct();
  Instances::cheatWindow.destruct();
  Instances::stateWindow.destruct();
  Instances::toolsWindow.destruct();
  Instances::netplayWindow.destruct();
}
