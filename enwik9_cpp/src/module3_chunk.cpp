#include "module3_chunk.h"
#include "binary_writer.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {
struct TagParseResult {
    std::map<std::string, uint64_t> counts;
    std::string carry;
};

bool is_name_start(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == ':';
}
bool is_name_char(unsigned char c) {
    return is_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

// Count complete XML element tags by normalized name. Opening and closing tags
// are distinct keys ("page" and "/page"); attributes are ignored.
// Comments, CDATA, processing instructions, and declarations are skipped.
TagParseResult parse_tags(const std::string& input, size_t current_chunk_start) {
    TagParseResult result;
    size_t i = 0;
    while (i < input.size()) {
        if (input[i] != '<') { ++i; continue; }

        if (input.compare(i, 4, "<!--") == 0) {
            size_t end = input.find("-->", i + 4);
            if (end == std::string::npos) { result.carry = input.substr(i); break; }
            i = end + 3; continue;
        }
        if (input.compare(i, 9, "<![CDATA[") == 0) {
            size_t end = input.find("]]>", i + 9);
            if (end == std::string::npos) { result.carry = input.substr(i); break; }
            i = end + 3; continue;
        }
        if (input.compare(i, 2, "<?") == 0) {
            size_t end = input.find("?>", i + 2);
            if (end == std::string::npos) { result.carry = input.substr(i); break; }
            i = end + 2; continue;
        }
        if (input.compare(i, 2, "<!") == 0) {
            // Skip declarations, respecting quoted strings and internal-subset brackets.
            bool quote = false; char quote_char = 0; int bracket_depth = 0;
            size_t j = i + 2;
            for (; j < input.size(); ++j) {
                char c = input[j];
                if (quote) { if (c == quote_char) quote = false; }
                else if (c == '"' || c == '\'') { quote = true; quote_char = c; }
                else if (c == '[') ++bracket_depth;
                else if (c == ']' && bracket_depth > 0) --bracket_depth;
                else if (c == '>' && bracket_depth == 0) break;
            }
            if (j == input.size()) { result.carry = input.substr(i); break; }
            i = j + 1; continue;
        }

        size_t name_pos = i + 1;
        bool closing = false;
        if (name_pos < input.size() && input[name_pos] == '/') { closing = true; ++name_pos; }
        if (name_pos >= input.size() || !is_name_start(static_cast<unsigned char>(input[name_pos]))) {
            ++i; continue;
        }

        size_t name_end = name_pos + 1;
        while (name_end < input.size() && is_name_char(static_cast<unsigned char>(input[name_end]))) ++name_end;

        bool quote = false; char quote_char = 0;
        size_t j = name_end;
        for (; j < input.size(); ++j) {
            char c = input[j];
            if (quote) { if (c == quote_char) quote = false; }
            else if (c == '"' || c == '\'') { quote = true; quote_char = c; }
            else if (c == '>') break;
        }
        if (j == input.size()) { result.carry = input.substr(i); break; }

        // The token is complete. Count it in the chunk where its closing '>' occurs.
        if (j >= current_chunk_start) {
            std::string key = input.substr(name_pos, name_end - name_pos);
            if (closing) key.insert(key.begin(), '/');
            ++result.counts[key];
        }
        i = j + 1;
    }
    return result;
}


}

