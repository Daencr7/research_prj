#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

class BinaryWriter {
public:
    explicit BinaryWriter(const std::string& path);
    bool good() const;
    void flush();
    void write_u8(uint8_t value);
    void write_u32_le(uint32_t value);
    void write_u64_le(uint64_t value);
    void write_varint(uint64_t value);
    void write_bytes(const uint8_t* data, size_t size);
    void write_bytes(const std::string& data);
    void write_string(const std::string& data);
private:
    std::ofstream out_;
};

void write_csv_field(std::ostream& out, const std::string& value);
