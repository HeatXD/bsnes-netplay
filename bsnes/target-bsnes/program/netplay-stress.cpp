#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>

struct NetplayLogger {
    auto start() -> void {
        std::lock_guard<std::mutex> lock(_mutex);
        if(_running) return;
        _running = true;
        _dropped = 0;
        _worker = std::thread([this] { run(); });
    }

    auto stop() -> void {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if(!_running) return;
            _running = false;
        }
        _condition.notify_all();
        if(_worker.joinable()) _worker.join();
    }

    auto log(string text) -> void {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if(!_running) { fputs(text, stdout); fflush(stdout); return; }
            _queue.push_back({move(text), {}, {}});
        }
        _condition.notify_one();
    }

    auto dump(string path, const uint8_t* data, uint size) -> void {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if(!_running) return;
            if(_queuedBytes + size > QueueLimit) { _dropped++; return; }
            Entry entry;
            entry.path = move(path);
            entry.data.resize(size);
            memory::copy(entry.data.data(), data, size);
            _queuedBytes += size;
            _queue.push_back(move(entry));
        }
        _condition.notify_one();
    }

private:
    enum : uint { QueueLimit = 256 * 1024 * 1024 };

    struct Entry {
        string text;
        string path;
        vector<uint8_t> data;
    };

    auto run() -> void {
        for(;;) {
            Entry entry;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _condition.wait(lock, [this] { return !_queue.empty() || !_running; });
                if(_queue.empty()) {
                    if(!_running) return;
                    continue;
                }
                entry = move(_queue.front());
                _queue.pop_front();
                if(entry.path) _queuedBytes -= entry.data.size();
            }

            if(entry.path) {
                file::write(entry.path, {entry.data.data(), entry.data.size()});
            } else {
                //quit() terminates the process, discarding buffered output
                fputs(entry.text, stdout);
                fflush(stdout);
            }
        }
    }

    std::thread _worker;
    mutable std::mutex _mutex;
    std::condition_variable _condition;
    std::deque<Entry> _queue;
    bool _running = false;
    uint _queuedBytes = 0;
    uint _dropped = 0;
};

static NetplayLogger netplayLogger;

static auto netplayChecksumUpdate(uint32_t crc, const uint8_t* data, uint size) -> uint32_t {
    static uint32_t table[256];
    static bool initialized = false;
    if(!initialized) {
        initialized = true;
        for(uint n = 0; n < 256; n++) {
            uint32_t entry = n;
            for(uint bit = 0; bit < 8; bit++) entry = (entry >> 1) ^ (entry & 1 ? 0xedb88320 : 0);
            table[n] = entry;
        }
    }

    for(uint i = 0; i < size; i++) crc = (crc >> 8) ^ table[(crc ^ data[i]) & 0xff];
    return crc;
}

//coroutine stacks hold host pointers and stale bytes: never the same twice
auto Program::netplayBuildChecksumRanges() -> void {
    netplay.checksumRanges.reset();
    for(auto& component : emulator->serializeMap(false)) {
        if(component.name.endsWith(".stack")) continue;
        netplay.checksumRanges.append({component.offset, component.size});
    }
}

auto Program::netplayPrintStateMap() -> void {
    netplayLogger.log("[netplay] serialize layout:\n");
    for(auto& component : emulator->serializeMap(false)) {
        netplayLogger.log({"[netplay]   ", pad(component.name, -24), " offset ", pad(component.offset, 10),
            "  size ", component.size, component.name.endsWith(".stack") ? "  (excluded from checksum)" : "", "\n"});
    }
}

auto Program::netplayStateChecksum(const uint8_t* data, uint size) -> uint32_t {
    uint32_t crc = ~0u;
    if(!netplay.checksumRanges) return ~netplayChecksumUpdate(crc, data, size);

    for(auto& range : netplay.checksumRanges) {
        if(range.offset >= size) continue;
        crc = netplayChecksumUpdate(crc, data + range.offset, min(range.size, size - range.offset));
    }
    return ~crc;
}

static auto netplayComponentAt(const vector<Emulator::SerializeComponent>& map, uint offset) -> string {
    for(auto& component : map) {
        if(offset >= component.offset && offset < component.offset + component.size) {
            return {component.name, "+", offset - component.offset, " (component at ", component.offset,
                ", ", component.size, " bytes)"};
        }
    }
    return "unknown";
}

auto Program::netplayResetDesyncDirectory() -> void {
    string path = {Path::userSettings(), "bsnes/desyncs/"};
    for(auto& name : directory::files(path, "*.state")) remove(string{path, name});
    directory::create(path);
}

auto Program::netplayDumpState(int frame, const string& tag, uint32 checksum, const vector<uint8>& data) -> void {
    string name = {Path::userSettings(), "bsnes/desyncs/frame", frame, "-", tag, "-", hex(checksum, 8L), ".state"};
    netplayLogger.dump(name, data.data(), data.size());
    netplayLogger.log({"[netplay]   queued ", data.size(), " byte state -> ", name, "\n"});
}

