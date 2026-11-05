// fable_2 - Reading and patching the GUI texture banks for keyboard button prompts
//
// A bank is big-endian: a zlib-packed table at 0x11 (its packed size at 0x09)
// lists each entry's name, offset from 0x8000 and size. gui_textures.bnk holds
// the texture bodies and gui_texture_headers.bnk an 84-byte header for each.
// Header words: 1 stored size, 3 flags, 4 width, 5 height, 6 format, 7 mips,
// 20 output format.
//
// The prompts are patched by appending new bodies and repointing the table
// (see PatchBanks for the forms used). Decode()
// is the game's own decoder (sub_82B8E320 and helpers), used to draw keys over
// the buttons on sheets that also hold other art.

#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <zlib-ng.h>

namespace fable2::prompts {

using Bytes = std::vector<uint8_t>;

inline uint32_t ReadU32(std::span<const uint8_t> b, size_t at) {
  return (uint32_t(b[at]) << 24) | (uint32_t(b[at + 1]) << 16) | (uint32_t(b[at + 2]) << 8) |
         uint32_t(b[at + 3]);
}

inline void WriteU32(std::span<uint8_t> b, size_t at, uint32_t value) {
  b[at] = uint8_t(value >> 24);
  b[at + 1] = uint8_t(value >> 16);
  b[at + 2] = uint8_t(value >> 8);
  b[at + 3] = uint8_t(value);
}

// Xenos tiling: the index of texel (x, y) on a surface this wide.
inline int Align32(int value) { return (value + 31) & ~31; }

inline int TiledIndex(int x, int y, int width, int log_bpp) {
  int macro = ((x >> 5) + (y >> 5) * (Align32(width) >> 5)) << (log_bpp + 7);
  int micro = ((x & 7) + ((y & 6) << 2)) << log_bpp;
  int offset = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4);
  return (((offset & ~511) << 3) + ((offset & 448) << 2) + (offset & 63) + ((y & 16) << 7) +
          ((((y & 8) >> 2) + (x >> 3)) & 3) * 64) >>
         log_bpp;
}

struct Image {
  int width = 0;
  int height = 0;
  Bytes rgba;  // straight alpha, top-down
};

// Straight RGBA to the bank's raw layout: tiled big-endian ARGB.
inline Bytes ToRaw(const Image& image) {
  Bytes raw(size_t(Align32(image.width)) * Align32(image.height) * 4);
  for (int y = 0; y < image.height; y++) {
    for (int x = 0; x < image.width; x++) {
      const uint8_t* from = &image.rgba[(size_t(y) * image.width + x) * 4];
      uint8_t* to = &raw[size_t(TiledIndex(x, y, image.width, 2)) * 4];
      to[0] = from[3];
      to[1] = from[0];
      to[2] = from[1];
      to[3] = from[2];
    }
  }
  return raw;
}

