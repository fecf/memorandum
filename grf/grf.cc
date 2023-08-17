#include "grf.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>
#include <mutex>

#include <zlib/zlib.h>

namespace des {

constexpr uint8_t mask[]{0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};

constexpr int initial_permutation_table[]{
    58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4,
    62, 54, 46, 38, 30, 22, 14, 6, 64, 56, 48, 40, 32, 24, 16, 8,
    57, 49, 41, 33, 25, 17, 9,  1, 59, 51, 43, 35, 27, 19, 11, 3,
    61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7,
};

constexpr int final_permutation_table[]{
    40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31,
    38, 6, 46, 14, 54, 22, 62, 30, 37, 5, 45, 13, 53, 21, 61, 29,
    36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
    34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9,  49, 17, 57, 25,
};

constexpr int transposition_table[]{
    16, 7, 20, 21, 29, 12, 28, 17, 1,  15, 23, 26, 5,  18, 31, 10,
    2,  8, 24, 14, 32, 27, 3,  9,  19, 13, 30, 6,  22, 11, 4,  25,
};

constexpr uint8_t substitution_box_table[4][64] = {
  {
    0xef, 0x03, 0x41, 0xfd, 0xd8, 0x74, 0x1e, 0x47, 0x26, 0xef, 0xfb,
    0x22, 0xb3, 0xd8, 0x84, 0x1e, 0x39, 0xac, 0xa7, 0x60, 0x62, 0xc1,
    0xcd, 0xba, 0x5c, 0x96, 0x90, 0x59, 0x05, 0x3b, 0x7a, 0x85, 0x40,
    0xfd, 0x1e, 0xc8, 0xe7, 0x8a, 0x8b, 0x21, 0xda, 0x43, 0x64, 0x9f,
    0x2d, 0x14, 0xb1, 0x72, 0xf5, 0x5b, 0xc8, 0xb6, 0x9c, 0x37, 0x76,
    0xec, 0x39, 0xa0, 0xa3, 0x05, 0x52, 0x6e, 0x0f, 0xd9,
  },
  {
    0xa7, 0xdd, 0x0d, 0x78, 0x9e, 0x0b, 0xe3, 0x95, 0x60, 0x36, 0x36,
    0x4f, 0xf9, 0x60, 0x5a, 0xa3, 0x11, 0x24, 0xd2, 0x87, 0xc8, 0x52,
    0x75, 0xec, 0xbb, 0xc1, 0x4c, 0xba, 0x24, 0xfe, 0x8f, 0x19, 0xda,
    0x13, 0x66, 0xaf, 0x49, 0xd0, 0x90, 0x06, 0x8c, 0x6a, 0xfb, 0x91,
    0x37, 0x8d, 0x0d, 0x78, 0xbf, 0x49, 0x11, 0xf4, 0x23, 0xe5, 0xce,
    0x3b, 0x55, 0xbc, 0xa2, 0x57, 0xe8, 0x22, 0x74, 0xce,
  },
  {
    0x2c, 0xea, 0xc1, 0xbf, 0x4a, 0x24, 0x1f, 0xc2, 0x79, 0x47, 0xa2,
    0x7c, 0xb6, 0xd9, 0x68, 0x15, 0x80, 0x56, 0x5d, 0x01, 0x33, 0xfd,
    0xf4, 0xae, 0xde, 0x30, 0x07, 0x9b, 0xe5, 0x83, 0x9b, 0x68, 0x49,
    0xb4, 0x2e, 0x83, 0x1f, 0xc2, 0xb5, 0x7c, 0xa2, 0x19, 0xd8, 0xe5,
    0x7c, 0x2f, 0x83, 0xda, 0xf7, 0x6b, 0x90, 0xfe, 0xc4, 0x01, 0x5a,
    0x97, 0x61, 0xa6, 0x3d, 0x40, 0x0b, 0x58, 0xe6, 0x3d,
  },
  {
    0x4d, 0xd1, 0xb2, 0x0f, 0x28, 0xbd, 0xe4, 0x78, 0xf6, 0x4a, 0x0f,
    0x93, 0x8b, 0x17, 0xd1, 0xa4, 0x3a, 0xec, 0xc9, 0x35, 0x93, 0x56,
    0x7e, 0xcb, 0x55, 0x20, 0xa0, 0xfe, 0x6c, 0x89, 0x17, 0x62, 0x17,
    0x62, 0x4b, 0xb1, 0xb4, 0xde, 0xd1, 0x87, 0xc9, 0x14, 0x3c, 0x4a,
    0x7e, 0xa8, 0xe2, 0x7d, 0xa0, 0x9f, 0xf6, 0x5c, 0x6a, 0x09, 0x8d,
    0xf0, 0x0f, 0xe3, 0x53, 0x25, 0x95, 0x36, 0x28, 0xcb,
  },
};

void initial_permutation(uint8_t* src, int index) {
  uint8_t tmp[8]{};
  for (int i = 0; i < 64; i += 1) {
    const auto j = initial_permutation_table[i] - 1;
    if (src[index + ((j >> 3) & 7)] & mask[j & 7]) {
      tmp[(i >> 3) & 7] |= mask[i & 7];
    }
  }
  ::memcpy(src + index, tmp, 8);
}

void final_permutation(uint8_t* src, int index) {
  uint8_t tmp[8]{};
  for (int i = 0; i < 64; i += 1) {
    const auto j = final_permutation_table[i] - 1;
    if (src[index + ((j >> 3) & 7)] & mask[j & 7]) {
      tmp[(i >> 3) & 7] |= mask[i & 7];
    }
  }
  ::memcpy(src + index, tmp, 8);
}

void transposition(uint8_t* src, int index) {
  uint8_t tmp[8]{};
  for (int i = 0; i < 32; i += 1) {
    const auto j = transposition_table[i] - 1;
    if (src[index + (j >> 3)] & mask[j & 7]) {
      tmp[(i >> 3) + 4] |= mask[i & 7];
    }
  }
  ::memcpy(src + index, tmp, 8);
}

void expansion(uint8_t* src, int index) {
  uint8_t tmp[8]{};
  tmp[0] = ((src[index + 7] << 5) | (src[index + 4] >> 3)) & 0x3f;  // ..0 vutsr
  tmp[1] = ((src[index + 4] << 1) | (src[index + 5] >> 7)) & 0x3f;  // ..srqpo n
  tmp[2] = ((src[index + 4] << 5) | (src[index + 5] >> 3)) & 0x3f;  // ..o nmlkj
  tmp[3] = ((src[index + 5] << 1) | (src[index + 6] >> 7)) & 0x3f;  // ..kjihg f
  tmp[4] = ((src[index + 5] << 5) | (src[index + 6] >> 3)) & 0x3f;  // ..g fedcb
  tmp[5] = ((src[index + 6] << 1) | (src[index + 7] >> 7)) & 0x3f;  // ..cba98 7
  tmp[6] = ((src[index + 6] << 5) | (src[index + 7] >> 3)) & 0x3f;  // ..8 76543
  tmp[7] = ((src[index + 7] << 1) | (src[index + 4] >> 7)) & 0x3f;  // ..43210 v
  ::memcpy(src + index, tmp, 8);
}

void substitution_box(uint8_t* src, int index) {
  uint8_t tmp[8]{};
  for (int i = 0; i < 4; i += 1) {
    tmp[i] = (substitution_box_table[i][src[i * 2 + 0 + index]] & 0xf0) |
             (substitution_box_table[i][src[i * 2 + 1 + index]] & 0x0f);
  }
  ::memcpy(src + index, tmp, 8);
}

void round_function(uint8_t* src, int index) {
  uint8_t tmp[8]{};
  for (int i = 0; i < 8; i += 1) {
    tmp[i] = src[index + i];
  }
  expansion(tmp, 0);
  substitution_box(tmp, 0);
  transposition(tmp, 0);
  src[index + 0] ^= tmp[4];
  src[index + 1] ^= tmp[5];
  src[index + 2] ^= tmp[6];
  src[index + 3] ^= tmp[7];
}

void decrypt_block(uint8_t* src, int index) {
  initial_permutation(src, index);
  round_function(src, index);
  final_permutation(src, index);
}

void shuffle_dec(uint8_t* src, int index) {
  uint8_t tmp[8]{};

  static uint8_t shuffle_dec_table[256]{};
  static std::once_flag once;
  std::call_once(once, [&] {
    constexpr uint8_t list[14]{0x00, 0x2b, 0x6c, 0x80, 0x01, 0x68, 0x48,
                               0x77, 0x60, 0xff, 0xb9, 0xc0, 0xfe, 0xeb};
    for (int i = 0; i < 256; i += 1) {
      shuffle_dec_table[i] = i;
    }
    for (int i = 0; i < 14; i += 2) {
      shuffle_dec_table[list[i + 0]] = list[i + 1];
      shuffle_dec_table[list[i + 1]] = list[i + 0];
    }
  });

  tmp[0] = src[index + 3];
  tmp[1] = src[index + 4];
  tmp[2] = src[index + 6];
  tmp[3] = src[index + 0];
  tmp[4] = src[index + 1];
  tmp[5] = src[index + 2];
  tmp[6] = src[index + 5];
  tmp[7] = shuffle_dec_table[src[index + 7]];
  ::memcpy(src + index, tmp, 8);
}

}  // namespace des

