#include <cstdint>
#include <cstdlib>

extern "C" void LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size > 0 && Data[0] == 'H') {
        if (Size > 1 && Data[1] == 'I') {
            if (Size > 2 && Data[2] == '!') {
                __builtin_trap(); // Triggers SIGTRAP/SIGILL -> Crash
            }
        }
    }
}