namespace encode_detail {

inline uint16_t To565(const float* c) {
  auto q = [](float v, int max) { return int(std::clamp(v, 0.f, 255.f) * max / 255.f + 0.5f); };
  return uint16_t((q(c[0], 31) << 11) | (q(c[1], 63) << 5) | q(c[2], 31));
}

inline void From565(uint16_t c, int* out) {
  out[0] = (c >> 11) * 255 / 31;
  out[1] = ((c >> 5) & 63) * 255 / 63;
  out[2] = (c & 31) * 255 / 31;
}

// One DXT5 block in its usual little-endian byte order.
inline void EncodeBlock(const Image& image, int column, int row, uint8_t* out) {
  float pixels[16][4];
  for (int i = 0; i < 16; i++) {
    const uint8_t* p = &image.rgba[(size_t(row * 4 + i / 4) * image.width + column * 4 + i % 4) * 4];
    for (int k = 0; k < 4; k++) pixels[i][k] = p[k];
  }

  int a0 = 0, a1 = 255;
  for (auto& p : pixels) {
    a0 = std::max(a0, int(p[3]));
    a1 = std::min(a1, int(p[3]));
  }
  int alphas[8];
  alphas[0] = a0;
  alphas[1] = a1;
  for (int i = 0; i < 6; i++) alphas[i + 2] = ((6 - i) * a0 + (i + 1) * a1) / 7;
  uint64_t alpha_bits = 0;
  for (int i = 0; i < 16 && a0 != a1; i++) {
    int best = 0;
    for (int k = 1; k < 8; k++)
      if (std::abs(alphas[k] - int(pixels[i][3])) < std::abs(alphas[best] - int(pixels[i][3]))) best = k;
    alpha_bits |= uint64_t(best) << (3 * i);
  }
  out[0] = uint8_t(a0);
  out[1] = uint8_t(a1);
  for (int i = 0; i < 6; i++) out[2 + i] = uint8_t(alpha_bits >> (8 * i));

  // Colour endpoints at the ends of the block's main axis, ignoring clear pixels.
  float mean[3] = {}, weight = 0;
  for (auto& p : pixels) {
    if (p[3] == 0) continue;
    for (int k = 0; k < 3; k++) mean[k] += p[k];
    weight++;
  }
  float lo[3] = {}, hi[3] = {};
  if (weight > 0) {
    for (float& m : mean) m /= weight;
    float cov[6] = {};
    for (auto& p : pixels) {
      if (p[3] == 0) continue;
      float d[3] = {p[0] - mean[0], p[1] - mean[1], p[2] - mean[2]};
      cov[0] += d[0] * d[0]; cov[1] += d[0] * d[1]; cov[2] += d[0] * d[2];
      cov[3] += d[1] * d[1]; cov[4] += d[1] * d[2]; cov[5] += d[2] * d[2];
    }
    float axis[3] = {1, 1, 1};
    for (int it = 0; it < 8; it++) {
      float n[3] = {cov[0] * axis[0] + cov[1] * axis[1] + cov[2] * axis[2],
                    cov[1] * axis[0] + cov[3] * axis[1] + cov[4] * axis[2],
                    cov[2] * axis[0] + cov[4] * axis[1] + cov[5] * axis[2]};
      float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
      if (len < 1e-6f) break;
      for (int k = 0; k < 3; k++) axis[k] = n[k] / len;
    }
    float min_t = 1e9f, max_t = -1e9f;
    for (auto& p : pixels) {
      if (p[3] == 0) continue;
      float t = (p[0] - mean[0]) * axis[0] + (p[1] - mean[1]) * axis[1] + (p[2] - mean[2]) * axis[2];
      min_t = std::min(min_t, t);
      max_t = std::max(max_t, t);
    }
    for (int k = 0; k < 3; k++) {
      lo[k] = mean[k] + axis[k] * min_t;
      hi[k] = mean[k] + axis[k] * max_t;
    }
  }
  uint16_t c0 = To565(hi), c1 = To565(lo);
  if (c0 < c1) std::swap(c0, c1);
  int palette[4][3];
  From565(c0, palette[0]);
  From565(c1, palette[1]);
  for (int k = 0; k < 3; k++) {
    palette[2][k] = (2 * palette[0][k] + palette[1][k]) / 3;
    palette[3][k] = (palette[0][k] + 2 * palette[1][k]) / 3;
  }
  uint32_t indices = 0;
  for (int i = 0; i < 16 && c0 != c1; i++) {
    int best = 0;
    float best_error = 1e9f;
    for (int k = 0; k < 4; k++) {
      float error = 0;
      for (int ch = 0; ch < 3; ch++) error += (palette[k][ch] - pixels[i][ch]) * (palette[k][ch] - pixels[i][ch]);
      if (error < best_error) {
        best_error = error;
        best = k;
      }
    }
    indices |= uint32_t(best) << (2 * i);
  }
  out[8] = uint8_t(c0);
  out[9] = uint8_t(c0 >> 8);
  out[10] = uint8_t(c1);
  out[11] = uint8_t(c1 >> 8);
  for (int i = 0; i < 4; i++) out[12 + i] = uint8_t(indices >> (8 * i));
}

}  // namespace encode_detail

// A body the game reads as stored DXT5 blocks: one {7, 48, length} section,
// then the blocks row by row with their bytes swapped in pairs.
inline Bytes ToStoredDxt5(const Image& image) {
  const int columns = image.width / 4, rows = image.height / 4;
  const size_t length = size_t(columns) * rows * 16;
  Bytes body(48 + length, 0);
  WriteU32(body, 0, 7);
  WriteU32(body, 4, 48);
  WriteU32(body, 8, uint32_t(length));
  uint8_t block[16];
  for (int row = 0; row < rows; row++) {
    for (int column = 0; column < columns; column++) {
      encode_detail::EncodeBlock(image, column, row, block);
      size_t at = 48 + (size_t(row) * columns + column) * 16;
      for (int i = 0; i < 16; i++) body[at + (i ^ 1)] = block[i];
    }
  }
  return body;
}

struct BankEntry {
  size_t toc_position = 0;
  uint32_t offset = 0;
  uint32_t size = 0;
};

inline constexpr size_t kBaseOffset = 0x8000;
inline constexpr size_t kTocSizeAt = 0x09;
inline constexpr size_t kTocAt = 0x11;

inline std::optional<Bytes> ReadToc(std::span<const uint8_t> bank) {
  if (bank.size() < kBaseOffset) return std::nullopt;
  uint32_t packed = ReadU32(bank, kTocSizeAt);
  if (packed < 2 || kTocAt + packed > bank.size()) return std::nullopt;
  // Raw inflate after the two-byte zlib header: some banks' streams have no checksum tail.
  zng_stream z{};
  if (zng_inflateInit2(&z, -15) != Z_OK) return std::nullopt;
  Bytes out;
  uint8_t chunk[65536];
  z.next_in = bank.data() + kTocAt + 2;
  z.avail_in = packed - 2;
  int status = Z_OK;
  while (status == Z_OK) {
    z.next_out = chunk;
    z.avail_out = sizeof(chunk);
    status = zng_inflate(&z, Z_NO_FLUSH);
    out.insert(out.end(), chunk, chunk + (sizeof(chunk) - z.avail_out));
    if (status == Z_BUF_ERROR && z.avail_in == 0) status = Z_STREAM_END;
  }
  zng_inflateEnd(&z);
  if (status != Z_STREAM_END) return std::nullopt;
  return out;
}

