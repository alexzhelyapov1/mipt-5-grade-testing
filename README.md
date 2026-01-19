# Coverage-Guided Fuzzer

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