namespace zlib {

void inflate(std::vector<uint8_t>& data, std::vector<uint8_t>& out) {
  z_stream zs{};
  auto ret = inflateInit(&zs);

  zs.avail_in = (unsigned int)data.size();
  zs.next_in = (Bytef*)data.data();

  out.clear();
  do {
    uint8_t buf[65535]{};
    zs.next_out = buf;
    zs.avail_out = sizeof(buf);
    ret = ::inflate(&zs, Z_SYNC_FLUSH);
    assert(ret == Z_OK || ret == Z_STREAM_END);
    auto out_size = sizeof(buf) - zs.avail_out;
    out.insert(out.end(), buf, buf + out_size);
  } while (zs.avail_out == 0);

  ::inflateEnd(&zs);
}

}  // namespace zlib

Grf::Grf(const std::string& path) : path_(path) {
  std::ifstream fs(path, std::ios::binary);

  Header header{};
  fs.read((char*)&header, sizeof(header));

  assert(header.version == 0x200);

  fs.seekg(header.file_table_offset + 46);
  Table table{};
  fs.read((char*)&table, sizeof(table));

  std::vector<uint8_t> buffer(table.pack_size);
  fs.read((char*)buffer.data(), table.pack_size);

  std::vector<uint8_t> dest;
  zlib::inflate(buffer, dest);

  int pos = 0;
  for (int i = 0; i < (int)header.file_count && pos < (int)dest.size(); ++i) {
    std::string filename = "";
    while (dest[pos] != '\0') {
      filename += (char)dest[pos++];
    }
    pos++;

    Entry entry;
    entry.filename = filename;
    entry.pack_size = dest[pos + 0] | dest[pos + 1] << 8 | dest[pos + 2] << 16 | dest[pos + 3] << 24;
    pos += 4;
    entry.length_aligned = dest[pos + 0] | dest[pos + 1] << 8 | dest[pos + 2] << 16 | dest[pos + 3] << 24;
    pos += 4;
    entry.real_size = dest[pos + 0] | dest[pos + 1] << 8 | dest[pos + 2] << 16 | dest[pos + 3] << 24;
    pos += 4;
    entry.type = dest[pos];
    pos++;
    entry.offset = dest[pos + 0] | dest[pos + 1] << 8 | dest[pos + 2] << 16 | dest[pos + 3] << 24;
    pos += 4;
    entries_[filename] = std::move(entry);
  }
}

