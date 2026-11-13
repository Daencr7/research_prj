#include "module1_sequence.h"
#include "binary_writer.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using CountMap = std::unordered_map<std::string, uint64_t>;

bool is_separator(unsigned char c) {
    // LF, CR, TAB, and ordinary space are excluded and break n-gram windows.
    return c == 0x0a || c == 0x0d || c == 0x09 || c == 0x20;
}

void write_varint_stream(std::ostream& out, uint64_t value) {
    while (value >= 0x80) {
        out.put(static_cast<char>((value & 0x7f) | 0x80));
        value >>= 7;
    }
    out.put(static_cast<char>(value));
}

void write_run(const std::string& path, const CountMap& counts) {
    std::vector<std::pair<std::string, uint64_t>> rows(counts.begin(), counts.end());
    std::sort(rows.begin(), rows.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    std::ofstream out(path, std::ios::binary);
    for (const auto& row : rows) {
        out.put(static_cast<char>(row.first.size()));
        out.write(row.first.data(), static_cast<std::streamsize>(row.first.size()));
        write_varint_stream(out, row.second);
    }
}

bool read_varint(std::istream& in, uint64_t& value) {
    value = 0;
    unsigned shift = 0;
    for (int i = 0; i < 10; ++i) {
        int ch = in.get();
        if (ch == EOF) return false;
        uint8_t b = static_cast<uint8_t>(ch);
        value |= static_cast<uint64_t>(b & 0x7f) << shift;
        if ((b & 0x80) == 0) return true;
        shift += 7;
    }
    return false;
}

struct RunReader {
    std::ifstream in;
    std::string key;
    uint64_t count = 0;
    bool valid = false;

    explicit RunReader(const std::string& path) : in(path, std::ios::binary) { advance(); }

    void advance() {
        int len = in.get();
        if (len == EOF) { valid = false; return; }
        key.assign(static_cast<size_t>(static_cast<uint8_t>(len)), '\0');
        if (!key.empty()) in.read(&key[0], static_cast<std::streamsize>(key.size()));
        if (!in || !read_varint(in, count)) { valid = false; return; }
        valid = true;
    }
};

struct HeapNode {
    std::string key;
    size_t run_id;
    uint64_t count;
};
struct HeapGreater {
    bool operator()(const HeapNode& a, const HeapNode& b) const {
        return a.key > b.key;
    }
};

void write_record(BinaryWriter& out, const std::string& key, uint64_t count) {
    out.write_u8(static_cast<uint8_t>(key.size()));
    out.write_bytes(key);
    out.write_varint(count);
}
} // namespace

bool run_module1(const std::string& input_path, const Module1Options& opt) {
    if (opt.max_length == 0 || opt.max_length > 255 || opt.max_unique_entries == 0) {
        std::cerr << "Data1: max-length must be 1..255 and max-unique-entries > 0\n";
        return false;
    }

    std::filesystem::path prefix(opt.output_prefix);
    if (prefix.has_parent_path()) std::filesystem::create_directories(prefix.parent_path());

    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        std::cerr << "Cannot open input: " << input_path << "\n";
        return false;
    }

    CountMap counts;
    counts.reserve(std::min<size_t>(opt.max_unique_entries, 1000000));
    std::vector<std::string> run_paths;
    std::string current;
    current.reserve(opt.max_length);
    std::array<char, 1 << 16> buffer{};
    uint64_t bytes_read = 0;
    size_t run_index = 0;

    auto spill = [&]() {
        if (counts.empty()) return;
        std::string path = opt.output_prefix + ".run" + std::to_string(run_index++);
        write_run(path, counts);
        run_paths.push_back(path);
        counts.clear();
        counts.rehash(0);
    };

    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::streamsize n = input.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            unsigned char c = static_cast<unsigned char>(buffer[static_cast<size_t>(i)]);
            ++bytes_read;
            if (is_separator(c)) {
                current.clear();
                continue;
            }
            current.push_back(static_cast<char>(c));
            if (current.size() > opt.max_length) current.erase(current.begin());
            for (size_t len = 1; len <= current.size(); ++len) {
                std::string key = current.substr(current.size() - len, len);
                ++counts[key];
            }
            if (counts.size() >= opt.max_unique_entries) spill();
        }
    }
    spill();

    const std::string final_path = opt.output_prefix + ".bin";
    BinaryWriter out(final_path);
    if (!out.good()) {
        std::cerr << "Cannot create: " << final_path << "\n";
        return false;
    }

    // File format: magic "D1NG", version u32, max_len u8, record_count u64,
    // then sorted records: key_length u8, key bytes, occurrence_count unsigned varint.
    out.write_bytes(std::string("D1NG"));
    out.write_u32_le(1);
    out.write_u8(static_cast<uint8_t>(opt.max_length));

    std::vector<std::unique_ptr<RunReader>> readers;
    readers.reserve(run_paths.size());
    std::priority_queue<HeapNode, std::vector<HeapNode>, HeapGreater> heap;
    for (size_t i = 0; i < run_paths.size(); ++i) {
        readers.emplace_back(new RunReader(run_paths[i]));
        if (readers.back()->valid)
            heap.push({readers.back()->key, i, readers.back()->count});
    }

    // Records continue until EOF; the exact record count is stored in metadata.
    uint64_t records = 0;
    while (!heap.empty()) {
        HeapNode node = heap.top();
        heap.pop();
        uint64_t total = node.count;

        readers[node.run_id]->advance();
        if (readers[node.run_id]->valid) {
            heap.push({readers[node.run_id]->key, node.run_id, readers[node.run_id]->count});
        }

        while (!heap.empty() && heap.top().key == node.key) {
            HeapNode same = heap.top();
            heap.pop();
            total += same.count;
            readers[same.run_id]->advance();
            if (readers[same.run_id]->valid)
                heap.push({readers[same.run_id]->key, same.run_id, readers[same.run_id]->count});
        }
        write_record(out, node.key, total);
        ++records;
    }

    std::ofstream meta(opt.output_prefix + ".meta.txt");
    meta << "format=D1NG\nversion=1\nmax_length=" << opt.max_length
         << "\nrecords=" << records << "\ninput_bytes=" << bytes_read
         << "\nseparators=TAB,LF,CR,SPACE\nordering=lexicographic\n"
         << "count_encoding=unsigned_varint\n";


    for (const auto& path : run_paths) std::filesystem::remove(path);
    std::cout << "Data1 complete: " << records << " distinct sequences; " << final_path << "\n";
    return true;
}
