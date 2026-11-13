#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

struct Module1Options {
    unsigned max_length = 5;
    size_t max_unique_entries = 500000;
    std::string output_prefix = "output/data1";
};

bool run_module1(const std::string& input_path, const Module1Options& options);