inline std::optional<std::map<std::string, BankEntry>> ReadEntries(std::span<const uint8_t> bank) {
  auto toc = ReadToc(bank);
  if (!toc || toc->size() < 4) return std::nullopt;
  std::map<std::string, BankEntry> entries;
  uint32_t count = ReadU32(*toc, 0);
  size_t position = 4;
  for (uint32_t i = 0; i < count; i++) {
    if (position + 4 > toc->size()) return std::nullopt;
    uint32_t name_length = ReadU32(*toc, position);
    position += 4;
    if (position + name_length + 8 > toc->size()) return std::nullopt;
    std::string name(reinterpret_cast<const char*>(toc->data() + position), name_length);
    while (!name.empty() && name.back() == '\0') name.pop_back();
    position += name_length;
    entries[name] = {position, ReadU32(*toc, position), ReadU32(*toc, position + 4)};
    position += 8;
  }
  return entries;
}

inline std::optional<std::span<const uint8_t>> EntryBytes(std::span<const uint8_t> bank,
                                                          const BankEntry& entry) {
  if (kBaseOffset + entry.offset + size_t(entry.size) > bank.size()) return std::nullopt;
  return bank.subspan(kBaseOffset + entry.offset, entry.size);
}

inline constexpr const char* kSheetEntry = "Art\\GUI\\Persistant\\abyx.tex";
inline constexpr const char* kRawModelEntry = "Art\\GUI\\Frames\\vignette256.tex";
inline constexpr size_t kHeaderStoredSizeAt = 4;
inline constexpr size_t kHeaderWidthAt = 16;
inline constexpr size_t kHeaderHeightAt = 20;
inline constexpr size_t kHeaderFormatAt = 24;
inline constexpr uint32_t kFormatArgb = 0x02;
inline constexpr uint32_t kFormatDxt5 = 0x27;

struct PatchedBanks {
  Bytes textures;
  Bytes headers;
};

// Replace the named textures, each in the cheapest form the game reads:
//   - DXT5 textures become stored DXT5 blocks and keep their own header, so
//     they take no more memory than before;
//   - raw ARGB textures of the same size are overwritten in place;
//   - anything else becomes raw ARGB with the header of a raw texture.
// Textures the data doesn't have are left out; a missing abyx sheet or raw
// model means unsupported data.
inline std::optional<PatchedBanks> PatchBanks(std::span<const uint8_t> textures, std::span<const uint8_t> headers,
                                              const std::map<std::string, Image>& images) {
  auto entries = ReadEntries(textures);
  auto header_entries = ReadEntries(headers);
  auto toc = ReadToc(textures);
  if (!entries || !header_entries || !toc || !entries->contains(kSheetEntry) || !images.contains(kSheetEntry)) {
    return std::nullopt;
  }
  auto model = header_entries->find(kRawModelEntry);
  if (model == header_entries->end() || model->second.size < 84) return std::nullopt;
  const auto model_header = headers.subspan(kBaseOffset + model->second.offset, model->second.size);

  PatchedBanks out;
  out.headers.assign(headers.begin(), headers.end());
  struct Body {
    size_t at;
    Bytes bytes;
  };
  std::vector<Body> appended, in_place;
  size_t end = textures.size();
  for (const auto& [name, image] : images) {
    auto entry = entries->find(name);
    auto header_entry = header_entries->find(name);
    if (entry == entries->end() || header_entry == header_entries->end() || header_entry->second.size < 84) continue;
    const size_t header_at = kBaseOffset + header_entry->second.offset;
    const auto header = headers.subspan(header_at, header_entry->second.size);
    const bool same_size = ReadU32(header, kHeaderWidthAt) == uint32_t(image.width) &&
                           ReadU32(header, kHeaderHeightAt) == uint32_t(image.height);
    const uint32_t format = ReadU32(header, kHeaderFormatAt);
    const size_t raw_bytes = size_t(Align32(image.width)) * Align32(image.height) * 4;
    const size_t body_at = kBaseOffset + entry->second.offset;

    Bytes body;
    if (same_size && format == kFormatDxt5 && image.width % 4 == 0 && image.height % 4 == 0) {
      body = ToStoredDxt5(image);
    } else if (same_size && format == kFormatArgb && entry->second.size == raw_bytes + 4 &&
               body_at + 4 <= textures.size() && ReadU32(textures, body_at) == raw_bytes) {
      in_place.push_back({body_at + 4, ToRaw(image)});
      continue;
    } else if (image.width == image.height && header_entry->second.size == model_header.size()) {
      body.resize(4);
      WriteU32(body, 0, uint32_t(raw_bytes));
      Bytes raw = ToRaw(image);
      body.insert(body.end(), raw.begin(), raw.end());
      std::copy(model_header.begin(), model_header.end(), out.headers.begin() + header_at);
      WriteU32(out.headers, header_at + kHeaderWidthAt, uint32_t(image.width));
      WriteU32(out.headers, header_at + kHeaderHeightAt, uint32_t(image.height));
    } else {
      continue;
    }
    WriteU32(out.headers, header_at + kHeaderStoredSizeAt, uint32_t(body.size()));
    const size_t at = (end + 15) & ~size_t(15);
    WriteU32(*toc, entry->second.toc_position, uint32_t(at - kBaseOffset));
    WriteU32(*toc, entry->second.toc_position + 4, uint32_t(body.size()));
    end = at + body.size();
    appended.push_back({at, std::move(body)});
  }

  size_t packed_size = zng_compressBound(toc->size());
  Bytes packed(packed_size);
  if (zng_compress2(packed.data(), &packed_size, toc->data(), toc->size(), 9) != Z_OK) return std::nullopt;
  if (kTocAt + packed_size > kBaseOffset) return std::nullopt;

  out.textures.assign(end, 0);
  std::copy(textures.begin(), textures.end(), out.textures.begin());
  std::fill(out.textures.begin() + kTocAt, out.textures.begin() + kBaseOffset, uint8_t(0));
  std::copy_n(packed.begin(), packed_size, out.textures.begin() + kTocAt);
  WriteU32(out.textures, kTocSizeAt, uint32_t(packed_size));
  for (const auto* list : {&appended, &in_place})
    for (const Body& b : *list) std::copy(b.bytes.begin(), b.bytes.end(), out.textures.begin() + b.at);
  return out;
}

