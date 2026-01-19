# Coverage-Guided Fuzzer (aka Mini-AFL)

This project is a lightweight, custom Coverage-Guided Fuzzer for C/C++ applications. The tool automatically generates inputs, monitors code execution paths in real-time using compiler instrumentation, and identifies inputs that trigger new code coverage or application crashes.

## Building the Fuzzer

### Prerequisites
- Clang
- CMake

### Build
```bash
mkdir build
cd build
CXX=clang++ cmake -GNinja ..
```

## Running the Fuzzer

The fuzzer executable `my_fuzzer` will be located in the `build` directory. To run it:
```bash
./build/my_fuzzer
```

The fuzzer will run indefinitely, trying to find crashes in the target function. When a crash is found, it will save the input that caused the crash to a file named `crash_*.bin` in the root directory of the project.


## Logic

*   **Instrumentation:** Implements Clang `trace-pc-guard`. Maps 8-bit execution counters to logarithmic buckets to track block hit frequency.
*   **Memory Model:** Zero-allocation hot loop. Uses static 1MB buffers (`input_buffer`, `scratch_buffer`) to eliminate heap overhead.
*   **Feedback Loop:** Inputs are added to the corpus only if they trigger a new bucket transition in the global coverage map.
*   **Mutation Engine:**
    *   **Splicing:** 25% probability of merging two corpus inputs.
    *   **Elementary Strategies (75%):** Uniform distribution among:
        *   **Bit Flip:** Invert single bit.
        *   **Byte Flip:** Invert byte or substitute with edge cases (`0x00`, `0xFF`, `0x7F`, `0x80`).
        *   **Arithmetic:** Add/subtract small integers.
        *   **Insertion:** Inject edge-case byte into the buffer.
*   **Crash Handling:** Catches `SIGSEGV`, `SIGILL`, `SIGABRT`, `SIGFPE` via `sigaction`. Dumps raw memory buffer to `crash_<id>.bin` before termination.
