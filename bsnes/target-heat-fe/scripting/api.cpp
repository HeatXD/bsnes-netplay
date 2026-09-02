#include "engine.hpp"

#include "../app.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

void LuaEngine::registerApi() {
  lua_pushlightuserdata(state, this);
  lua_setfield(state, LUA_REGISTRYINDEX, "heat-fe.lua");

  const struct {
    const char* name;
    int bytes;
    bool little;
    bool write;
    bool signedValue;
  } functions[] = {
      {"read_u8", 1, true, false, false},      {"read_u16_le", 2, true, false, false},
      {"read_u16_be", 2, false, false, false}, {"read_u24_le", 3, true, false, false},
      {"read_u24_be", 3, false, false, false}, {"read_u32_le", 4, true, false, false},
      {"read_u32_be", 4, false, false, false}, {"read_s8", 1, true, false, true},
      {"read_s16_le", 2, true, false, true},   {"read_s16_be", 2, false, false, true},
      {"read_s24_le", 3, true, false, true},   {"read_s24_be", 3, false, false, true},
      {"read_s32_le", 4, true, false, true},   {"read_s32_be", 4, false, false, true},
      {"write_u8", 1, true, true, false},      {"write_u16_le", 2, true, true, false},
      {"write_u16_be", 2, false, true, false}, {"write_u24_le", 3, true, true, false},
      {"write_u24_be", 3, false, true, false}, {"write_u32_le", 4, true, true, false},
      {"write_u32_be", 4, false, true, false}, {"write_s8", 1, true, true, true},
      {"write_s16_le", 2, true, true, true},   {"write_s16_be", 2, false, true, true},
      {"write_s24_le", 3, true, true, true},   {"write_s24_be", 3, false, true, true},
      {"write_s32_le", 4, true, true, true},   {"write_s32_be", 4, false, true, true},
  };

  auto addMemoryDomain = [&](EmuCore::MemoryDomain domain) {
    for(const auto& function : functions) {
      lua_pushinteger(state, function.bytes);
      lua_pushboolean(state, function.little);
      lua_pushinteger(state, (lua_Integer)domain);
      lua_pushboolean(state, function.signedValue);
      lua_pushcclosure(state, function.write ? writeMemory : readMemory, 4);
      lua_setfield(state, -2, function.name);
    }
    lua_pushinteger(state, (lua_Integer)domain);
    lua_pushcclosure(state, readBit, 1);
    lua_setfield(state, -2, "read_bit");
    lua_pushinteger(state, (lua_Integer)domain);
    lua_pushcclosure(state, writeBit, 1);
    lua_setfield(state, -2, "write_bit");
    lua_pushinteger(state, app.core.memorySize(domain));
    lua_setfield(state, -2, "size");
  };

  lua_newtable(state);
  addMemoryDomain(EmuCore::MemoryDomain::Bus);

  const struct {
    const char* name;
    EmuCore::MemoryDomain domain;
  } domains[] = {
      {"wram", EmuCore::MemoryDomain::WRAM},     {"vram", EmuCore::MemoryDomain::VRAM},
      {"cgram", EmuCore::MemoryDomain::CGRAM},   {"oam", EmuCore::MemoryDomain::OAM},
      {"apuram", EmuCore::MemoryDomain::APURAM},
  };

  for(const auto& domain : domains) {
    lua_newtable(state);
    addMemoryDomain(domain.domain);
    lua_setfield(state, -2, domain.name);
  }
  lua_setglobal(state, "memory");

  const auto addLibrary = [&](const char* name, const luaL_Reg* functions) {
    lua_newtable(state);
    luaL_setfuncs(state, functions, 0);
    lua_setglobal(state, name);
  };
  const luaL_Reg gui[] = {
      {"box", drawBox},      {"circle", drawCircle}, {"ellipse", drawEllipse}, {"line", drawLine},
      {"pixel", drawPixel},  {"text", drawText},     {"window", guiWindow},    {"label", guiLabel},
      {"button", guiButton}, {nullptr, nullptr},
  };
  const luaL_Reg input[] = {
      {"value", inputValue}, {"held", inputHeld},          {"set", inputSet},
      {"clear", inputClear}, {"clear_all", inputClearAll}, {nullptr, nullptr},
  };
  const luaL_Reg console[] = {{"log", consoleLog}, {"clear", consoleClear}, {nullptr, nullptr}};
  const luaL_Reg savestate[] = {
      {"save", saveState},     {"load", loadState},          {"exists", hasState},
      {"remove", removeState}, {"save_file", saveStateFile}, {"load_file", loadStateFile},
      {nullptr, nullptr},
  };
  const luaL_Reg emu[] = {
      {"loaded", emuLoaded}, {"paused", emuPaused}, {"pause", emuPause},
      {"resume", emuResume}, {"reset", emuReset},   {"power", emuPower},
      {"frame", emuFrame},   {"game", emuGame},     {nullptr, nullptr},
  };
  addLibrary("gui", gui);
  addLibrary("input", input);
  lua_pushcfunction(state, consoleLog);
  lua_setglobal(state, "print");
  addLibrary("console", console);
  addLibrary("savestate", savestate);
  addLibrary("emu", emu);

  lua_newtable(state);
  lua_pushcfunction(state, fileRead);
  lua_setfield(state, -2, "read");
  lua_pushboolean(state, false);
  lua_pushcclosure(state, fileWrite, 1);
  lua_setfield(state, -2, "write");
  lua_pushboolean(state, true);
  lua_pushcclosure(state, fileWrite, 1);
  lua_setfield(state, -2, "append");
  lua_pushcfunction(state, fileDirectory);
  lua_setfield(state, -2, "directory");
  lua_pushcfunction(state, fileExists);
  lua_setfield(state, -2, "exists");
  lua_pushcfunction(state, fileList);
  lua_setfield(state, -2, "list");
  lua_pushcfunction(state, fileRemove);
  lua_setfield(state, -2, "remove");
  lua_setglobal(state, "file");
}