namespace codec_detail {

struct BlockMode {
  int8_t mode, dx, dy;
};
struct ColourDelta {
  int8_t r, g, b;
};

// The game's fixed tables, from its image at 0x83316898 and 0x833162E0.
// Block prediction: mode (4 means explicit colours), then the source block's offset.
inline constexpr BlockMode kBlockModes[62] = {
    {0, -1, 0},  {0, -2, 0},  {0, -3, 0},  {0, -4, 0},  {0, -5, 0},  {0, 0, -1},  {0, -1, -1}, {0, -2, -1},
    {0, -3, -1}, {0, -4, -1}, {0, -5, -1}, {0, 0, -2},  {0, -1, -2}, {0, -2, -2}, {0, -3, -2}, {0, 0, -3},
    {0, -1, -3}, {0, -2, -3}, {0, 0, -4},  {0, -1, -4}, {0, 0, -5},  {0, -1, -5}, {1, -1, 0},  {1, -2, 0},
    {1, -3, 0},  {1, -4, 0},  {1, 0, -1},  {1, -1, -1}, {1, -2, -1}, {1, -3, -1}, {1, -4, -1}, {1, 0, -2},
    {1, -1, -2}, {1, -2, -2}, {1, 0, -3},  {1, -1, -3}, {1, 0, -4},  {1, -1, -4}, {2, -1, 0},  {2, -2, 0},
    {2, -3, 0},  {2, -4, 0},  {2, 0, -1},  {2, -1, -1}, {2, -2, -1}, {2, -3, -1}, {2, -4, -1}, {2, 0, -2},
    {2, -1, -2}, {2, -2, -2}, {2, 0, -3},  {2, -1, -3}, {2, 0, -4},  {2, -1, -4}, {3, -1, 0},  {3, -2, 0},
    {3, 0, -1},  {3, -1, -1}, {3, -2, -1}, {3, 0, -2},  {3, -1, -2}, {4, 0, 0},
};

// Changes applied to a predicted RGB565 colour.
inline constexpr ColourDelta kColourDeltas[122] = {
    {-3, -5, -3}, {-3, -5, -2}, {-3, -5, -1}, {-3, -3, -3}, {-3, -3, -2}, {-3, -3, -1}, {-2, -5, -3}, {-2, -5, -2},
    {-2, -5, -1}, {-2, -5, 0},  {-2, -3, -3}, {-2, -3, -2}, {-2, -3, -1}, {-2, -3, 0},  {-2, -1, -2}, {-2, -1, -1},
    {-2, -1, 0},  {-2, 0, -2},  {-2, 0, -1},  {-2, 0, 0},   {-2, 1, -2},  {-2, 1, -1},  {-2, 1, 0},   {-1, -5, -3},
    {-1, -5, -2}, {-1, -5, -1}, {-1, -5, 0},  {-1, -3, -3}, {-1, -3, -2}, {-1, -3, -1}, {-1, -3, 0},  {-1, -3, 1},
    {-1, -1, -2}, {-1, -1, -1}, {-1, -1, 0},  {-1, -1, 1},  {-1, 0, -2},  {-1, 0, -1},  {-1, 0, 0},   {-1, 0, 1},
    {-1, 1, -2},  {-1, 1, -1},  {-1, 1, 0},   {-1, 1, 1},   {-1, 3, -1},  {-1, 3, 0},   {-1, 3, 1},   {0, -5, -2},
    {0, -5, -1},  {0, -5, 0},   {0, -3, -2},  {0, -3, -1},  {0, -3, 0},   {0, -3, 1},   {0, -1, -2},  {0, -1, -1},
    {0, -1, 0},   {0, -1, 1},   {0, -1, 2},   {0, 0, -2},   {0, 0, -1},   {0, 0, 1},    {0, 0, 2},    {0, 1, -2},
    {0, 1, -1},   {0, 1, 0},    {0, 1, 1},    {0, 1, 2},    {0, 3, -1},   {0, 3, 0},    {0, 3, 1},    {0, 3, 2},
    {0, 5, 0},    {0, 5, 1},    {0, 5, 2},    {1, -3, -1},  {1, -3, 0},   {1, -3, 1},   {1, -1, -1},  {1, -1, 0},
    {1, -1, 1},   {1, -1, 2},   {1, 0, -1},   {1, 0, 0},    {1, 0, 1},    {1, 0, 2},    {1, 1, -1},   {1, 1, 0},
    {1, 1, 1},    {1, 1, 2},    {1, 3, -1},   {1, 3, 0},    {1, 3, 1},    {1, 3, 2},    {1, 3, 3},    {1, 5, 0},
    {1, 5, 1},    {1, 5, 2},    {1, 5, 3},    {2, -1, 0},   {2, -1, 1},   {2, -1, 2},   {2, 0, 0},    {2, 0, 1},
    {2, 0, 2},    {2, 1, 0},    {2, 1, 1},    {2, 1, 2},    {2, 3, 0},    {2, 3, 1},    {2, 3, 2},    {2, 3, 3},
    {2, 5, 0},    {2, 5, 1},    {2, 5, 2},    {2, 5, 3},    {3, 3, 1},    {3, 3, 2},    {3, 3, 3},    {3, 5, 1},
    {3, 5, 2},    {3, 5, 3},
};

// 32-bit big-endian words, read from each word's lowest bit up.
class BitReader {
 public:
  BitReader(std::span<const uint8_t> data, size_t start) : data_(data), start_(start) {}