auto Program::netplayReportLocalDesync(int frame, uint32 checksumA, const vector<uint8>& bufA, uint32 checksumB, const vector<uint8>& bufB) -> void {
    netplay.desyncCount++;

    string report = {"[netplay] local resim desync at frame ", frame, ": checksum ",
        hex(checksumA, 8L), " -> ", hex(checksumB, 8L), "\n"};

    if(bufA.size() != bufB.size()) {
        report.append("[netplay]   state size differs: ", bufA.size(), " vs ", bufB.size(), " bytes\n");
    }

    uint size = min(bufA.size(), bufB.size());
    uint firstDiff = size;
    uint diffCount = 0;
    for(uint i = 0; i < size; i++) {
        if(bufA[i] != bufB[i]) {
            if(firstDiff == size) firstDiff = i;
            diffCount++;
        }
    }

    if(diffCount > 0) {
        auto map = emulator->serializeMap(false);
        report.append("[netplay]   ", diffCount, " byte(s) differ, first at offset ", firstDiff,
            " -> ", netplayComponentAt(map, firstDiff), "\n");
    }
    netplayLogger.log(report);

    if(netplay.desyncCount <= Netplay::DesyncDumpLimit) {
        netplayDumpState(frame, "resim-a", checksumA, bufA);
        netplayDumpState(frame, "resim-b", checksumB, bufB);
    } else if(netplay.desyncCount == Netplay::DesyncDumpLimit + 1) {
        netplayLogger.log("[netplay]   (further state dumps suppressed)\n");
    }
}

auto Program::netplayCacheState(int frame, uint32 checksum, const uint8* data, uint size) -> void {
    if(!netplay.stateCache.size()) return;

    auto& slot = netplay.stateCache[(uint)frame % netplay.stateCache.size()];
    if(slot.frame == frame && slot.checksum != checksum) {
        vector<uint8> current;
        current.resize(size);
        memory::copy(current.data(), data, size);
        netplayReportLocalDesync(frame, slot.checksum, slot.data, checksum, current);
    }

    netplay.lastChecksum = checksum;
    slot.frame = frame;
    slot.checksum = checksum;
    slot.data.resize(size);
    memory::copy(slot.data.data(), data, size);
}

auto Program::netplayRandomInput(uint player) -> Netplay::Buttons {
    // pure function of (block, player, seed) so runs replay identically
    uint64_t x = ((uint64_t)(netplay.counter / 6) << 8) ^ (player * 0x9e3779b97f4a7c15ull) ^ stressTestSeed;
    x ^= x >> 33; x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;

    uint16_t bits = (uint16_t)(x & 0xfff) & (uint16_t)((x >> 20) & 0xfff);

    Netplay::Buttons input = {};
    input.u.value = (int16)bits;

    if(input.u.btn.up && input.u.btn.down) input.u.btn.down = 0;
    if(input.u.btn.left && input.u.btn.right) input.u.btn.right = 0;

    return input;
}

auto Program::netplayStressStart(uint8 players, uint8 checkDistance) -> void {
    if(netplay.mode != Netplay::Mode::Inactive) return;
    if(!emulator->loaded()) return;

    players = max((uint8)1, min((uint8)5, players));

    const int inpBufferLength = players > 2 ? 5 : players;
    for(int i = 0; i < inpBufferLength; i++) {
        netplay.inputs.append(Netplay::Buttons());
    }

    emulator->connect(0, Netplay::Device::Gamepad);
    emulator->connect(1, players > 2 ? Netplay::Device::Multitap : Netplay::Device::Gamepad);

    netplayApplyDeterministicSettings();
    emulator->power();

    const int stateSize = emulator->serialize(0).size();

    netplay.config = {};
    netplay.config.num_players = players;
    netplay.config.input_size = sizeof(Netplay::Buttons);
    netplay.config.state_size = stateSize;
    netplay.config.desync_detection = true;
    netplay.config.check_distance = checkDistance;

    netplay.stateCache.resize(256);
    netplayBuildChecksumRanges();
    netplay.desyncCount = 0;
    netplay.crossPeerDesyncCount = 0;
    netplayResetDesyncDirectory();
    netplayLogger.start();

    gekko_create(&netplay.session, GekkoStressSession);
    gekko_start(netplay.session, &netplay.config);

    for(uint i = 0; i < players; i++) {
        auto peer = Netplay::Peer();
        peer.type = GekkoLocalPlayer;
        peer.id = gekko_add_actor(netplay.session, GekkoLocalPlayer, nullptr);
        netplay.peers.append(peer);
        gekko_set_local_delay(netplay.session, peer.id, 1);
    }

    video.setBlocking(false);
    audio.setBlocking(false);
    program.mute |= Mute::Always;

    netplayMode(Netplay::Stress);
    netplayLogger.log({"[netplay] stress test started: ", players, " player(s), check distance ", checkDistance,
        ", seed ", stressTestSeed, ", state ", stateSize, " bytes\n"});
    netplayPrintStateMap();
    showMessage({"Desync stress test started (", players, "p)"});
}

