// ref. https://github.com/EmanuelJr/grf-reader/tree/master

#pragma once

#include <string>
#include <map>
#include <vector>

class Grf {
 public:
  Grf() = delete;
  Grf(const std::string& path);

#pragma pack(push, 1)
  struct Header {
    char signature[15] = {};
    char key[15] = {};
    uint32_t file_table_offset = 0;
    uint32_t padding = 0;
    uint32_t file_count = 0;
    uint32_t version = 0;
  };

  struct Table {
    uint32_t pack_size = 0;
    uint32_t real_size = 0;
  };
#pragma pack(pop)

  struct Entry {
    std::string filename = filename;
    uint32_t pack_size = 0;
    uint32_t length_aligned = 0;
    uint32_t real_size = 0;
    uint8_t type = 0;
    uint32_t offset = 0;
  };

  bool extract_memory(const std::string& entry_path, std::vector<uint8_t>& dst);
  void decode_full(std::vector<uint8_t>& data, uint32_t length_aligned, uint32_t pack_size);
  void decode_header(std::vector<uint8_t>& data);

 private:
  std::string path_;
  std::map<std::string, Entry> entries_;
};

class Gat {
 public:
  Gat() = delete;
  Gat(const uint8_t* data, size_t size);

#pragma pack(push, 1)
  struct Header {
    uint8_t magic[4];
    uint8_t major_version;
    uint8_t minor_version;
    int width;
    int height;
  };

  struct Cell {
    float bl;
    float br;
    float tl;
    float tr;
    int type;
  };
#pragma pack(pop)

  const Header& header() const { return header_; }
  const std::vector<Cell>& cells() const { return cells_; }

 private:
  Header header_;
  std::vector<Cell> cells_;
};

