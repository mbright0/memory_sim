#pragma once

#include "memory_data.h"

class MemoryHierarchy {
public:
    SSD*     ssd;
    DRAM*    dram;
    L3Cache* l3;
    L2Cache* l2;
    L1Cache* l1;

    HierarchyConfig config;
    MovementLog     moveLog;

    size_t totalReads  = 0;
    size_t totalWrites = 0;
    size_t cacheHits   = 0;
    size_t cacheMisses = 0;

    explicit MemoryHierarchy(const HierarchyConfig& cfg) : config(cfg) {
        cfg.validate();

        ssd  = new SSD(cfg.ssdSize);
        dram = new DRAM(cfg.dramSize);
        l3   = new L3Cache(cfg.l3Size);
        l2   = new L2Cache(cfg.l2Size);
        l1   = new L1Cache(cfg.l1Size);

        ssd->lowerLevel  = dram;  ssd->upperLevel  = nullptr;
        dram->upperLevel = ssd;   dram->lowerLevel = l3;
        l3->upperLevel   = dram;  l3->lowerLevel   = l2;
        l2->upperLevel   = l3;    l2->lowerLevel   = l1;
        l1->upperLevel   = l2;    l1->lowerLevel   = nullptr;
    }

    ~MemoryHierarchy() {
        delete ssd; delete dram; delete l3; delete l2; delete l1;
    }

    void loadIntoSSD(const Instruction& instr) {
        ssd->store(instr);
    }

    Instruction read(uint32_t address) {
        totalReads++;
        std::cout << "READ 0x" << hex8(address) << "\n";

        Instruction hit = l1->fetchLRU(address);
        if (hit.valid) {
            std::cout << "  L1 hit: " << hit.toString() << "\n";
            cacheHits++;
            moveLog.record("L1 Cache", "CPU", hit, "READ");
            return hit;
        }
        std::cout << "  L1 miss\n";

        hit = l2->fetchLRU(address);
        if (hit.valid) {
            std::cout << "  L2 hit: " << hit.toString() << "\n";
            cacheHits++;
            l1->store(hit);
            moveLog.record("L2 Cache", "L1 Cache", hit, "READ");
            moveLog.record("L1 Cache", "CPU",      hit, "READ");
            return hit;
        }
        std::cout << "  L2 miss\n";

        hit = l3->fetchLRU(address);
        if (hit.valid) {
            std::cout << "  L3 hit: " << hit.toString() << "\n";
            cacheHits++;
            l2->store(hit);
            l1->store(hit);
            moveLog.record("L3 Cache", "L2 Cache", hit, "READ");
            moveLog.record("L2 Cache", "L1 Cache", hit, "READ");
            moveLog.record("L1 Cache", "CPU",      hit, "READ");
            return hit;
        }
        std::cout << "  L3 miss\n";

        hit = dram->fetch(address);
        if (hit.valid) {
            std::cout << "  DRAM hit: " << hit.toString() << "\n";
            cacheMisses++;
            l3->store(hit);
            l2->store(hit);
            l1->store(hit);
            moveLog.record("DRAM",     "L3 Cache", hit, "READ");
            moveLog.record("L3 Cache", "L2 Cache", hit, "READ");
            moveLog.record("L2 Cache", "L1 Cache", hit, "READ");
            moveLog.record("L1 Cache", "CPU",      hit, "READ");
            return hit;
        }
        std::cout << "  DRAM miss\n";

        hit = ssd->fetch(address);
        if (hit.valid) {
            std::cout << "  SSD hit: " << hit.toString() << "\n";
            cacheMisses++;
            dram->store(hit);
            l3->store(hit);
            l2->store(hit);
            l1->store(hit);
            moveLog.record("SSD",      "DRAM",     hit, "READ");
            moveLog.record("DRAM",     "L3 Cache", hit, "READ");
            moveLog.record("L3 Cache", "L2 Cache", hit, "READ");
            moveLog.record("L2 Cache", "L1 Cache", hit, "READ");
            moveLog.record("L1 Cache", "CPU",      hit, "READ");
            return hit;
        }

        std::cout << "  Not found in hierarchy\n";
        return Instruction();
    }

    void write(uint32_t address, uint32_t data) {
        totalWrites++;
        Instruction instr(data, address);
        std::cout << "WRITE 0x" << hex8(address) << " = 0x" << hex8(data) << "\n";

        std::cout << "  CPU -> L1 Cache\n";
        l1->store(instr);
        moveLog.record("CPU", "L1 Cache", instr, "WRITE");

        std::cout << "  L1 Cache -> L2 Cache\n";
        l2->store(instr);
        moveLog.record("L1 Cache", "L2 Cache", instr, "WRITE");

        std::cout << "  L2 Cache -> L3 Cache\n";
        l3->store(instr);
        moveLog.record("L2 Cache", "L3 Cache", instr, "WRITE");

        std::cout << "  L3 Cache -> DRAM\n";
        dram->store(instr);
        moveLog.record("L3 Cache", "DRAM", instr, "WRITE");
    }

    void printConfig() const {
        config.print();
    }

    void printMovementLog() const {
        moveLog.print();
    }

    void printStats() const {
        std::cout << "Total reads:    " << totalReads  << "\n";
        std::cout << "Total writes:   " << totalWrites << "\n";
        std::cout << "Cache hits:     " << cacheHits   << "\n";
        std::cout << "Cache misses:   " << cacheMisses << "\n";
        size_t total = cacheHits + cacheMisses;
        if (total > 0)
            std::cout << "Hit rate:       " << std::fixed << std::setprecision(1)
                      << 100.0 * cacheHits / total << "%\n";
        std::cout << "\n";
        std::cout << "Per-cache breakdown:\n";
        auto showCache = [](const CacheLevel* c) {
            size_t t = c->hits + c->misses;
            std::cout << "  " << c->name
                      << "  hits: " << c->hits
                      << "  misses: " << c->misses
                      << "  evictions: " << c->evictions;
            if (t > 0) std::cout << "  hit rate: " << std::fixed << std::setprecision(1)
                                 << 100.0 * c->hits / t << "%";
            std::cout << "\n";
        };
        showCache(l1);
        showCache(l2);
        showCache(l3);
    }

    void printState() const {
        std::cout << "SSD: " << ssd->count() << "/" << ssd->capacity << " instructions\n";
        for (const auto& i : ssd->storage)
            std::cout << "  " << i.toString() << "\n";

        std::cout << "DRAM: " << dram->count() << "/" << dram->capacity << " instructions\n";
        for (const auto& i : dram->storage)
            std::cout << "  " << i.toString() << "\n";

        for (const CacheLevel* c : {(const CacheLevel*)l3,
                                    (const CacheLevel*)l2,
                                    (const CacheLevel*)l1}) {
            size_t total = c->hits + c->misses;
            std::cout << c->name << ": " << c->count() << "/" << c->capacity
                      << " instructions"
                      << "  hits: " << c->hits
                      << "  misses: " << c->misses
                      << "  evictions: " << c->evictions;
            if (total > 0)
                std::cout << "  hit rate: " << std::fixed << std::setprecision(1)
                          << 100.0 * c->hits / total << "%";
            std::cout << "\n";
            size_t rank = 0;
            for (const auto& i : c->lruList)
                std::cout << "  [MRU+" << rank++ << "] " << i.toString() << "\n";
        }
    }

private:
    static std::string hex8(uint32_t v) {
        std::ostringstream o;
        o << std::hex << std::setw(8) << std::setfill('0') << v;
        return o.str();
    }
};