  uint32_t Read(int count) {
    size_t word = size_t(position_ >> 5);
    int shift = int(position_ & 31);
    position_ += count;
    uint64_t joined = uint64_t(Word(word)) | (uint64_t(Word(word + 1)) << 32);
    return uint32_t((joined >> shift) & ((uint64_t(1) << count) - 1));
  }

  uint32_t Bit() {
    size_t word = size_t(position_ >> 5);
    int shift = int(position_ & 31);
    position_++;
    return (Word(word) >> shift) & 1;
  }

 private:
  uint32_t Word(size_t index) const {
    size_t at = start_ + index * 4;
    return at + 4 <= data_.size() ? ReadU32(data_, at) : 0;
  }

  std::span<const uint8_t> data_;
  size_t start_;
  uint64_t position_ = 0;
};

// Built exactly as the game builds it: the codes depend on how its heap breaks
// ties between equal weights.
class Huffman {
 public:
  explicit Huffman(const std::vector<uint32_t>& frequencies) {
    leaves_ = int(frequencies.size());
    int capacity = leaves_ * 2;
    std::vector<uint32_t> weights(capacity);
    child0_.assign(capacity, 0);
    child1_.assign(capacity, 0);
    std::vector<int> heap(capacity);
    int size = 0, nodes = leaves_;
    for (int i = 0; i < leaves_; i++) {
      weights[i] = frequencies[i];
      Push(heap, size, weights, i);
    }
    while (size > 1) {
      int a = Pop(heap, size, weights);
      int b = Pop(heap, size, weights);
      child0_[nodes] = a;
      child1_[nodes] = b;
      weights[nodes] = weights[a] + weights[b];
      Push(heap, size, weights, nodes);
      nodes++;
    }
    root_ = heap[0];
  }

  int Next(BitReader& bits) const {
    int node = root_;
    while (node >= leaves_) node = bits.Bit() == 0 ? child0_[node] : child1_[node];
    return node;
  }

 private:
  static void Push(std::vector<int>& heap, int& size, const std::vector<uint32_t>& weights, int value) {
    heap[size] = value;
    int i = size++;
    while (i != 0) {
      int parent = (i - 1) >> 1;
      if (weights[heap[parent]] < weights[heap[i]]) break;
      std::swap(heap[parent], heap[i]);
      i = parent;
    }
  }

  static int Pop(std::vector<int>& heap, int& size, const std::vector<uint32_t>& weights) {
    int top = heap[0];
    heap[0] = heap[size - 1];
    size--;
    int i = 0;
    while (true) {
      int a = 2 * i + 1, b = 2 * i + 2;
      if (b < size) {
        uint32_t wi = weights[heap[i]], wa = weights[heap[a]], wb = weights[heap[b]];
        if (wi < wa && wi < wb) break;
        int next = wb < wa ? b : a;
        std::swap(heap[i], heap[next]);
        i = next;
      } else {
        if (a < size && weights[heap[i]] >= weights[heap[a]]) std::swap(heap[i], heap[a]);
        break;
      }
    }
    return top;
  }

