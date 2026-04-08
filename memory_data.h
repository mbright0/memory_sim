#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <sstream>
#include <vector>
#include <list>
#include <unordered_map>
#include <stdexcept>
#include <iostream>
#include <iomanip>

struct Instruction {
    uint32_t data;
    uint32_t address;
    bool     valid;

    Instruction() : data(0), address(0), valid(false) {}
    Instruction(uint32_t d, uint32_t addr) : data(d), address(addr), valid(true) {}

    std::string toString() const {
        if (!valid) return "[empty]";
        std::ostringstream o;
        o << "INSTR[0x" << std::hex << std::setw(8) << std::setfill('0')
          << address << "] = 0x" << std::setw(8) << std::setfill('0') << data;
        return o.str();
    }

    bool operator==(const Instruction& o) const {
        return address == o.address && data == o.data;
    }
};

enum class LevelID { SSD, DRAM, L3, L2, L1, CPU };

inline std::string levelName(LevelID id) {
    switch (id) {
        case LevelID::SSD:  return "SSD";
        case LevelID::DRAM: return "DRAM";
        case LevelID::L3:   return "L3 Cache";
        case LevelID::L2:   return "L2 Cache";
        case LevelID::L1:   return "L1 Cache";
        case LevelID::CPU:  return "CPU";
    }
    return "Unknown";
}

class MemoryLevel {
public:
    LevelID      id;
    std::string  name;
    size_t       capacity;
    MemoryLevel* lowerLevel;
    MemoryLevel* upperLevel;
    std::vector<Instruction> storage;
    size_t reads  = 0;
    size_t writes = 0;

    MemoryLevel(LevelID lid, size_t cap)
        : id(lid), name(levelName(lid)), capacity(cap),
          lowerLevel(nullptr), upperLevel(nullptr)
    {
        storage.reserve(cap);
    }

    virtual ~MemoryLevel() = default;

    virtual bool   isFull()  const { return storage.size() >= capacity; }
    bool           isEmpty() const { return storage.empty(); }
    virtual size_t count()   const { return storage.size(); }

    virtual bool contains(uint32_t address) const {
        for (const auto& i : storage)
            if (i.valid && i.address == address) return true;
        return false;
    }

    Instruction fetch(uint32_t address) {
        for (auto& i : storage)
            if (i.valid && i.address == address) { reads++; return i; }
        return Instruction();
    }

    virtual void store(const Instruction& instr) {
        for (auto& slot : storage) {
            if (slot.valid && slot.address == instr.address) {
                slot = instr; writes++; return;
            }
        }
        if (!isFull()) { storage.push_back(instr); writes++; }
        else throw std::runtime_error(name + " is full");
    }

    bool isDirectLower(const MemoryLevel* o) const { return lowerLevel == o; }
    bool isDirectUpper(const MemoryLevel* o) const { return upperLevel == o; }
};

class SSD : public MemoryLevel {
public:
    explicit SSD(size_t cap) : MemoryLevel(LevelID::SSD, cap) {}

    void store(const Instruction& instr) override {
        for (auto& slot : storage)
            if (slot.valid && slot.address == instr.address)
                { slot = instr; writes++; return; }
        if (isFull())
            storage.erase(storage.begin());
        storage.push_back(instr);
        writes++;
    }
};

class DRAM : public MemoryLevel {
public:
    explicit DRAM(size_t cap) : MemoryLevel(LevelID::DRAM, cap) {}

    void store(const Instruction& instr) override {
        for (auto& slot : storage)
            if (slot.valid && slot.address == instr.address)
                { slot = instr; writes++; return; }
        if (isFull() && upperLevel) {
            upperLevel->store(storage.front());
            storage.erase(storage.begin());
        }
        storage.push_back(instr);
        writes++;
    }
};

class CacheLevel : public MemoryLevel {
public:
    std::list<Instruction>                        lruList;
    std::unordered_map<uint32_t,
        std::list<Instruction>::iterator>         lruMap;

    size_t hits      = 0;
    size_t misses    = 0;
    size_t evictions = 0;

    CacheLevel(LevelID lid, size_t cap) : MemoryLevel(lid, cap) {}

    bool   isFull() const override { return lruList.size() >= capacity; }
    size_t count()  const override { return lruList.size(); }

    bool contains(uint32_t address) const override {
        return lruMap.count(address) > 0;
    }

    Instruction fetchLRU(uint32_t address) {
        auto it = lruMap.find(address);
        if (it == lruMap.end()) { misses++; return Instruction(); }
        lruList.splice(lruList.begin(), lruList, it->second);
        hits++; reads++;
        return *(it->second);
    }

    void store(const Instruction& instr) override {
        auto it = lruMap.find(instr.address);
        if (it != lruMap.end()) {
            it->second->data  = instr.data;
            it->second->valid = true;
            lruList.splice(lruList.begin(), lruList, it->second);
            writes++;
            syncStorage();
            return;
        }
        if (isFull()) evict();
        lruList.push_front(instr);
        lruMap[instr.address] = lruList.begin();
        writes++;
        syncStorage();
    }

    void evict() {
        if (lruList.empty()) return;
        Instruction victim = lruList.back();
        lruMap.erase(victim.address);
        lruList.pop_back();
        evictions++;
        std::cout << "  Evicting from " << name << ": " << victim.toString() << "\n";
        if (upperLevel) upperLevel->store(victim);
    }

    void syncStorage() {
        storage.assign(lruList.begin(), lruList.end());
    }

