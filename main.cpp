#include "memory_hierarchy.h"
#include <iostream>
#include <string>

static void header(const std::string& title) {
    std::cout << "\n--- " << title << " ---\n\n";
}

int main() {

    HierarchyConfig cfg(64, 32, 16, 8, 4);
    MemoryHierarchy mem(cfg);

    header("1. Memory Hierarchy Configuration");
    mem.printConfig();

    header("Loading Instructions into SSD");
    for (uint32_t i = 0; i < 10; ++i) {
        uint32_t addr = 0x00400000 + i * 4;
        uint32_t data = 0x8C080000 + i;
        mem.loadIntoSSD(Instruction(data, addr));
    }
    std::cout << "Loaded 10 instructions into SSD.\n";

    header("2. Instruction Access Trace");

    std::cout << "Access 1: cold miss\n";
    mem.moveLog.clear();
    mem.read(0x00400000);

    std::cout << "\nAccess 2: cold miss\n";
    mem.moveLog.clear();
    mem.read(0x00400004);

    std::cout << "\nAccess 3: L1 hit\n";
    mem.moveLog.clear();
    mem.read(0x00400000);

    std::cout << "\nAccess 4-6: fill L1 to capacity\n";
    for (uint32_t i = 2; i < 5; ++i)
        mem.read(0x00400000 + i * 4);

    std::cout << "\nAccess 7: L1 full, LRU eviction on miss\n";
    mem.moveLog.clear();
    mem.read(0x00400014);

    std::cout << "\nAccess 8: write new address\n";
    mem.moveLog.clear();
    mem.write(0x00500000, 0xDEADBEEF);

    std::cout << "\nAccess 9: write existing address\n";
    mem.moveLog.clear();
    mem.write(0x00400000, 0xCAFEBABE);

    std::cout << "\nAccess 10: read causing eviction cascade\n";
    mem.moveLog.clear();
    mem.read(0x00400018);

    header("3. Movement of Data Across Levels");
    std::cout << "Movement log for last access:\n";
    mem.printMovementLog();

    header("4. Cache Hits / Misses");
    mem.printStats();

    header("5. Final State of Each Memory Level");
    mem.printState();

    return 0;
}
