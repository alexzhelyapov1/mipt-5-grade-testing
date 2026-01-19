#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <set>
#include <random>
#include <chrono>
#include <iostream>
#include <fstream>
#include <signal.h>
#include <unistd.h>
#include <cstring>

static uint8_t* global_coverage_map_ptr = nullptr; // Per-execution coverage map
static bool* permanent_seen_coverage_ptr = nullptr; // Tracks all unique blocks ever seen
static uint32_t *__start_pc_guard;
static uint32_t *__stop_pc_guard;
static size_t num_guards;

// Current input buffer being fuzzed, used by the crash handler
static std::vector<uint8_t> current_input_buffer;

static std::vector<std::vector<uint8_t>> corpus;

static std::mt19937_64 rng(std::chrono::high_resolution_clock::now().time_since_epoch().count());

extern "C" {
    void __sanitizer_cov_trace_pc_guard_init(uint32_t *start, uint32_t *stop) {
        if (__start_pc_guard != nullptr && __stop_pc_guard != nullptr) {
            return;
        }
        __start_pc_guard = start;
        __stop_pc_guard = stop;
        num_guards = stop - start;
        fprintf(stderr, "INFO: __sanitizer_cov_trace_pc_guard_init: guards: %lu\n", num_guards);
        global_coverage_map_ptr = new uint8_t[num_guards];
        permanent_seen_coverage_ptr = new bool[num_guards];
        for (size_t i = 0; i < num_guards; ++i) {
            global_coverage_map_ptr[i] = 0;
            permanent_seen_coverage_ptr[i] = false;
        }
    }

    void __sanitizer_cov_trace_pc_guard(uint32_t *guard) {
        size_t idx = guard - __start_pc_guard;
        if (idx < num_guards) {
            global_coverage_map_ptr[idx]++;
        }
    }
}

void reset_coverage_map() {
    memset(global_coverage_map_ptr, 0, num_guards * sizeof(uint8_t));
}

std::vector<uint8_t> mutate_bit_flip(const std::vector<uint8_t>& input) {
    std::vector<uint8_t> mutated_input = input;
    if (mutated_input.empty()) return mutated_input;
    std::uniform_int_distribution<size_t> dist(0, mutated_input.size() - 1);
    size_t idx = dist(rng);
    mutated_input[idx] ^= (1 << (rng() % 8)); // Flip bit
    return mutated_input;
}

std::vector<uint8_t> mutate_byte_flip(const std::vector<uint8_t>& input) {
    std::vector<uint8_t> mutated_input = input;
    if (mutated_input.empty()) return mutated_input;
    std::uniform_int_distribution<size_t> dist(0, mutated_input.size() - 1);
    size_t idx = dist(rng);

    if (rng() % 2 == 0) {
        mutated_input[idx] = ~mutated_input[idx]; // Invert byte
    } else {
        // replacing with interesting value
        const uint8_t interesting_values[] = {0x00, 0xFF, 0x7F, 0x01, 0x10, 0x80, 'H', 'I', '!'};
        mutated_input[idx] = interesting_values[rng() % (sizeof(interesting_values) / sizeof(interesting_values[0]))];
    }
    return mutated_input;
}

std::vector<uint8_t> mutate_arithmetic(const std::vector<uint8_t>& input) {
    std::vector<uint8_t> mutated_input = input;
    if (mutated_input.empty()) return mutated_input;
    std::uniform_int_distribution<size_t> dist(0, mutated_input.size() - 1);
    size_t idx = dist(rng);
    int8_t delta = (rng() % 3) - 1;
    mutated_input[idx] += delta;
    return mutated_input;
}

std::vector<uint8_t> mutate_insert_interesting_byte(const std::vector<uint8_t>& input) {
    std::vector<uint8_t> mutated_input = input;
    const uint8_t interesting_values[] = {0x00, 0xFF, 0x7F, 0x01, 0x10, 0x80, 'H', 'I', '!'};
    uint8_t byte_to_insert = interesting_values[rng() % (sizeof(interesting_values) / sizeof(interesting_values[0]))];

    std::uniform_int_distribution<size_t> dist(0, mutated_input.size());
    size_t idx = dist(rng);

    mutated_input.insert(mutated_input.begin() + idx, byte_to_insert);
    return mutated_input;
}