  std::vector<int> child0_, child1_;
  int root_ = 0, leaves_ = 0;
};

inline std::vector<uint32_t> ReadFrequencies(BitReader& bits, int count) {
  std::vector<uint32_t> frequencies(count);
  for (int i = 0; i < count; i++) {
    uint32_t value = bits.Read(8);
    frequencies[i] = (value & 15) << (value >> 4);
  }
  return frequencies;
}

inline uint16_t AddDelta(uint16_t colour, int symbol) {
  const ColourDelta& d = kColourDeltas[symbol];
  int r = std::clamp((colour >> 11) + d.r, 0, 31);
  int g = std::clamp(((colour >> 5) & 63) + d.g, 0, 63);
  int b = std::clamp((colour & 31) + d.b, 0, 31);
  return uint16_t((r << 11) | (g << 5) | b);
}

inline void PaintColour(Image& image, int column, int row, uint16_t c0, uint16_t c1, uint32_t indices,
                        bool one_bit_alpha) {
  int r[4], g[4], b[4];
  r[0] = (c0 >> 11) * 255 / 31; g[0] = ((c0 >> 5) & 63) * 255 / 63; b[0] = (c0 & 31) * 255 / 31;
  r[1] = (c1 >> 11) * 255 / 31; g[1] = ((c1 >> 5) & 63) * 255 / 63; b[1] = (c1 & 31) * 255 / 31;
  bool three_colour = one_bit_alpha && c0 <= c1;
  if (three_colour) {
    r[2] = (r[0] + r[1]) / 2; g[2] = (g[0] + g[1]) / 2; b[2] = (b[0] + b[1]) / 2;
    r[3] = g[3] = b[3] = 0;
  } else {
    r[2] = (2 * r[0] + r[1]) / 3; g[2] = (2 * g[0] + g[1]) / 3; b[2] = (2 * b[0] + b[1]) / 3;
    r[3] = (r[0] + 2 * r[1]) / 3; g[3] = (g[0] + 2 * g[1]) / 3; b[3] = (b[0] + 2 * b[1]) / 3;
  }
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      int px = column * 4 + x, py = row * 4 + y;
      if (px >= image.width || py >= image.height) continue;
      int i = int(indices >> (2 * (y * 4 + x))) & 3;
      uint8_t* p = &image.rgba[(size_t(py) * image.width + px) * 4];
      p[0] = uint8_t(r[i]);
      p[1] = uint8_t(g[i]);
      p[2] = uint8_t(b[i]);
      p[3] = three_colour && i == 3 ? 0 : 255;
    }
  }
}

inline void DecodeColour(std::span<const uint8_t> body, size_t at, Image& image, bool one_bit_alpha,
                         bool sort) {
  BitReader bits(body, at);
  int width = int(bits.Read(16)), height = int(bits.Read(16));
  Huffman index_tree(ReadFrequencies(bits, 256));
  Huffman mode_tree(ReadFrequencies(bits, 62));
  Huffman delta_tree(ReadFrequencies(bits, 122));

  int columns = width >> 2, rows = height >> 2;
  std::vector<uint16_t> c0s(size_t(columns) * rows), c1s(size_t(columns) * rows);
  for (int row = 0; row < rows; row++) {
    for (int column = 0; column < columns; column++) {
      const BlockMode& m = kBlockModes[mode_tree.Next(bits)];
      uint16_t c0, c1;
      if (m.mode == 4) {
        c0 = uint16_t(bits.Read(16));
        c1 = uint16_t(bits.Read(16));
      } else {
        int from = (column + m.dx) + (row + m.dy) * columns;
        c0 = c0s[from];
        c1 = c1s[from];
        if (m.mode == 1 || m.mode == 3) c0 = AddDelta(c0, delta_tree.Next(bits));
        if (m.mode == 2 || m.mode == 3) c1 = AddDelta(c1, delta_tree.Next(bits));
        if (sort && c0 < c1) std::swap(c0, c1);
      }
      c0s[column + row * columns] = c0;
      c1s[column + row * columns] = c1;

      uint8_t v[4] = {};
      if (!(sort && c0 == c1)) {
        for (int i = 0; i < 4; i++) v[i] = uint8_t(index_tree.Next(bits));
      }
      uint8_t row0 = uint8_t((v[0] & 15) | ((v[1] & 15) << 4));
      uint8_t row1 = uint8_t((v[1] & 0xF0) | (v[0] >> 4));
      uint8_t row2 = uint8_t((v[2] & 15) | ((v[3] & 15) << 4));
      uint8_t row3 = uint8_t((v[3] & 0xF0) | (v[2] >> 4));
      PaintColour(image, column, row, c0, c1,
                  uint32_t(row0) | (uint32_t(row1) << 8) | (uint32_t(row2) << 16) | (uint32_t(row3) << 24),
                  one_bit_alpha);
    }
  }
}

inline int To8(int six) { return int(std::floor(six * 255.0 / 63.0 + 0.5)); }

