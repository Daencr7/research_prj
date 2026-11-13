#include "module1_sequence.h"
#include "module3_chunk.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static void usage(const char* exe) {
    std::cout
        << "Usage:\n"
        << "  " << exe << " <input.bin> all [max_ngram] [max_unique_entries]\n"
        << "  " << exe << " <input.bin> data1 [max_ngram] [max_unique_entries]\n"
        << "  " << exe << " <input.bin> data3 [chunk_bytes_csv]\n\n"
        << "Examples:\n"
        << "  " << exe << " enwik9 all 5 500000\n"
        << "  " << exe << " enwik9 data1 5 500000\n"
        << "  " << exe << " enwik9 data3 1000000,2000000,5000000,10000000\n\n"
        << "Chunk sizes are decimal bytes (1 MB = 1,000,000 bytes).\n";
}

static std::vector<uint64_t> parse_sizes(const std::string& text) {
    std::vector<uint64_t> values;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(',', start);
        std::string part = text.substr(start, end == std::string::npos ? end : end - start);
        if (!part.empty()) values.push_back(std::stoull(part));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return values;
}

int main(int argc, char** argv) {
    if (argc < 3) { usage(argv[0]); return 1; }

    const std::string input = argv[1];
    const std::string mode = argv[2];

    if (mode == "all" || mode == "data1") {
        Module1Options m1;
        if (argc >= 4) m1.max_length = static_cast<unsigned>(std::stoul(argv[3]));
        if (argc >= 5) m1.max_unique_entries = static_cast<size_t>(std::stoull(argv[4]));
        if (!run_module1(input, m1)) return 2;
        if (mode == "data1") return 0;
    }

    if (mode == "all" || mode == "data3") {
        Module3Options m3;
        if (argc >= 4 && mode == "data3") m3.chunk_sizes = parse_sizes(argv[3]);
        if (!run_module3(input, m3)) return 3;
        return 0;
    }

    usage(argv[0]);
    return 1;
}