    size_t lruSize() const { return lruList.size(); }
};

class L3Cache : public CacheLevel {
public: explicit L3Cache(size_t cap) : CacheLevel(LevelID::L3, cap) {}
};
class L2Cache : public CacheLevel {
public: explicit L2Cache(size_t cap) : CacheLevel(LevelID::L2, cap) {}
};
class L1Cache : public CacheLevel {
public: explicit L1Cache(size_t cap) : CacheLevel(LevelID::L1, cap) {}
};

struct HierarchyConfig {
    size_t ssdSize, dramSize, l3Size, l2Size, l1Size;

    HierarchyConfig(size_t ssd, size_t dram, size_t l3, size_t l2, size_t l1)
        : ssdSize(ssd), dramSize(dram), l3Size(l3), l2Size(l2), l1Size(l1) {}

    bool validate() const {
        std::ostringstream err;
        bool ok = true;
        auto chk = [&](size_t a, size_t b, const std::string& an, const std::string& bn) {
            if (a <= b) { err << an << " must be > " << bn << "\n"; ok = false; }
        };
        chk(ssdSize, dramSize, "SSD",  "DRAM");
        chk(dramSize, l3Size,  "DRAM", "L3");
        chk(l3Size,  l2Size,   "L3",   "L2");
        chk(l2Size,  l1Size,   "L2",   "L1");
        for (auto [v, n] : std::initializer_list<std::pair<size_t, const char*>>{
                {ssdSize,"SSD"},{dramSize,"DRAM"},{l3Size,"L3"},{l2Size,"L2"},{l1Size,"L1"}})
            if (v == 0) { err << n << " capacity must be > 0\n"; ok = false; }
        if (!ok) throw std::invalid_argument("Invalid config:\n" + err.str());
        return true;
    }

    static size_t toBytes(size_t n) { return n * 4; }

    static std::string humanBytes(size_t n) {
        size_t b = toBytes(n);
        std::ostringstream o;
        if      (b >= (1 << 20)) o << std::fixed << std::setprecision(1) << b / double(1 << 20) << " MB";
        else if (b >= (1 << 10)) o << std::fixed << std::setprecision(1) << b / double(1 << 10) << " KB";
        else                     o << b << " B";
        return o.str();
    }

    void print() const {
        std::cout << "Memory Hierarchy Configuration\n";
        std::cout << "Instruction size: 32 bits (4 bytes)\n";
        std::cout << "Data flow: SSD -> DRAM -> L3 -> L2 -> L1 -> CPU\n";
        std::cout << "Eviction policy: LRU on all cache levels\n";
        std::cout << "Bypass: not allowed\n\n";
        std::cout << "Level       Instructions   Bytes        Size\n";
        auto row = [&](const std::string& nm, size_t n) {
            std::cout << std::left << std::setw(12) << nm
                      << std::right << std::setw(8) << n
                      << std::setw(10) << toBytes(n)
                      << "   " << humanBytes(n) << "\n";
        };
        row("SSD",      ssdSize);
        row("DRAM",     dramSize);
        row("L3 Cache", l3Size);
        row("L2 Cache", l2Size);
        row("L1 Cache", l1Size);
        std::cout << "\nSize ratios:\n";
        auto r = [&](size_t a, size_t b, const std::string& an, const std::string& bn) {
            std::cout << "  " << an << " / " << bn << " = "
                      << std::fixed << std::setprecision(1) << double(a) / double(b) << "x\n";
        };
        r(ssdSize, dramSize, "SSD",  "DRAM");
        r(dramSize, l3Size,  "DRAM", "L3");
        r(l3Size,  l2Size,   "L3",   "L2");
        r(l2Size,  l1Size,   "L2",   "L1");
    }
};

struct MovementEvent {
    std::string from;
    std::string to;
    Instruction instr;
    std::string trigger;
    size_t      step;
};

static const std::pair<std::string, std::string> LEGAL_HOPS[] = {
    {"SSD",      "DRAM"    },
    {"DRAM",     "SSD"     },
    {"DRAM",     "L3 Cache"},
    {"L3 Cache", "DRAM"    },
    {"L3 Cache", "L2 Cache"},
    {"L2 Cache", "L3 Cache"},
    {"L2 Cache", "L1 Cache"},
    {"L1 Cache", "L2 Cache"},
    {"L1 Cache", "CPU"     },
    {"CPU",      "L1 Cache"},
};

class MovementLog {
public:
    std::vector<MovementEvent> events;
    size_t stepCounter = 0;

    void record(const std::string& from, const std::string& to,
                const Instruction& instr, const std::string& trigger)
    {
        validateHop(from, to);
        events.push_back({from, to, instr, trigger, ++stepCounter});
    }

    static void validateHop(const std::string& from, const std::string& to) {
        for (const auto& p : LEGAL_HOPS)
            if (p.first == from && p.second == to) return;
        throw std::runtime_error(
            "Illegal hop: " + from + " -> " + to);
    }

    void clear() { events.clear(); stepCounter = 0; }

    void print() const {
        if (events.empty()) { std::cout << "  No movements recorded.\n"; return; }
        std::cout << "  Step  Trigger  From          To            Instruction\n";
        for (const auto& e : events)
            std::cout << "  " << std::left
                      << std::setw(6)  << e.step
                      << std::setw(9)  << e.trigger
                      << std::setw(14) << e.from
                      << std::setw(14) << e.to
                      << e.instr.toString() << "\n";
    }
};