std::vector<uint8_t> mutate_splicing(const std::vector<uint8_t>& input1, const std::vector<uint8_t>& input2) {
    if (input1.empty() || input2.empty()) {
        return input1.empty() ? input2 : input1;
    }

    std::uniform_int_distribution<size_t> dist1(0, input1.size());
    size_t split_point = dist1(rng);

    std::vector<uint8_t> new_input;
    new_input.insert(new_input.end(), input1.begin(), input1.begin() + split_point);

    std::uniform_int_distribution<size_t> dist2(0, input2.size());
    size_t start_point2 = dist2(rng);

    new_input.insert(new_input.end(), input2.begin() + start_point2, input2.end());

    return new_input;
}

std::vector<uint8_t> mutate(const std::vector<uint8_t>& input, const std::vector<uint8_t>* other_input = nullptr) {
    if (other_input && !other_input->empty() && (rng() % 4 == 0)) { // 25% chance of splicing
        return mutate_splicing(input, *other_input);
    }

    if (input.empty()) {
        std::vector<uint8_t> initial_input;
        initial_input.resize(1 + (rng() % 10), rng() % 256); // Start with random bytes
        return initial_input;
    }

    switch (rng() % 4) {
        case 0:
            return mutate_bit_flip(input);
        case 1:
            return mutate_byte_flip(input);
        case 2:
            return mutate_arithmetic(input);
        case 3:
            return mutate_insert_interesting_byte(input);
        default: return input;
    }
}

void crash_handler(int sig) {
    fprintf(stderr, "CRASH: Caught signal %d\n", sig);
    char filename[256];
    snprintf(filename, sizeof(filename), "crash_%lu_%d.bin", rng(), getpid());
    std::ofstream ofs(filename, std::ios::binary);
    if (ofs) {
        ofs.write(reinterpret_cast<const char*>(current_input_buffer.data()), current_input_buffer.size());
        ofs.close();
        fprintf(stderr, "CRASH: Input saved to %s\n", filename);
    } else {
        fprintf(stderr, "CRASH: Failed to save input to file %s\n", filename);
    }
    _exit(1);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size);

int main() {
    struct sigaction sa;
    sa.sa_handler = crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr); // Catch SIGILL for __builtin_trap()

    // start with an empty input
    corpus.push_back({});

    unsigned long long executions = 0;
    auto start_time = std::chrono::high_resolution_clock::now();

    while (true) {
        std::uniform_int_distribution<size_t> corpus_dist(0, corpus.size() - 1);
        const std::vector<uint8_t>& seed = corpus[corpus_dist(rng)];
        const std::vector<uint8_t>& other_seed = corpus[corpus_dist(rng)];

        std::vector<uint8_t> test_input = mutate(seed, &other_seed);
        current_input_buffer = test_input;

        reset_coverage_map();

        LLVMFuzzerTestOneInput(test_input.data(), test_input.size());
        executions++;

        bool discovered_new_block = false;
        for (size_t i = 0; i < num_guards; ++i) {
            if (global_coverage_map_ptr[i] > 0 && !permanent_seen_coverage_ptr[i]) {
                permanent_seen_coverage_ptr[i] = true;
                discovered_new_block = true;
            }
        }

        if (discovered_new_block) {
            corpus.push_back(test_input);
            fprintf(stderr, "INFO: New path found. Corpus size: %lu\n", corpus.size());
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);

        if (duration.count() >= 1) {
            double execs_per_sec = static_cast<double>(executions) / duration.count();
            fprintf(stderr, "Corp: %lu | Execs: %llu | Execs/s: %.2f\n", corpus.size(), executions, execs_per_sec);
            start_time = end_time;
            executions = 0;
        }
    }

    return 0;
}
