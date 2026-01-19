cmake_minimum_required(VERSION 3.10)
project(CoverageFuzzer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_program(CLANG_COMPILER NAMES clang++)
if(CLANG_COMPILER)
    set(CMAKE_CXX_COMPILER "${CLANG_COMPILER}")
else()
    message(FATAL_ERROR "Clang++ compiler not found. Please install Clang.")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fsanitize-coverage=trace-pc-guard -fno-sanitize-recover=all")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize-coverage=trace-pc-guard")

add_executable(my_fuzzer src/fuzzer.cpp src/target.cpp)

target_link_libraries(my_fuzzer PRIVATE ${SANITIZER_COMMON_LIBS})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