inline Bytes DecodeAlpha(std::span<const uint8_t> body, size_t at, int image_width, int image_height) {
  BitReader bits(body, at);
  int width = int(bits.Read(16)), height = int(bits.Read(16));
  uint32_t four_bit = bits.Read(4);
  bits.Read(8);
  Huffman high_tree(ReadFrequencies(bits, 64));
  Huffman spread_tree(ReadFrequencies(bits, 32));
  Huffman index_tree(ReadFrequencies(bits, 64));
  Huffman run_tree(ReadFrequencies(bits, 32));

  Bytes plane(size_t(image_width) * image_height);
  auto set = [&](int x, int y, int value) {
    if (x < image_width && y < image_height) plane[size_t(y) * image_width + x] = uint8_t(value);
  };
  int columns = width >> 2, rows = height >> 2;
  int run = 0, mode = -1, constant = 0;
  int palette[8];
  for (int row = 0; row < rows; row++) {
    for (int column = 0; column < columns; column++) {
      if (run <= 0) {
        int symbol = run_tree.Next(bits);
        int extra = symbol & 7;
        mode = symbol >> 3;
        run = (1 << extra) + (extra > 0 ? int(bits.Read(extra)) : 0);
        constant = 0;
        if (mode == 2) constant = four_bit == 1 ? int(bits.Read(4)) * 17 : int(bits.Read(8));
      }
      run--;

      if (mode <= 2) {
        int value = mode == 0 ? 0 : mode == 1 ? 255 : constant;
        for (int y = 0; y < 4; y++)
          for (int x = 0; x < 4; x++) set(column * 4 + x, row * 4 + y, value);
        continue;
      }

      int middle = high_tree.Next(bits);
      int spread = spread_tree.Next(bits);
      int a0 = To8(std::clamp(middle + spread, 0, 63));
      int a1 = To8(std::clamp(middle - spread, 0, 63));
      palette[0] = a0;
      palette[1] = a1;
      palette[2] = (6 * a0 + a1 + 3) / 7;
      palette[3] = (5 * a0 + 2 * a1 + 3) / 7;
      palette[4] = (4 * a0 + 3 * (a1 + 1)) / 7;
      palette[5] = (3 * (a0 + 1) + 4 * a1) / 7;
      palette[6] = (5 * a1 + 2 * a0 + 3) / 7;
      palette[7] = (6 * a1 + a0 + 3) / 7;
      for (int y = 0; y < 4; y++) {
        int s0 = index_tree.Next(bits), s1 = index_tree.Next(bits);
        set(column * 4, row * 4 + y, palette[s0 & 7]);
        set(column * 4 + 1, row * 4 + y, palette[s0 >> 3]);
        set(column * 4 + 2, row * 4 + y, palette[s1 & 7]);
        set(column * 4 + 3, row * 4 + y, palette[s1 >> 3]);
      }
    }
  }
  return plane;
}

inline void FillAlphaPalette(int a0, int a1, int* palette) {
  palette[0] = a0;
  palette[1] = a1;
  if (a0 > a1) {
    for (int i = 0; i < 6; i++) palette[i + 2] = ((6 - i) * a0 + (i + 1) * a1) / 7;
  } else {
    for (int i = 0; i < 4; i++) palette[i + 2] = ((4 - i) * a0 + (i + 1) * a1) / 5;
    palette[6] = 0;
    palette[7] = 255;
  }
}

inline void PaintStoredBlock(std::span<const uint8_t> body, size_t at, int stride, Image& image, int column,
                             int row) {
  uint8_t block[16] = {};
  for (int i = 0; i < stride; i++) block[i] = body[at + (i ^ 1)];
  int colour_at = stride == 16 ? 8 : 0;
  uint16_t c0 = uint16_t(block[colour_at] | (block[colour_at + 1] << 8));
  uint16_t c1 = uint16_t(block[colour_at + 2] | (block[colour_at + 3] << 8));
  uint32_t indices = uint32_t(block[colour_at + 4]) | (uint32_t(block[colour_at + 5]) << 8) |
                     (uint32_t(block[colour_at + 6]) << 16) | (uint32_t(block[colour_at + 7]) << 24);
  PaintColour(image, column, row, c0, c1, indices, stride == 8);
  if (stride != 16) return;

  int palette[8];
  FillAlphaPalette(block[0], block[1], palette);
  uint64_t alpha_bits = 0;
  for (int i = 5; i >= 0; i--) alpha_bits = (alpha_bits << 8) | block[2 + i];
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      int px = column * 4 + x, py = row * 4 + y;
      if (px >= image.width || py >= image.height) continue;
      image.rgba[(size_t(py) * image.width + px) * 4 + 3] =
          uint8_t(palette[int(alpha_bits >> (3 * (y * 4 + x))) & 7]);
    }
  }
}

inline std::optional<Image> DecodeRaw(std::span<const uint8_t> body, std::span<const uint8_t> header) {
  if (header.size() < 84) return std::nullopt;
  int width = int(ReadU32(header, 16)), height = int(ReadU32(header, 20));
  uint32_t format = ReadU32(header, 24);
  size_t bytes = ReadU32(body, 0);
  Image image{width, height, Bytes(size_t(width) * height * 4)};
  auto area = [](int w, int h) { return size_t(Align32(w)) * size_t(Align32(h)); };
  if (bytes == area(width, height) * 4) {
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        const uint8_t* from = &body[4 + size_t(TiledIndex(x, y, width, 2)) * 4];
        uint8_t* to = &image.rgba[(size_t(y) * width + x) * 4];
        to[0] = from[1];
        to[1] = from[2];
        to[2] = from[3];
        to[3] = from[0];
      }
    }
    return image;
  }
  int columns = width / 4, rows = height / 4;
  int stride = format == 0x28 ? 0
               : bytes == area(columns, rows) * 8  ? 8
               : bytes == area(columns, rows) * 16 ? 16
                                                   : 0;
  if (stride == 0) return std::nullopt;
  for (int row = 0; row < rows; row++)
    for (int column = 0; column < columns; column++)
      PaintStoredBlock(body, 4 + size_t(TiledIndex(column, row, columns, stride == 8 ? 3 : 4)) * stride, stride,
                       image, column, row);
  return image;
}

}  // namespace codec_detail

