#include "binary_writer.h"
#include <stdexcept>

BinaryWriter::BinaryWriter(const std::string& path)
    : out_(path, std::ios::binary) {}

bool BinaryWriter::good() const { return out_.good(); }
void BinaryWriter::flush() { out_.flush(); }

void BinaryWriter::write_u8(uint8_t value) {
    out_.put(static_cast<char>(value));
}

void BinaryWriter::write_u32_le(uint32_t value) {
    for (int i = 0; i < 4; ++i) write_u8(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
}

void BinaryWriter::write_u64_le(uint64_t value) {
    for (int i = 0; i < 8; ++i) write_u8(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
}

void BinaryWriter::write_varint(uint64_t value) {
    while (value >= 0x80) {
        write_u8(static_cast<uint8_t>((value & 0x7f) | 0x80));
        value >>= 7;
    }
    write_u8(static_cast<uint8_t>(value));
}

void BinaryWriter::write_bytes(const uint8_t* data, size_t size) {
    if (size) out_.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
}

void BinaryWriter::write_bytes(const std::string& data) {
    if (!data.empty()) out_.write(data.data(), static_cast<std::streamsize>(data.size()));
}

void BinaryWriter::write_string(const std::string& data) {
    write_varint(data.size());
    write_bytes(data);
}

void write_csv_field(std::ostream& out, const std::string& value) {
    out << '"';
    for (char c : value) {
        if (c == '"') out << "\"\"";
        else out << c;
    }
    out << '"';
}
