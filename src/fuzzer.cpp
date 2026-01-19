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
static uint8_t* permanent_seen_coverage_ptr = nullptr; // Tracks hit count buckets for each block
static uint32_t *__start_pc_guard;
static uint32_t *__stop_pc_guard;
static size_t num_guards;

static std::vector<std::vector<uint8_t>> corpus;

// Input Buffers
const size_t MAX_INPUT_SIZE = 1 * 1024 * 1024; // 1MB
static uint8_t input_buffer[MAX_INPUT_SIZE];
static size_t input_size = 0;
static uint8_t scratch_buffer[MAX_INPUT_SIZE];

static std::mt19937_64 rng(std::chrono::high_resolution_clock::now().time_since_epoch().count());

static const uint8_t INTERESTING_VALUES[] = {
    0x00, 0xFF, 0x7F, 0x01, 0x10, 0x80, 0xFE, 0x7E
    // 0x00, 0xFF, 0x7F, 0x01, 0x10, 0x80, 0xFE, 0x7E, 'H', 'I', '!'
};

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
        permanent_seen_coverage_ptr = new uint8_t[num_guards];
        for (size_t i = 0; i < num_guards; ++i) {
            global_coverage_map_ptr[i] = 0;
            permanent_seen_coverage_ptr[i] = 0;
        }
    }

    void __sanitizer_cov_trace_pc_guard(uint32_t *guard) {
        size_t idx = guard - __start_pc_guard;
        if (idx < num_guards) {
            global_coverage_map_ptr[idx]++;
        }
    }
}

uint8_t get_bucket(uint8_t hit_count) {
    if (hit_count == 0)
        return 0;
    if (hit_count == 1)
        return 1;
    if (hit_count == 2)
        return 2;
    if (hit_count == 3)
        return 3;
    if (hit_count < 8)
        return 4;
    if (hit_count < 16)
        return 5;
    if (hit_count < 32)
        return 6;
    if (hit_count < 128)
        return 7;
    return 8;
}

void reset_coverage_map() {
    memset(global_coverage_map_ptr, 0, num_guards * sizeof(uint8_t));
}

size_t mutate_bit_flip(uint8_t *data, size_t size) {
    if (size == 0) return 0;
    std::uniform_int_distribution<size_t> dist(0, size - 1);
    size_t idx = dist(rng);
    data[idx] ^= (1 << (rng() % 8)); // Flip bit
    return size;
}

size_t mutate_byte_flip(uint8_t *data, size_t size) {
    if (size == 0) return 0;
    std::uniform_int_distribution<size_t> dist(0, size - 1);
    size_t idx = dist(rng);

    if (rng() % 2 == 0) {
        data[idx] = ~data[idx]; // Invert byte
    } else {
        // replacing with interesting value
        data[idx] = INTERESTING_VALUES[rng() % (sizeof(INTERESTING_VALUES) / sizeof(INTERESTING_VALUES[0]))];
    }
    return size;
}

size_t mutate_arithmetic(uint8_t *data, size_t size) {
    if (size == 0) return 0;
    std::uniform_int_distribution<size_t> dist(0, size - 1);
    size_t idx = dist(rng);
    int8_t delta = (rng() % 3) - 1;
    data[idx] += delta;
    return size;
}

size_t mutate_insert_interesting_byte(uint8_t *data, size_t size) {
    if (size >= MAX_INPUT_SIZE) return size;

    uint8_t byte_to_insert = INTERESTING_VALUES[rng() % (sizeof(INTERESTING_VALUES) / sizeof(INTERESTING_VALUES[0]))];

    std::uniform_int_distribution<size_t> dist(0, size);
    size_t idx = dist(rng);

    memmove(data + idx + 1, data + idx, size - idx);
    data[idx] = byte_to_insert;

    return size + 1;
}

size_t mutate_splicing(uint8_t *data, size_t size, const std::vector<uint8_t>& other_input) {
    if (other_input.empty()) return size;

    std::uniform_int_distribution<size_t> dist1(0, size);
    size_t split_point = dist1(rng);

    memcpy(scratch_buffer, data, split_point);

    std::uniform_int_distribution<size_t> dist2(0, other_input.size());
    size_t start_point2 = dist2(rng);

    size_t other_size = other_input.size() - start_point2;
    if (split_point + other_size > MAX_INPUT_SIZE) {
        other_size = MAX_INPUT_SIZE - split_point;
    }

    memcpy(scratch_buffer + split_point, other_input.data() + start_point2, other_size);

    size_t new_size = split_point + other_size;
    memcpy(data, scratch_buffer, new_size);

    return new_size;
}

size_t mutate(uint8_t *data, size_t size, const std::vector<uint8_t>* other_input = nullptr) {
    if (other_input && !other_input->empty() && (rng() % 4 == 0)) { // 25% chance of splicing
        return mutate_splicing(data, size, *other_input);
    }

    if (size == 0) {
        size_t new_size = 1 + (rng() % 10);
        for (size_t i = 0; i < new_size; ++i) {
            data[i] = rng() % 256;
        }
        return new_size;
    }

    switch (rng() % 4) {
        case 0:
            return mutate_bit_flip(data, size);
        case 1:
            return mutate_byte_flip(data, size);
        case 2:
            return mutate_arithmetic(data, size);
        case 3:
            return mutate_insert_interesting_byte(data, size);
        default: return size;
    }
}

void crash_handler(int sig) {
    fprintf(stderr, "CRASH: Caught signal %d\n", sig);
    char filename[256];
    snprintf(filename, sizeof(filename), "crash_%lu_%d.bin", rng(), getpid());
    std::ofstream ofs(filename, std::ios::binary);
    if (ofs) {
        ofs.write(reinterpret_cast<const char*>(input_buffer), input_size);
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

        memcpy(input_buffer, seed.data(), seed.size());
        input_size = seed.size();

        input_size = mutate(input_buffer, input_size, &other_seed);

        reset_coverage_map();

        LLVMFuzzerTestOneInput(input_buffer, input_size);
        executions++;

        bool discovered_new_block = false;
        for (size_t i = 0; i < num_guards; ++i) {
            uint8_t bucket = get_bucket(global_coverage_map_ptr[i]);
            if (bucket > permanent_seen_coverage_ptr[i]) {
                permanent_seen_coverage_ptr[i] = bucket;
                discovered_new_block = true;
            }
        }

        if (discovered_new_block) {
            corpus.push_back(std::vector<uint8_t>(input_buffer, input_buffer + input_size));
            printf("INFO: New path found. Corpus size: %lu, input: '%.*s'\n", corpus.size(), (int)input_size,
                input_buffer);
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
