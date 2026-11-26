#include "coord/modules/archive.h"

#include "coord/modules/checksum.h"
#include "coord/modules/platform.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace coord::archive {
namespace {

using Bytes = std::string_view;

std::uint8_t byteAt(Bytes data, std::size_t index) {
  return static_cast<std::uint8_t>(data[index]);
}

std::uint16_t readLe16(const char* data) {
  return static_cast<std::uint16_t>(
      static_cast<std::uint8_t>(data[0]) |
      (static_cast<std::uint16_t>(static_cast<std::uint8_t>(data[1])) << 8));
}

std::uint32_t readLe32(const char* data) {
  return static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[0])) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[1])) << 8) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[2])) << 16) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[3])) << 24);
}

bool failText(std::string& error, std::string message) {
  error = std::move(message);
  return false;
}

std::string cString(Bytes data) {
  std::size_t length = 0;
  while (length < data.size() && data[length] != '\0') {
    ++length;
  }
  return std::string(data.substr(0, length));
}

// --------------------------------------------------------------- compression

class BitReader {
 public:
  explicit BitReader(Bytes data) : data_(data) {}

  int bit() {
    if (bitCount_ == 0) {
      if (position_ >= data_.size()) {
        return -1;
      }
      bitBuffer_ = byteAt(data_, position_++);
      bitCount_ = 8;
    }
    const int value = static_cast<int>(bitBuffer_ & 1U);
    bitBuffer_ >>= 1;
    --bitCount_;
    return value;
  }

  int bits(int count) {
    int value = 0;
    for (int i = 0; i < count; ++i) {
      const int current = bit();
      if (current < 0) {
        return -1;
      }
      value |= current << i;
    }
    return value;
  }

  void alignToByte() {
    bitBuffer_ = 0;
    bitCount_ = 0;
  }

  int byte() {
    if (bitCount_ != 0 || position_ >= data_.size()) {
      return -1;
    }
    return byteAt(data_, position_++);
  }

  [[nodiscard]] std::size_t consumed() const { return position_; }

 private:
  Bytes data_;
  std::size_t position_ = 0;
  unsigned bitBuffer_ = 0;
  int bitCount_ = 0;
};

struct Huffman {
  std::array<int, 16> count{};
  std::vector<int> symbol;
};

bool buildHuffman(Huffman& table, const std::vector<int>& lengths) {
  table.count.fill(0);
  for (const int length : lengths) {
    if (length < 0 || length > 15) {
      return false;
    }
    ++table.count[static_cast<std::size_t>(length)];
  }
  int left = 1;
  for (int length = 1; length < 16; ++length) {
    left <<= 1;
    left -= table.count[static_cast<std::size_t>(length)];
    if (left < 0) {
      return false;  // over-subscribed code
    }
  }
  std::array<int, 16> offsets{};
  offsets[1] = 0;
  for (int length = 1; length < 15; ++length) {
    offsets[static_cast<std::size_t>(length) + 1] =
        offsets[static_cast<std::size_t>(length)] +
        table.count[static_cast<std::size_t>(length)];
  }
  table.symbol.assign(lengths.size(), 0);
  for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol) {
    if (lengths[symbol] != 0) {
      const std::size_t length = static_cast<std::size_t>(lengths[symbol]);
      table.symbol[static_cast<std::size_t>(offsets[length])] =
          static_cast<int>(symbol);
      ++offsets[length];
    }
  }
  return true;
}

int decodeSymbol(BitReader& reader, const Huffman& table) {
  if (table.symbol.empty()) {
    return -1;
  }
  int code = 0;
  int first = 0;
  int index = 0;
  for (int length = 1; length <= 15; ++length) {
    const int bit = reader.bit();
    if (bit < 0) {
      return -1;
    }
    code |= bit;
    const int count = table.count[static_cast<std::size_t>(length)];
    if (code - count < first) {
      const std::size_t position =
          static_cast<std::size_t>(index + (code - first));
      if (position >= table.symbol.size()) {
        return -1;
      }
      return table.symbol[position];
    }
    index += count;
    first += count;
    first <<= 1;
    code <<= 1;
  }
  return -1;
}