// Decode a body with its 84-byte header. Only the kinds GUI art uses: sections,
// or raw ARGB and DXT. Returns nothing for anything else.
inline std::optional<Image> Decode(std::span<const uint8_t> body, std::span<const uint8_t> header) {
  using namespace codec_detail;
  constexpr uint32_t kColour = 1, kAlpha3 = 2, kAlpha5 = 3, kSolid = 6, kBlocks = 7, kColourPunch = 11;
  if (body.size() < 48 || header.size() < 84) return std::nullopt;
  if (uint32_t size = ReadU32(body, 0); size >= 0x100 && size <= body.size() - 4) return DecodeRaw(body, header);

  struct Section {
    uint32_t type, a, b;
  };
  std::vector<Section> sections;
  for (int i = 0; i < 4; i++) {
    uint32_t type = ReadU32(body, i * 12);
    if (type == 0) break;
    sections.push_back({type, ReadU32(body, i * 12 + 4), ReadU32(body, i * 12 + 8)});
  }
  if (sections.empty()) return std::nullopt;

  int width = int(ReadU32(header, 16)), height = int(ReadU32(header, 20));
  for (const Section& s : sections) {
    if (s.type == kColour || s.type == kColourPunch || s.type == kAlpha3 || s.type == kAlpha5) {
      if (s.a >= body.size()) return std::nullopt;
      BitReader bits(body, s.a);
      width = int(bits.Read(16));
      height = int(bits.Read(16));
      break;
    }
  }
  if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return std::nullopt;

  const uint32_t output_format = ReadU32(header, 80);
  const bool one_bit_alpha = output_format == 0 || output_format == 12;
  Image image{width, height, Bytes(size_t(width) * height * 4)};
  bool colour_done = false;
  std::vector<Bytes> planes;
  for (const Section& s : sections) {
    if (s.a >= body.size() && s.type != kSolid) return std::nullopt;
    switch (s.type) {
      case kColour:
      case kColourPunch:
        DecodeColour(body, s.a, image, one_bit_alpha, s.type == kColour);
        colour_done = true;
        break;
      case kBlocks: {
        int stride = ReadU32(header, 24) == 0x23 ? 8 : 16;
        int columns = (width + 3) / 4, rows = (height + 3) / 4;
        if (s.a + size_t(columns) * rows * stride > body.size()) return std::nullopt;
        for (int row = 0; row < rows; row++)
          for (int column = 0; column < columns; column++)
            PaintStoredBlock(body, s.a + (size_t(row) * columns + column) * stride, stride, image, column, row);
        colour_done = true;
        break;
      }
      case kSolid:
        if (!colour_done && planes.empty() &&
            !(output_format == 3 || (output_format >= 4 && output_format < 12))) {
          for (size_t p = 0; p < image.rgba.size(); p += 4) {
            image.rgba[p] = uint8_t(s.a >> 16);
            image.rgba[p + 1] = uint8_t(s.a >> 8);
            image.rgba[p + 2] = uint8_t(s.a);
            image.rgba[p + 3] = 255;
          }
          colour_done = true;
        } else {
          planes.emplace_back(size_t(width) * height, uint8_t(s.a));
        }
        break;
      case kAlpha3:
      case kAlpha5:
        planes.push_back(DecodeAlpha(body, s.a, width, height));
        break;
      default:
        return std::nullopt;
    }
  }

  const size_t pixels = size_t(width) * height;
  if (colour_done) {
    if (!planes.empty())
      for (size_t p = 0; p < pixels; p++) image.rgba[p * 4 + 3] = planes[0][p];
    return image;
  }
  // No colour section: channels on their own.
  for (size_t p = 0; p < pixels; p++) {
    uint8_t r = planes[0][p];
    uint8_t g = planes.size() > 1 ? planes[1][p] : r;
    uint8_t b = planes.size() > 2 ? planes[2][p] : planes.size() > 1 ? 255 : r;
    image.rgba[p * 4] = r;
    image.rgba[p * 4 + 1] = g;
    image.rgba[p * 4 + 2] = b;
    image.rgba[p * 4 + 3] = planes.size() > 3 ? planes[3][p] : 255;
  }
  return image;
}

// A texture's stored body and header, or nothing if the banks lack it.
inline std::optional<Image> DecodeEntry(std::span<const uint8_t> textures, std::span<const uint8_t> headers,
                                        const std::string& name) {
  auto entries = ReadEntries(textures);
  auto header_entries = ReadEntries(headers);
  if (!entries || !header_entries) return std::nullopt;
  auto body = entries->find(name);
  auto header = header_entries->find(name);
  if (body == entries->end() || header == header_entries->end()) return std::nullopt;
  auto body_bytes = EntryBytes(textures, body->second);
  auto header_bytes = EntryBytes(headers, header->second);
  if (!body_bytes || !header_bytes) return std::nullopt;
  return Decode(*body_bytes, *header_bytes);
}

}  // namespace fable2::prompts