bool run_module3(const std::string& input_path, const Module3Options& opt) {
    std::filesystem::create_directories(opt.output_dir);
    struct ComparisonRow { uint64_t chunk_size, chunks, input_bytes, binary_bytes, csv_bytes; };
    std::vector<ComparisonRow> comparison;

    for (uint64_t chunk_size : opt.chunk_sizes) {
        if (chunk_size == 0) {
            std::cerr << "Ignoring zero chunk size\n";
            continue;
        }

        std::ifstream input(input_path, std::ios::binary);
        if (!input) {
            std::cerr << "Cannot open input: " << input_path << "\n";
            return false;
        }

        const std::string stem = "data3_" + std::to_string(chunk_size) + "B";
        const std::string bin_path = (std::filesystem::path(opt.output_dir) / (stem + ".bin")).string();
        const std::string csv_path = (std::filesystem::path(opt.output_dir) / (stem + "_summary.csv")).string();
        BinaryWriter out(bin_path);
        if (!out.good()) return false;

        // Header: magic D3CK, version u32, chunk_size u64; records follow until EOF.
        out.write_bytes(std::string("D3CK"));
        out.write_u32_le(1);
        out.write_u64_le(chunk_size);

        std::ofstream csv(csv_path);
        csv << "chunk_index,offset_bytes,actual_bytes,nonzero_byte_values,distinct_tag_names,carry_in_bytes,carry_out_bytes\n";

        std::string carry;
        std::vector<char> chunk(static_cast<size_t>(std::min<uint64_t>(chunk_size, 64ULL * 1024 * 1024)));
        uint64_t offset = 0, chunk_index = 0, total_input = 0;
        while (input) {
            uint64_t remaining = chunk_size;
            std::string data;
            data.reserve(static_cast<size_t>(std::min<uint64_t>(chunk_size, 64ULL * 1024 * 1024)));
            while (remaining > 0 && input) {
                size_t want = static_cast<size_t>(std::min<uint64_t>(remaining, chunk.size()));
                input.read(chunk.data(), static_cast<std::streamsize>(want));
                std::streamsize got = input.gcount();
                if (got <= 0) break;
                data.append(chunk.data(), static_cast<size_t>(got));
                remaining -= static_cast<uint64_t>(got);
            }
            if (data.empty()) break;

            std::array<uint64_t, 256> freq{};
            for (unsigned char c : data) ++freq[c];

            std::string combined = carry + data;
            TagParseResult parsed = parse_tags(combined, carry.size());
            const size_t carry_in = carry.size();
            carry = parsed.carry;

            // Record: chunk_index varint, offset varint, actual_bytes varint,
            // sparse byte histogram (number of entries, then byte id + count),
            // tag histogram (count, name string + frequency), carry-in and carry-out.
            out.write_varint(chunk_index);
            out.write_varint(offset);
            out.write_varint(data.size());

            uint64_t nonzero = 0;
            for (uint64_t v : freq) if (v) ++nonzero;
            out.write_varint(nonzero);
            for (size_t b = 0; b < 256; ++b) {
                if (freq[b]) {
                    out.write_u8(static_cast<uint8_t>(b));
                    out.write_varint(freq[b]);
                }
            }

            out.write_varint(parsed.counts.size());
            for (const auto& item : parsed.counts) {
                out.write_string(item.first);
                out.write_varint(item.second);
            }
            out.write_string(std::string()); // reserved extension field
            out.write_varint(carry_in);
            out.write_string(carry);
            csv << chunk_index << ',' << offset << ',' << data.size() << ','
                << nonzero << ',' << parsed.counts.size() << ','
                << carry_in << ',' << carry.size() << '\n';

            offset += data.size();
            total_input += data.size();
            ++chunk_index;
        }

        out.flush();
        csv.flush();

        // Any unfinished XML token at EOF is explicitly reported, not counted as a tag.
        std::ofstream meta((std::filesystem::path(opt.output_dir) / (stem + ".meta.txt")).string());
        meta << "format=D3CK\nversion=1\nchunk_size_bytes=" << chunk_size
             << "\nchunk_count=" << chunk_index << "\ninput_bytes=" << total_input
             << "\nfinal_unfinished_tag_bytes=" << carry.size()
             << "\nbyte_histogram=256 values, sparse varint counts\n"
             << "tag_counting=complete XML element tags; attributes ignored; opening/closing separate\n"
             << "comments_cdata_pi_declarations=ignored\n";
        const auto bin_bytes = std::filesystem::file_size(bin_path);
        const auto csv_bytes = std::filesystem::file_size(csv_path);
        comparison.push_back({chunk_size, chunk_index, total_input, bin_bytes, csv_bytes});

        std::cout << "Data3 complete for chunk size " << chunk_size
                  << " bytes: " << chunk_index << " chunks -> " << bin_path << "\n";
    }

    std::ofstream cmp((std::filesystem::path(opt.output_dir) / "data3_comparison.csv").string());
    cmp << "chunk_size_bytes,chunk_count,input_bytes,binary_datasheet_bytes,csv_summary_bytes\n";
    for (const auto& row : comparison) {
        cmp << row.chunk_size << ',' << row.chunks << ',' << row.input_bytes << ','
            << row.binary_bytes << ',' << row.csv_bytes << '\n';
    }
    return true;
}