bool Grf::extract_memory(const std::string& entry_path,
                         std::vector<uint8_t>& dst) {
  auto it = entries_.find(entry_path);
  if (it == entries_.end()) {
    return false;
  }

  const Entry& entry = it->second;
  std::ifstream fs(path_, std::ios::binary);
  fs.seekg(entry.offset + 46);

  if (entry.pack_size == entry.real_size) {
    dst.resize(entry.length_aligned);
    fs.read((char*)dst.data(), entry.length_aligned);
    if (entry.type & 0x02) {
      decode_full(dst, entry.length_aligned, entry.pack_size);
    } else if (entry.type & 0x04) {
      decode_header(dst);
    }
    dst.resize(entry.real_size);
  } else {
    std::vector<uint8_t> pack(entry.length_aligned);
    fs.read((char*)pack.data(), entry.length_aligned);
    if (entry.type & 0x02) {
      decode_full(pack, entry.length_aligned, entry.pack_size);
    } else if (entry.type & 0x04) {
      decode_header(pack);
    }
    zlib::inflate(pack, dst);
  }

  return true;
}

void Grf::decode_full(std::vector<uint8_t>& data,
                      uint32_t length_aligned,
                      uint32_t pack_size) {
  const int length = pack_size;
  const int blocks = length_aligned >> 3;
  const int cycle = (length < 3)   ? 1
                    : (length < 5) ? length + 1
                    : (length < 7) ? length + 9
                                   : length + 15;
  for (int i = 0; i < 20 && i < blocks; i += 1) {
    des::decrypt_block(data.data(), i * 8);
  }
  for (int i = 20, j = 0; i < blocks; i += 1) {
    if (i % cycle == 0) {
      des::decrypt_block(data.data(), i * 8);
      continue;
    }
    if (j == 7) {
      des::shuffle_dec(data.data(), i * 8);
      j = 0;
    }
    j += 1;
  }
}

void Grf::decode_header(std::vector<uint8_t>& data) {
  const int blocks = (int)data.size() >> 3;
  for (int i = 0; i < 20 && i < blocks; i += 1) {
    des::decrypt_block(data.data(), i * 8);
  }
}

Gat::Gat(const uint8_t* data, size_t size) {
  ::memcpy(&header_, data, sizeof(Header));

  const uint8_t* ptr = data;
  ptr += sizeof(Header);

  cells_.resize(header_.height * header_.width);
  for (int y = 0; y < header_.height; ++y) {
    for (int x = 0; x < header_.width; ++x) {
      ::memcpy(&cells_[y * header_.width + x], ptr, sizeof(Cell));
      ptr += sizeof(Cell);
    }
  }
}