constexpr std::array<int, 29> kLengthBase = {
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<int, 29> kLengthExtra = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<int, 30> kDistanceBase = {
    1,    2,    3,    4,    5,    7,     9,     13,    17,   25,
    33,   49,   65,   97,   129,  193,   257,   385,   513,  769,
    1025, 1537, 2049, 3073, 4097, 6145,  8193,  12289, 16385, 24577};
constexpr std::array<int, 30> kDistanceExtra = {
    0, 0, 0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,
    6, 7, 7,  8,  8,  9,  9,  10, 10, 11, 11, 12, 12, 13, 13};

bool inflateImpl(Bytes data, std::string& output, std::string& error,
                 std::size_t maxOutput, std::size_t* consumed) {
  output.clear();
  BitReader reader(data);
  const auto decodeBlock = [&](const Huffman& literal,
                               const Huffman& distance) -> bool {
    for (;;) {
      const int symbol = decodeSymbol(reader, literal);
      if (symbol < 0) {
        return failText(error, "invalid DEFLATE literal/length code");
      }
      if (symbol < 256) {
        output.push_back(static_cast<char>(symbol));
        if (output.size() > maxOutput) {
          return failText(error, "inflated data exceeds the size limit");
        }
        continue;
      }
      if (symbol == 256) {
        return true;
      }
      const int lengthCode = symbol - 257;
      if (lengthCode >= static_cast<int>(kLengthBase.size())) {
        return failText(error, "invalid DEFLATE length code");
      }
      const int lengthExtra =
          reader.bits(kLengthExtra[static_cast<std::size_t>(lengthCode)]);
      if (lengthExtra < 0) {
        return failText(error, "truncated DEFLATE stream");
      }
      const std::size_t length =
          static_cast<std::size_t>(
              kLengthBase[static_cast<std::size_t>(lengthCode)]) +
          static_cast<std::size_t>(lengthExtra);
      const int distanceSymbol = decodeSymbol(reader, distance);
      if (distanceSymbol < 0 ||
          distanceSymbol >= static_cast<int>(kDistanceBase.size())) {
        return failText(error, "invalid DEFLATE distance code");
      }
      const int distanceExtra = reader.bits(
          kDistanceExtra[static_cast<std::size_t>(distanceSymbol)]);
      if (distanceExtra < 0) {
        return failText(error, "truncated DEFLATE stream");
      }
      const std::size_t distanceValue =
          static_cast<std::size_t>(
              kDistanceBase[static_cast<std::size_t>(distanceSymbol)]) +
          static_cast<std::size_t>(distanceExtra);
      if (distanceValue == 0 || distanceValue > output.size()) {
        return failText(error, "DEFLATE distance reaches before the output");
      }
      if (output.size() + length > maxOutput) {
        return failText(error, "inflated data exceeds the size limit");
      }
      for (std::size_t i = 0; i < length; ++i) {
        const char value = output[output.size() - distanceValue];
        output.push_back(value);
      }
    }
  };

  int last = 0;
  while (last == 0) {
    last = reader.bits(1);
    if (last < 0) {
      return failText(error, "truncated DEFLATE stream");
    }
    const int type = reader.bits(2);
    if (type < 0) {
      return failText(error, "truncated DEFLATE stream");
    }
    if (type == 0) {
      reader.alignToByte();
      const int lengthLow = reader.byte();
      const int lengthHigh = reader.byte();
      const int complementLow = reader.byte();
      const int complementHigh = reader.byte();
      if (lengthLow < 0 || lengthHigh < 0 || complementLow < 0 ||
          complementHigh < 0) {
        return failText(error, "truncated stored DEFLATE block");
      }
      const std::size_t length =
          static_cast<std::size_t>(lengthLow | (lengthHigh << 8));
      const std::size_t complement = static_cast<std::size_t>(
          complementLow | (complementHigh << 8));
      if (((length ^ 0xFFFFU) & 0xFFFFU) != complement) {
        return failText(error, "stored DEFLATE block length mismatch");
      }
      if (output.size() + length > maxOutput) {
        return failText(error, "inflated data exceeds the size limit");
      }
      for (std::size_t i = 0; i < length; ++i) {
        const int value = reader.byte();
        if (value < 0) {
          return failText(error, "truncated stored DEFLATE block");
        }
        output.push_back(static_cast<char>(value));
      }
      continue;
    }
    Huffman literal;
    Huffman distance;
    if (type == 1) {
      std::vector<int> literalLengths(288, 0);
      for (std::size_t i = 0; i <= 143; ++i) {
        literalLengths[i] = 8;
      }
      for (std::size_t i = 144; i <= 255; ++i) {
        literalLengths[i] = 9;
      }
      for (std::size_t i = 256; i <= 279; ++i) {
        literalLengths[i] = 7;
      }
      for (std::size_t i = 280; i <= 287; ++i) {
        literalLengths[i] = 8;
      }
      std::vector<int> distanceLengths(32, 5);
      if (!buildHuffman(literal, literalLengths) ||
          !buildHuffman(distance, distanceLengths)) {
        return failText(error, "invalid fixed DEFLATE Huffman table");
      }
    } else if (type == 2) {
      const int literalCountRaw = reader.bits(5);
      const int distanceCountRaw = reader.bits(5);
      const int codeCountRaw = reader.bits(4);
      if (literalCountRaw < 0 || distanceCountRaw < 0 || codeCountRaw < 0) {
        return failText(error, "truncated dynamic DEFLATE header");
      }
      const std::size_t literalCount =
          static_cast<std::size_t>(literalCountRaw) + 257;
      const std::size_t distanceCount =
          static_cast<std::size_t>(distanceCountRaw) + 1;
      const std::size_t codeCount =
          static_cast<std::size_t>(codeCountRaw) + 4;
      static constexpr std::array<int, 19> kCodeOrder = {
          16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
      std::vector<int> codeLengths(19, 0);
      for (std::size_t i = 0; i < codeCount; ++i) {
        const int length = reader.bits(3);
        if (length < 0) {
          return failText(error, "truncated dynamic DEFLATE header");
        }
        codeLengths[static_cast<std::size_t>(kCodeOrder[i])] = length;
      }
      Huffman codeTable;
      if (!buildHuffman(codeTable, codeLengths)) {
        return failText(error, "invalid DEFLATE code-length table");
      }
      std::vector<int> lengths(literalCount + distanceCount, 0);
      std::size_t index = 0;
      while (index < lengths.size()) {
        const int symbol = decodeSymbol(reader, codeTable);
        if (symbol < 0) {
          return failText(error, "invalid DEFLATE code-length symbol");
        }
        if (symbol < 16) {
          lengths[index++] = symbol;
          continue;
        }
        std::size_t repeat = 0;
        int repeatValue = 0;
        if (symbol == 16) {
          if (index == 0) {
            return failText(error, "DEFLATE repeat with no previous length");
          }
          const int extra = reader.bits(2);
          if (extra < 0) {
            return failText(error, "truncated DEFLATE stream");
          }
          repeat = static_cast<std::size_t>(extra) + 3;
          repeatValue = lengths[index - 1];
        } else if (symbol == 17) {
          const int extra = reader.bits(3);
          if (extra < 0) {
            return failText(error, "truncated DEFLATE stream");
          }
          repeat = static_cast<std::size_t>(extra) + 3;
        } else if (symbol == 18) {
          const int extra = reader.bits(7);
          if (extra < 0) {
            return failText(error, "truncated DEFLATE stream");
          }
          repeat = static_cast<std::size_t>(extra) + 11;
        } else {
          return failText(error, "invalid DEFLATE code-length symbol");
        }
        if (index + repeat > lengths.size()) {
          return failText(error, "DEFLATE repeat exceeds the table");
        }
        for (std::size_t i = 0; i < repeat; ++i) {
          lengths[index++] = repeatValue;
        }
      }
      std::vector<int> literalLengths(lengths.begin(),
                                      lengths.begin() +
                                          static_cast<std::ptrdiff_t>(
                                              literalCount));
      std::vector<int> distanceLengths(
          lengths.begin() + static_cast<std::ptrdiff_t>(literalCount),
          lengths.end());
      if (!buildHuffman(literal, literalLengths) ||
          !buildHuffman(distance, distanceLengths)) {
        return failText(error, "invalid dynamic DEFLATE Huffman table");
      }
    } else {
      return failText(error, "reserved DEFLATE block type");
    }
    if (!decodeBlock(literal, distance)) {
      return false;
    }
  }
  if (consumed != nullptr) {
    *consumed = reader.consumed();
  }
  return true;
}

// ---------------------------------------------------------------------- gzip

bool parseGzipHeader(Bytes data, std::size_t offset, std::size_t& dataStart,
                     std::string& error) {
  if (data.size() - offset < 18) {
    return failText(error, "truncated gzip header");
  }
  if (byteAt(data, offset) != 0x1F || byteAt(data, offset + 1) != 0x8B) {
    return failText(error, "not a gzip stream");
  }
  if (byteAt(data, offset + 2) != 8) {
    return failText(error, "unsupported gzip compression method");
  }
  const std::uint8_t flags = byteAt(data, offset + 3);
  if ((flags & 0xE0U) != 0) {
    return failText(error, "invalid gzip flags");
  }
  std::size_t position = offset + 10;
  if ((flags & 0x04U) != 0) {  // FEXTRA
    if (position + 2 > data.size()) {
      return failText(error, "truncated gzip extra field");
    }
    const std::size_t length = readLe16(data.data() + position);
    position += 2;
    if (length > data.size() - position) {
      return failText(error, "truncated gzip extra field");
    }
    position += length;
  }
  if ((flags & 0x08U) != 0) {  // FNAME
    while (position < data.size() && data[position] != '\0') {
      ++position;
    }
    if (position >= data.size()) {
      return failText(error, "unterminated gzip file name");
    }
    ++position;
  }
  if ((flags & 0x10U) != 0) {  // FCOMMENT
    while (position < data.size() && data[position] != '\0') {
      ++position;
    }
    if (position >= data.size()) {
      return failText(error, "unterminated gzip comment");
    }
    ++position;
  }
  if ((flags & 0x02U) != 0) {  // FHCRC
    if (position + 2 > data.size()) {
      return failText(error, "truncated gzip header checksum");
    }
    position += 2;
  }
  if (position >= data.size()) {
    return failText(error, "truncated gzip stream");
  }
  dataStart = position;
  return true;
}

// ----------------------------------------------------------------------- tar

std::uint64_t parseTarNumber(const char* field, std::size_t size) {
  if (size == 0) {
    return 0;
  }
  if ((static_cast<unsigned char>(field[0]) & 0x80U) != 0) {
    // Base-256 encoding for large values (GNU tar).
    std::uint64_t value = static_cast<std::uint64_t>(
        static_cast<unsigned char>(field[0]) & 0x7FU);
    for (std::size_t i = 1; i < size; ++i) {
      value = (value << 8) |
              static_cast<std::uint64_t>(static_cast<unsigned char>(field[i]));
    }
    return value;
  }
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < size; ++i) {
    const char character = field[i];
    if (character == '\0' || character == ' ') {
      if (value != 0 || character == '\0') {
        break;
      }
      continue;
    }
    if (character < '0' || character > '7') {
      break;
    }
    value = value * 8 + static_cast<std::uint64_t>(character - '0');
  }
  return value;
}

std::string readTarField(const char* header, std::size_t offset,
                         std::size_t size) {
  std::size_t length = 0;
  while (length < size && header[offset + length] != '\0') {
    ++length;
  }
  return std::string(header + offset, length);
}

struct RawEntry {
  std::string name;
  std::string link;
  std::string data;
  std::uint32_t mode = 0644;
  bool directory = false;
  bool symlink = false;
  bool unsupported = false;
};

bool parsePaxRecords(Bytes data, std::string& name, std::string& link,
                     std::uint64_t& size) {
  std::size_t position = 0;
  while (position < data.size()) {
    const std::size_t space = data.find(' ', position);
    if (space == Bytes::npos) {
      break;
    }
    std::size_t length = 0;
    for (std::size_t i = position; i < space; ++i) {
      if (data[i] < '0' || data[i] > '9') {
        return false;
      }
      length = length * 10 + static_cast<std::size_t>(data[i] - '0');
    }
    if (length == 0 || length > data.size() - position) {
      return false;
    }
    const Bytes record = data.substr(space + 1, position + length - space - 2);
    const std::size_t equals = record.find('=');
    if (equals == Bytes::npos) {
      return false;
    }
    const Bytes key = record.substr(0, equals);
    const Bytes value = record.substr(equals + 1);
    if (key == "path") {
      name = std::string(value);
    } else if (key == "linkpath") {
      link = std::string(value);
    } else if (key == "size") {
      std::uint64_t parsed = 0;
      for (const char character : value) {
        if (character < '0' || character > '9') {
          return false;
        }
        parsed = parsed * 10 +
                 static_cast<std::uint64_t>(character - '0');
      }
      size = parsed;
    }
    position += length;
  }
  return true;
}

bool parseTarEntries(Bytes tar, std::vector<RawEntry>& entries,
                     std::string& error) {
  std::size_t offset = 0;
  std::string pendingName;
  std::string pendingLink;
  std::uint64_t pendingSize = std::numeric_limits<std::uint64_t>::max();
  int emptyBlocks = 0;
  while (offset + 512 <= tar.size()) {
    const char* header = tar.data() + offset;
    bool empty = true;
    for (std::size_t i = 0; i < 512; ++i) {
      if (header[i] != '\0') {
        empty = false;
        break;
      }
    }
    if (empty) {
      ++emptyBlocks;
      offset += 512;
      if (emptyBlocks >= 2) {
        break;
      }
      continue;
    }
    emptyBlocks = 0;
    const std::uint64_t storedChecksum = parseTarNumber(header + 148, 8);
    std::uint64_t computedChecksum = 0;
    for (std::size_t i = 0; i < 512; ++i) {
      const unsigned char value =
          (i >= 148 && i < 156) ? static_cast<unsigned char>(' ')
                                : static_cast<unsigned char>(header[i]);
      computedChecksum += value;
    }
    if (storedChecksum != computedChecksum) {
      return failText(error, "invalid tar header checksum");
    }
    std::string name = readTarField(header, 0, 100);
    const std::string prefix = readTarField(header, 345, 155);
    if (!prefix.empty()) {
      name = prefix + "/" + name;
    }
    const char type = header[156];
    const std::uint64_t headerSize = parseTarNumber(header + 124, 12);
    const std::uint32_t mode =
        static_cast<std::uint32_t>(parseTarNumber(header + 100, 8));
    const std::uint64_t size =
        pendingSize != std::numeric_limits<std::uint64_t>::max()
            ? pendingSize
            : headerSize;
    const std::size_t dataOffset = offset + 512;
    if (size > tar.size() - dataOffset) {
      return failText(error, "truncated tar member '" + name + "'");
    }
    const Bytes data = tar.substr(dataOffset, static_cast<std::size_t>(size));
    const std::size_t padded =
        (static_cast<std::size_t>(size) + 511) / 512 * 512;

    if (type == 'L') {
      pendingName = cString(data);
    } else if (type == 'K') {
      pendingLink = cString(data);
    } else if (type == 'x' || type == 'X') {
      std::string paxName;
      std::string paxLink;
      std::uint64_t paxSize = std::numeric_limits<std::uint64_t>::max();
      if (!parsePaxRecords(data, paxName, paxLink, paxSize)) {
        return failText(error, "invalid pax extended header");
      }
      if (!paxName.empty()) {
        pendingName = paxName;
      }
      if (!paxLink.empty()) {
        pendingLink = paxLink;
      }
      if (paxSize != std::numeric_limits<std::uint64_t>::max()) {
        // The size in the pax record is authoritative; the following header
        // contains the real payload. Recompute using the record size.
        pendingSize = paxSize;
        // Rewind so the next loop iteration sees the following header again.
        offset += 512 + padded;
        continue;
      }
    } else if (type == 'g') {
      // Global pax headers are ignored.
    } else {
      RawEntry entry;
      entry.name = pendingName.empty() ? name : pendingName;
      entry.mode = mode == 0 ? 0644U : mode;
      switch (type) {
        case '0':
        case '\0':
        case '7':
          entry.data.assign(data);
          break;
        case '5':
          entry.directory = true;
          break;
        case '1':
        case '2':
          entry.symlink = true;
          entry.link = pendingLink.empty() ? readTarField(header, 157, 100)
                                           : pendingLink;
          break;
        default:
          entry.unsupported = true;
          break;
      }
      entries.push_back(std::move(entry));
      pendingName.clear();
      pendingLink.clear();
      pendingSize = std::numeric_limits<std::uint64_t>::max();
    }
    offset = dataOffset + padded;
  }
  return true;
}

// ----------------------------------------------------------------------- zip

bool parseZipEntries(Bytes zip, std::vector<RawEntry>& entries,
                     std::string& error) {
  if (zip.size() < 22) {
    return failText(error, "truncated ZIP archive");
  }
  std::size_t eocd = Bytes::npos;
  const std::size_t limit =
      zip.size() > 65557 ? zip.size() - 65557 : 0;
  for (std::size_t position = zip.size() - 22;; --position) {
    if (byteAt(zip, position) == 'P' && byteAt(zip, position + 1) == 'K' &&
        byteAt(zip, position + 2) == 5 && byteAt(zip, position + 3) == 6) {
      eocd = position;
      break;
    }
    if (position == limit) {
      break;
    }
  }
  if (eocd == Bytes::npos) {
    return failText(error, "ZIP end-of-central-directory record not found");
  }
  const std::uint16_t disk = readLe16(zip.data() + eocd + 4);
  const std::uint16_t centralDisk = readLe16(zip.data() + eocd + 6);
  const std::uint16_t entriesOnDisk = readLe16(zip.data() + eocd + 8);
  const std::uint16_t entryCount = readLe16(zip.data() + eocd + 10);
  const std::uint32_t centralSize = readLe32(zip.data() + eocd + 12);
  const std::uint32_t centralOffset = readLe32(zip.data() + eocd + 16);
  if (disk != 0 || centralDisk != 0 || entriesOnDisk != entryCount) {
    return failText(error, "multi-disk ZIP archives are not supported");
  }
  if (entryCount == 0xFFFFU || centralSize == 0xFFFFFFFFU ||
      centralOffset == 0xFFFFFFFFU) {
    return failText(error, "ZIP64 archives are not supported");
  }
  if (static_cast<std::uint64_t>(centralOffset) + centralSize > zip.size()) {
    return failText(error, "truncated ZIP central directory");
  }
  std::size_t position = centralOffset;
  for (std::uint16_t i = 0; i < entryCount; ++i) {
    if (position + 46 > zip.size() || byteAt(zip, position) != 'P' ||
        byteAt(zip, position + 1) != 'K' || byteAt(zip, position + 2) != 1 ||
        byteAt(zip, position + 3) != 2) {
      return failText(error, "invalid ZIP central directory entry");
    }
    const std::uint16_t flags = readLe16(zip.data() + position + 8);
    const std::uint16_t method = readLe16(zip.data() + position + 10);
    const std::uint32_t compressedSize = readLe32(zip.data() + position + 20);
    const std::uint32_t uncompressedSize =
        readLe32(zip.data() + position + 24);
    const std::uint16_t nameLength = readLe16(zip.data() + position + 28);
    const std::uint16_t extraLength = readLe16(zip.data() + position + 30);
    const std::uint16_t commentLength = readLe16(zip.data() + position + 32);
    const std::uint32_t localOffset = readLe32(zip.data() + position + 42);
    const std::size_t nameOffset = position + 46;
    if (nameOffset + nameLength > zip.size()) {
      return failText(error, "truncated ZIP entry name");
    }
    const std::string name(zip.substr(nameOffset, nameLength));
    if ((flags & 0x0001U) != 0) {
      return failText(error, "encrypted ZIP entries are not supported");
    }
    if (compressedSize == 0xFFFFFFFFU || uncompressedSize == 0xFFFFFFFFU) {
      return failText(error, "ZIP64 entries are not supported");
    }
    if (method != 0 && method != 8) {
      return failText(error, "unsupported ZIP compression method for '" + name +
                                 "'");
    }
    if (localOffset + 30 > zip.size() ||
        byteAt(zip, localOffset) != 'P' ||
        byteAt(zip, localOffset + 1) != 'K' ||
        byteAt(zip, localOffset + 2) != 3 ||
        byteAt(zip, localOffset + 3) != 4) {
      return failText(error, "invalid ZIP local header for '" + name + "'");
    }
    const std::uint16_t localNameLength =
        readLe16(zip.data() + localOffset + 26);
    const std::uint16_t localExtraLength =
        readLe16(zip.data() + localOffset + 28);
    const std::size_t dataOffset = static_cast<std::size_t>(localOffset) + 30 +
                                   localNameLength + localExtraLength;
    if (dataOffset + compressedSize > zip.size()) {
      return failText(error, "truncated ZIP data for '" + name + "'");
    }
    RawEntry entry;
    entry.name = name;
    entry.directory = !name.empty() && name.back() == '/';
    std::string content;
    if (method == 0) {
      content.assign(zip.substr(dataOffset, compressedSize));
    } else {
      std::string inflateError;
      if (!inflateImpl(zip.substr(dataOffset, compressedSize), content,
                       inflateError, uncompressedSize == 0
                                          ? 512ull * 1024 * 1024
                                          : uncompressedSize,
                       nullptr)) {
        return failText(error, "cannot inflate ZIP entry '" + name +
                                   "': " + inflateError);
      }
    }
    if (content.size() != uncompressedSize) {
      return failText(error, "ZIP entry size mismatch for '" + name + "'");
    }
    entry.data = std::move(content);
    entries.push_back(std::move(entry));
    position = nameOffset + nameLength + extraLength + commentLength;
  }
  return true;
}

// ------------------------------------------------------------------ security

bool isReservedWindowsName(const std::string& component) {
  std::string stem = component;
  const std::size_t dot = stem.find('.');
  if (dot != std::string::npos) {
    stem = stem.substr(0, dot);
  }
  for (char& character : stem) {
    character = static_cast<char>(
        std::toupper(static_cast<unsigned char>(character)));
  }
  static const char* const kReserved[] = {
      "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
      "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
      "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
  for (const char* name : kReserved) {
    if (stem == name) {
      return true;
    }
  }
  return false;
}

bool normalizeEntryName(std::string_view raw, std::string& normalized,
                        std::string& error) {
  std::string text(raw);
  for (char& character : text) {
    if (character == '\\') {
      character = '/';
    }
  }
  while (text.rfind("./", 0) == 0) {
    text.erase(0, 2);
  }
  if (text.empty()) {
    normalized.clear();
    return true;
  }
  if (text.front() == '/') {
    return failText(error, "absolute archive entry '" + std::string(raw) + "'");
  }
  if (text.size() >= 2 &&
      std::isalpha(static_cast<unsigned char>(text[0])) != 0 &&
      text[1] == ':') {
    return failText(error, "drive-qualified archive entry '" + std::string(raw) +
                               "'");
  }
  std::vector<std::string> components;
  std::size_t begin = 0;
  while (begin <= text.size()) {
    const std::size_t end = text.find('/', begin);
    const std::string component =
        text.substr(begin, end == std::string::npos ? std::string::npos
                                                    : end - begin);
    if (!component.empty() && component != ".") {
      if (component == "..") {
        return failText(error, "archive entry escapes the destination: '" +
                                   std::string(raw) + "'");
      }
      if (component.find('\0') != std::string::npos ||
          component.find_first_of("<>:\"|?*") != std::string::npos ||
          isReservedWindowsName(component)) {
        return failText(error, "unsafe archive entry '" + std::string(raw) +
                                   "'");
      }
#if defined(_WIN32)
      if (component.back() == ' ' || component.back() == '.') {
        return failText(error, "unsafe archive entry '" + std::string(raw) +
                                   "'");
      }
#endif
      components.push_back(component);
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
  normalized.clear();
  for (std::size_t i = 0; i < components.size(); ++i) {
    if (i != 0) {
      normalized.push_back('/');
    }
    normalized += components[i];
  }
  return true;
}

std::string commonRootDirectory(const std::vector<RawEntry>& entries) {
  std::string root;
  bool initialized = false;
  for (const RawEntry& entry : entries) {
    std::string name = entry.name;
    while (!name.empty() && name.back() == '/') {
      name.pop_back();
    }
    if (name.empty()) {
      continue;
    }
    const std::size_t slash = name.find('/');
    if (slash == std::string::npos) {
      if (entry.directory) {
        continue;  // an explicit root directory entry
      }
      return std::string();  // a file at the top level: do not strip
    }
    const std::string first = name.substr(0, slash);
    if (!initialized) {
      root = first;
      initialized = true;
    } else if (root != first) {
      return std::string();
    }
  }
  return root;
}

bool writeExtractedFile(const std::filesystem::path& path, std::string_view data,
                        std::uint32_t mode, std::string& error) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return failText(error, "cannot create '" + platform::pathToUtf8(path) + "'");
  }
  output.write(data.data(), static_cast<std::streamsize>(data.size()));
  output.close();
  if (!output) {
    return failText(error, "cannot write '" + platform::pathToUtf8(path) + "'");
  }
#if !defined(_WIN32)
  if (mode != 0) {
    ::chmod(platform::pathToUtf8(path).c_str(),
            static_cast<mode_t>(mode & 0777U));
  }
#else
  (void)mode;
#endif
  return true;
}

ExtractResult extractEntries(std::vector<RawEntry>& entries,
                             const std::filesystem::path& destination,
                             const ExtractOptions& options) {
  ExtractResult result;
  std::string directoryError;
  if (!platform::ensureDirectory(destination, directoryError)) {
    result.error = directoryError;
    return result;
  }
  // Validate every entry name before any common-root stripping: otherwise a
  // leading `..` could be "stripped" into a harmless looking path.
  for (RawEntry& entry : entries) {
    std::string normalized;
    if (!normalizeEntryName(entry.name, normalized, result.error)) {
      return result;
    }
    entry.name = normalized;
    if (entry.symlink) {
      std::string linkTarget;
      if (!normalizeEntryName(entry.link, linkTarget, result.error)) {
        return result;
      }
      entry.link = linkTarget;
    }
  }
  std::string prefix;
  if (options.stripSingleRoot) {
    prefix = commonRootDirectory(entries);
  }
  for (RawEntry& entry : entries) {
    std::string name = entry.name;
    if (!prefix.empty()) {
      if (name == prefix || name == prefix + "/") {
        continue;
      }
      if (name.rfind(prefix + "/", 0) == 0) {
        name.erase(0, prefix.size() + 1);
      } else {
        result.error = "archive entry outside the package root: '" + entry.name +
                       "'";
        return result;
      }
    }
    std::string normalized;
    if (!normalizeEntryName(name, normalized, result.error)) {
      return result;
    }
    if (normalized.empty()) {
      continue;
    }
    if (entry.unsupported) {
      result.warnings.push_back("skipped special entry '" + normalized + "'");
      continue;
    }
    if (entry.symlink) {
      if (!options.allowSymlinks) {
        result.warnings.push_back("skipped symbolic link '" + normalized + "'");
        continue;
      }
      std::string linkTarget;
      if (!normalizeEntryName(entry.link, linkTarget, result.error)) {
        return result;
      }
      if (linkTarget.empty()) {
        result.warnings.push_back("skipped symbolic link with empty target");
        continue;
      }
      const std::filesystem::path target =
          platform::pathFromUtf8(normalized);
      const std::filesystem::path full = destination / target;
      std::error_code linkError;
      std::filesystem::create_directories(full.parent_path(), linkError);
      std::filesystem::create_symlink(platform::pathFromUtf8(linkTarget), full,
                                      linkError);
      if (linkError) {
        result.warnings.push_back("cannot create symbolic link '" + normalized +
                                  "': " + linkError.message());
      }
      continue;
    }
    const std::filesystem::path relative = platform::pathFromUtf8(normalized);
    const std::filesystem::path target = destination / relative;
    if (entry.directory) {
      std::string error;
      if (!platform::ensureDirectory(target, error)) {
        result.error = error;
        return result;
      }
      ++result.directories;
      continue;
    }
    if (result.bytes + entry.data.size() > options.maxTotalBytes) {
      result.error = "archive exceeds the extraction size limit";
      return result;
    }
    std::string directoryMessage;
    if (!platform::ensureDirectory(target.parent_path(), directoryMessage)) {
      result.error = directoryMessage;
      return result;
    }
    std::string writeError;
    if (!writeExtractedFile(target, entry.data, entry.mode, writeError)) {
      result.error = writeError;
      return result;
    }
    result.bytes += entry.data.size();
    ++result.files;
  }
  result.ok = true;
  return result;
}

// ---------------------------------------------------------------- tar output

void writeOctalField(char* field, std::size_t width, std::uint64_t value) {
  std::string digits;
  do {
    digits.push_back(static_cast<char>('0' + (value & 7U)));
    value >>= 3;
  } while (value != 0 && digits.size() + 1 < width);
  while (digits.size() + 1 < width) {
    digits.push_back('0');
  }
  std::reverse(digits.begin(), digits.end());
  for (std::size_t i = 0; i < digits.size(); ++i) {
    field[i] = digits[i];
  }
  field[width - 1] = '\0';
}

void writeNameField(char* field, std::size_t width, std::string_view name) {
  const std::size_t length = std::min(width, name.size());
  std::memcpy(field, name.data(), length);
  if (length < width) {
    std::memset(field + length, 0, width - length);
  }
}

std::string makeTarHeader(std::string_view name, std::string_view prefix,
                          std::uint64_t size, char type, std::uint32_t mode) {
  std::array<char, 512> header{};
  writeNameField(header.data(), 100, name);
  writeOctalField(header.data() + 100, 8, mode & 07777U);
  writeOctalField(header.data() + 108, 8, 0);
  writeOctalField(header.data() + 116, 8, 0);
  writeOctalField(header.data() + 124, 12, size);
  writeOctalField(header.data() + 136, 12, 0);  // deterministic mtime
  std::memset(header.data() + 148, ' ', 8);
  header[156] = type;
  std::memcpy(header.data() + 257, "ustar", 6);
  header[263] = '0';
  header[264] = '0';
  writeNameField(header.data() + 265, 32, "coord");
  writeNameField(header.data() + 297, 32, "coord");
  writeNameField(header.data() + 345, 155, prefix);
  std::uint64_t checksum = 0;
  for (const char character : header) {
    checksum += static_cast<unsigned char>(character);
  }
  char checksumField[8];
  std::snprintf(checksumField, sizeof(checksumField), "%06llo",
                static_cast<unsigned long long>(checksum & 0777777U));
  std::memcpy(header.data() + 148, checksumField, 6);
  header[154] = '\0';
  header[155] = ' ';
  return std::string(header.data(), header.size());
}

void appendTarData(std::string& tar, std::string_view data) {
  tar.append(data);
  const std::size_t remainder = data.size() % 512;
  if (remainder != 0) {
    tar.append(512 - remainder, '\0');
  }
}

void appendTarMember(std::string& tar, std::string_view fullName,
                     std::string_view data, char type, std::uint32_t mode) {
  std::string name(fullName);
  if (type == '5' && (name.empty() || name.back() != '/')) {
    name.push_back('/');
  }
  std::string prefix;
  std::string shortName = name;
  if (name.size() > 100) {
    bool split = false;
    for (std::size_t position = name.rfind('/'); position != std::string::npos;
         position = position == 0 ? std::string::npos
                                  : name.rfind('/', position - 1)) {
      if (position <= 155 && name.size() - position - 1 <= 100 &&
          position != 0 && name.size() - position - 1 > 0) {
        prefix = name.substr(0, position);
        shortName = name.substr(position + 1);
        split = true;
        break;
      }
    }
    if (!split) {
      std::string longName(name);
      longName.push_back('\0');
      tar += makeTarHeader("././@LongLink", {}, longName.size(), 'L', 0644);
      appendTarData(tar, longName);
      shortName = name.substr(0, 100);
    }
  }
  tar += makeTarHeader(shortName, prefix, data.size(), type, mode);
  appendTarData(tar, data);
}

}  // namespace

bool inflate(std::string_view deflate, std::string& output, std::string& error,
             std::size_t maxOutput) {
  return inflateImpl(deflate, output, error, maxOutput, nullptr);
}

bool gunzip(std::string_view gzip, std::string& output, std::string& error,
            std::size_t maxOutput) {
  output.clear();
  std::size_t offset = 0;
  bool foundMember = false;
  while (offset + 18 <= gzip.size()) {
    if (byteAt(gzip, offset) != 0x1F || byteAt(gzip, offset + 1) != 0x8B) {
      break;
    }
    std::size_t dataStart = 0;
    if (!parseGzipHeader(gzip, offset, dataStart, error)) {
      return false;
    }
    std::string chunk;
    std::size_t consumed = 0;
    if (output.size() > maxOutput) {
      return failText(error, "inflated data exceeds the size limit");
    }
    if (!inflateImpl(gzip.substr(dataStart), chunk, error,
                     maxOutput - output.size(), &consumed)) {
      return false;
    }
    const std::size_t trailer = dataStart + consumed;
    if (trailer + 8 > gzip.size()) {
      return failText(error, "truncated gzip trailer");
    }
    const std::uint32_t expectedCrc = readLe32(gzip.data() + trailer);
    const std::uint32_t expectedSize = readLe32(gzip.data() + trailer + 4);
    if (checksum::crc32(chunk) != expectedCrc) {
      return failText(error, "gzip CRC32 mismatch");
    }
    if (static_cast<std::uint32_t>(chunk.size()) != expectedSize) {
      return failText(error, "gzip size mismatch");
    }
    output += chunk;
    if (output.size() > maxOutput) {
      return failText(error, "inflated data exceeds the size limit");
    }
    offset = trailer + 8;
    foundMember = true;
  }
  if (!foundMember) {
    return failText(error, "not a gzip stream");
  }
  return true;
}

ExtractResult extractTar(std::string_view tar,
                         const std::filesystem::path& destination,
                         const ExtractOptions& options) {
  ExtractResult result;
  std::vector<RawEntry> entries;
  if (!parseTarEntries(tar, entries, result.error)) {
    return result;
  }
  if (entries.size() > options.maxEntries) {
    result.error = "archive contains too many entries";
    return result;
  }
  return extractEntries(entries, destination, options);
}

ExtractResult extractZip(std::string_view zip,
                         const std::filesystem::path& destination,
                         const ExtractOptions& options) {
  ExtractResult result;
  std::vector<RawEntry> entries;
  if (!parseZipEntries(zip, entries, result.error)) {
    return result;
  }
  if (entries.size() > options.maxEntries) {
    result.error = "archive contains too many entries";
    return result;
  }
  return extractEntries(entries, destination, options);
}

ExtractResult extract(const std::filesystem::path& archive,
                      const std::filesystem::path& destination,
                      const ExtractOptions& options) {
  ExtractResult result;
  std::error_code sizeError;
  const std::uintmax_t fileSize = std::filesystem::file_size(archive, sizeError);
  if (sizeError) {
    result.error = "cannot read archive '" + platform::pathToUtf8(archive) + "'";
    return result;
  }
  if (fileSize > 1024ull * 1024 * 1024) {
    result.error = "archive is larger than 1 GiB";
    return result;
  }
  std::string readError;
  std::optional<std::string> bytes =
      platform::readFile(archive, readError);
  if (!bytes.has_value()) {
    result.error = readError;
    return result;
  }
  const std::string_view data(*bytes);
  if (data.size() >= 2 && byteAt(data, 0) == 0x1F &&
      byteAt(data, 1) == 0x8B) {
    std::string inflated;
    std::string gzipError;
    if (!gunzip(data, inflated, gzipError, options.maxTotalBytes)) {
      result.error = "cannot decompress '" + platform::pathToUtf8(archive) +
                     "': " + gzipError;
      return result;
    }
    return extractTar(inflated, destination, options);
  }
  if (data.size() >= 4 && byteAt(data, 0) == 'P' && byteAt(data, 1) == 'K' &&
      ((byteAt(data, 2) == 3 && byteAt(data, 3) == 4) ||
       (byteAt(data, 2) == 5 && byteAt(data, 3) == 6))) {
    return extractZip(data, destination, options);
  }
  return extractTar(data, destination, options);
}

std::string createTarGz(const std::vector<ArchiveEntry>& entries,
                        std::string& error) {
  std::map<std::string, ArchiveEntry> files;
  for (const ArchiveEntry& entry : entries) {
    std::string normalized;
    if (!normalizeEntryName(entry.path, normalized, error)) {
      return std::string();
    }
    if (normalized.empty()) {
      continue;
    }
    ArchiveEntry copy = entry;
    copy.path = normalized;
    files[copy.path] = std::move(copy);
  }
  // Add parent directory entries so extractors always see a complete tree.
  std::vector<std::string> parents;
  for (const auto& [path, entry] : files) {
    for (std::size_t slash = path.find('/'); slash != std::string::npos;
         slash = path.find('/', slash + 1)) {
      const std::string parent = path.substr(0, slash + 1);
      if (files.find(parent) == files.end()) {
        parents.push_back(parent);
      }
    }
  }
  for (const std::string& parent : parents) {
    ArchiveEntry directory;
    directory.path = parent;
    directory.directory = true;
    directory.mode = 0755;
    files.emplace(parent, std::move(directory));
  }
  std::string tar;
  for (const auto& [path, entry] : files) {
    if (entry.directory) {
      appendTarMember(tar, path, {}, '5', entry.mode);
    } else {
      appendTarMember(tar, path, entry.data, '0', entry.mode);
    }
  }
  tar.append(1024, '\0');
  return gzipStore(tar);
}

std::string gzipStore(std::string_view data) {
  std::string output;
  output.reserve(data.size() + 64);
  const auto push8 = [&output](std::uint8_t value) {
    output.push_back(static_cast<char>(value));
  };
  const auto push16 = [&push8](std::uint16_t value) {
    push8(static_cast<std::uint8_t>(value & 0xFF));
    push8(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  };
  const auto push32 = [&push8](std::uint32_t value) {
    push8(static_cast<std::uint8_t>(value & 0xFF));
    push8(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    push8(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    push8(static_cast<std::uint8_t>((value >> 24) & 0xFF));
  };
  push8(0x1F);
  push8(0x8B);
  push8(0x08);
  push8(0x00);  // no flags
  push32(0);    // mtime
  push8(0x00);  // compression level hint
  push8(0xFF);  // unknown operating system
  std::size_t offset = 0;
  do {
    const std::size_t remaining = data.size() - offset;
    const std::size_t block = std::min<std::size_t>(remaining, 65535);
    const bool last = offset + block >= data.size();
    push8(static_cast<std::uint8_t>(last ? 1 : 0));  // BFINAL, BTYPE=00
    push16(static_cast<std::uint16_t>(block));
    push16(static_cast<std::uint16_t>(~block & 0xFFFFU));
    if (block != 0) {
      output.append(data.data() + offset, block);
    }
    offset += block;
    if (block == 0) {
      break;
    }
  } while (offset < data.size());
  push32(checksum::crc32(data));
  push32(static_cast<std::uint32_t>(data.size() & 0xFFFFFFFFU));
  return output;
}

}  // namespace coord::archive
