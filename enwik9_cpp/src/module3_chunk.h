#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Module3Options {
    std::vector<uint64_t> chunk_sizes = {
        1000000ULL, 2000000ULL, 5000000ULL, 10000000ULL, 20000000ULL, 50000000ULL
    };
    std::string output_dir = "output";
};

bool run_module3(const std::string& input_path, const Module3Options& options);
