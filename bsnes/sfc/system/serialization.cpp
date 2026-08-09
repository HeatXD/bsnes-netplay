auto System::serialize(bool synchronize) -> serializer {
  //deterministic serialization (synchronize=false) is only possible with select libco methods
  if(!co_serializable()) synchronize = true;

  if(!information.serializeSize[synchronize]) return {};  //should never occur
  if(synchronize) runToSave();

  serializer s(information.serializeSize[synchronize]);
  serializeHeader(s, synchronize);
  serializeAll(s, synchronize);
  return s;
}

auto System::unserialize(serializer& s) -> bool {
  uint signature = 0;
  uint serializeSize = 0;
  char version[16] = {};
  char description[512] = {};
  bool synchronize = false;
  bool fastPPU = false;

  s.integer(signature);
  s.integer(serializeSize);
  s.array(version);
  s.array(description);
  s.boolean(synchronize);
  s.boolean(fastPPU);

  if(signature != 0x31545342) return false;
  if(serializeSize != information.serializeSize[synchronize]) return false;
  if(string{version} != Emulator::SerializerVersion) return false;
  if(fastPPU != hacks.fastPPU) return false;

  if(synchronize) power(/* reset = */ false);
  serializeAll(s, synchronize);
  return true;
}

//internal

auto System::serializeAll(serializer& s, bool synchronize, vector<Emulator::SerializeComponent>* map) -> void {
  bool hostState = false;

  //records the range fn() wrote as a named component; transparent without a map
  auto mark = [&](const string& name, auto&& fn) {
    if(!map) return fn();
    uint start = s.size();
    fn();
    map->append({name, start, s.size() - start, hostState});
  };

  mark("random", [&] { random.serialize(s); });
  mark("cartridge", [&] { cartridge.serialize(s); });
  mark("cpu", [&] { cpu.serialize(s); });
  mark("smp", [&] { smp.serialize(s); });
  mark("ppu", [&] { ppu.serialize(s); });
  mark("dsp", [&] { dsp.serialize(s); });

  if(cartridge.has.ICD) mark("icd", [&] { icd.serialize(s); });
  if(cartridge.has.MCC) mark("mcc", [&] { mcc.serialize(s); });
  if(cartridge.has.DIP) mark("dip", [&] { dip.serialize(s); });
  if(cartridge.has.Event) mark("event", [&] { event.serialize(s); });
  if(cartridge.has.SA1) mark("sa1", [&] { sa1.serialize(s); });
  if(cartridge.has.SuperFX) mark("superfx", [&] { superfx.serialize(s); });
  if(cartridge.has.ARMDSP) mark("armdsp", [&] { armdsp.serialize(s); });
  if(cartridge.has.HitachiDSP) mark("hitachidsp", [&] { hitachidsp.serialize(s); });
  if(cartridge.has.NECDSP) mark("necdsp", [&] { necdsp.serialize(s); });
  if(cartridge.has.EpsonRTC) mark("epsonrtc", [&] { epsonrtc.serialize(s); });
  if(cartridge.has.SharpRTC) mark("sharprtc", [&] { sharprtc.serialize(s); });
  if(cartridge.has.SPC7110) mark("spc7110", [&] { spc7110.serialize(s); });
  if(cartridge.has.SDD1) mark("sdd1", [&] { sdd1.serialize(s); });
  if(cartridge.has.OBC1) mark("obc1", [&] { obc1.serialize(s); });
  if(cartridge.has.MSU1) mark("msu1", [&] { msu1.serialize(s); });

  if(cartridge.has.Cx4) mark("cx4", [&] { cx4.serialize(s); });
  if(cartridge.has.DSP1) mark("dsp1", [&] { dsp1.serialize(s); });
  if(cartridge.has.DSP2) mark("dsp2", [&] { dsp2.serialize(s); });
  if(cartridge.has.DSP4) mark("dsp4", [&] { dsp4.serialize(s); });
  if(cartridge.has.ST0010) mark("st0010", [&] { st0010.serialize(s); });

  if(cartridge.has.BSMemorySlot) mark("bsmemory", [&] { bsmemory.serialize(s); });
  if(cartridge.has.SufamiTurboSlotA) mark("sufamiturboA", [&] { sufamiturboA.serialize(s); });
  if(cartridge.has.SufamiTurboSlotB) mark("sufamiturboB", [&] { sufamiturboB.serialize(s); });

  mark("controllerPort1", [&] { controllerPort1.serialize(s); });
  mark("controllerPort2", [&] { controllerPort2.serialize(s); });
  mark("expansionPort", [&] { expansionPort.serialize(s); });

  if(!synchronize) {
    hostState = true;
    mark("cpu.stack", [&] { cpu.serializeStack(s); });
    mark("smp.stack", [&] { smp.serializeStack(s); });
    mark("ppu.stack", [&] { ppu.serializeStack(s); });
    uint index = 0;
    for(auto coprocessor : cpu.coprocessors) {
      mark({"coprocessor[", index++, "].stack"}, [&] { coprocessor->serializeStack(s); });
    }
  }
}

auto System::serializeHeader(serializer& s, bool synchronize) -> void {
  uint signature = 0x31545342;
  uint serializeSize = information.serializeSize[synchronize];
  char version[16] = {};
  char description[512] = {};
  memory::copy(&version, (const char*)Emulator::SerializerVersion, Emulator::SerializerVersion.size());

  s.integer(signature);
  s.integer(serializeSize);
  s.array(version);
  s.array(description);
  s.boolean(synchronize);
  s.boolean(hacks.fastPPU);
}

//dry-run serialize recording where each component lands in the state
auto System::serializeMap(bool synchronize) -> vector<Emulator::SerializeComponent> {
  vector<Emulator::SerializeComponent> map;

  serializer s;
  serializeHeader(s, synchronize);
  map.append({"header", 0, s.size(), false});

  serializeAll(s, synchronize, &map);
  return map;
}

//perform dry-run state save:
//determines exactly how many bytes are needed to save state for this cartridge,
//as amount varies per game (eg different RAM sizes, special chips, etc.)
auto System::serializeInit(bool synchronize) -> uint {
  serializer s;
  serializeHeader(s, synchronize);
  serializeAll(s, synchronize);
  return s.size();
}
