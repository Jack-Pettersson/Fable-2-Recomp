// fable_2 - Drawing keyboard keys for the in-game button prompts (see button_prompt_art.h)

#include "button_prompt_art.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <imstb_truetype.h>

namespace fable2::prompts {
namespace {

constexpr int kCellSize = 64;

struct Colour {
  float r, g, b, a;
};

Colour Rgb(int r, int g, int b, double shade = 1.0) {
  return {float(int(r * shade)) / 255.f, float(int(g * shade)) / 255.f, float(int(b * shade)) / 255.f, 1.f};
}

// Premultiplied RGBA, top-down.
struct Canvas {
  int width, height;
  std::vector<float> pixels;

  Canvas(int w, int h) : width(w), height(h), pixels(size_t(w) * h * 4, 0.f) {}

  void Blend(int x, int y, Colour c, float coverage) {
    if (x < 0 || y < 0 || x >= width || y >= height || coverage <= 0.f) return;
    float a = c.a * coverage;
    float* p = &pixels[(size_t(y) * width + x) * 4];
    p[0] = c.r * a + p[0] * (1 - a);
    p[1] = c.g * a + p[1] * (1 - a);
    p[2] = c.b * a + p[2] * (1 - a);
    p[3] = a + p[3] * (1 - a);
  }

  Bytes ToStraightRgba() const {
    Bytes out(size_t(width) * height * 4);
    for (size_t p = 0; p < out.size(); p += 4) {
      float a = pixels[p + 3];
      for (int i = 0; i < 3; i++)
        out[p + i] = a > 0 ? uint8_t(std::clamp(pixels[p + i] / a, 0.f, 1.f) * 255.f + 0.5f) : 0;
      out[p + 3] = uint8_t(std::clamp(a, 0.f, 1.f) * 255.f + 0.5f);
    }
    return out;
  }
};

// Local drawing units (a 64-unit key cell) to canvas pixels.
struct Transform {
  double sx = 1, sy = 1, tx = 0, ty = 0;
};

struct Rect {
  double x, y, w, h;
};

// Fill a shape given by an inside test over its local bounds, 4x4 samples per pixel.
template <typename Inside, typename Paint>
void Fill(Canvas& canvas, const Transform& t, Rect bounds, Inside inside, Paint paint) {
  int x0 = std::max(0, int(std::floor(bounds.x * t.sx + t.tx)));
  int y0 = std::max(0, int(std::floor(bounds.y * t.sy + t.ty)));
  int x1 = std::min(canvas.width, int(std::ceil((bounds.x + bounds.w) * t.sx + t.tx)) + 1);
  int y1 = std::min(canvas.height, int(std::ceil((bounds.y + bounds.h) * t.sy + t.ty)) + 1);
  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++) {
      int hits = 0;
      Colour sum{0, 0, 0, 0};
      for (int sy = 0; sy < 4; sy++) {
        for (int sx = 0; sx < 4; sx++) {
          double lx = (x + (sx + 0.5) / 4 - t.tx) / t.sx;
          double ly = (y + (sy + 0.5) / 4 - t.ty) / t.sy;
          if (!inside(lx, ly)) continue;
          Colour c = paint(lx, ly);
          sum.r += c.r; sum.g += c.g; sum.b += c.b; sum.a += c.a;
          hits++;
        }
      }
      if (hits == 0) continue;
      canvas.Blend(x, y, {sum.r / hits, sum.g / hits, sum.b / hits, sum.a / hits}, hits / 16.f);
    }
  }
}

bool InRoundedRect(Rect r, double radius, double x, double y) {
  if (x < r.x || y < r.y || x > r.x + r.w || y > r.y + r.h) return false;
  double cx = std::clamp(x, r.x + radius, r.x + r.w - radius);
  double cy = std::clamp(y, r.y + radius, r.y + r.h - radius);
  return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius;
}

void FillRoundedRect(Canvas& canvas, const Transform& t, Rect r, double radius, Colour c) {
  Fill(canvas, t, r, [&](double x, double y) { return InRoundedRect(r, radius, x, y); },
       [&](double, double) { return c; });
}

void FillRing(Canvas& canvas, const Transform& t, double cx, double cy, double inner, double outer, Colour c) {
  Fill(canvas, t, {cx - outer, cy - outer, outer * 2, outer * 2},
       [&](double x, double y) {
         double d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
         return d >= inner * inner && d <= outer * outer;
       },
       [&](double, double) { return c; });
}

struct Font {
  std::vector<unsigned char> data;
  stbtt_fontinfo info{};
};

// Bold UI fonts first; later ones fill in glyphs the first lacks.
const std::vector<Font>& Fonts() {
  static const std::vector<Font> fonts = [] {
    std::vector<std::filesystem::path> candidates;
#ifdef _WIN32
    const char* windir = std::getenv("WINDIR");
    std::filesystem::path dir = std::filesystem::path(windir ? windir : "C:\\Windows") / "Fonts";
    for (const char* name : {"segoeuib.ttf", "seguisym.ttf", "arialbd.ttf", "msgothic.ttc"})
      candidates.push_back(dir / name);
#elif defined(__APPLE__)
    candidates = {"/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/System/Library/Fonts/Helvetica.ttc"};
#else
    candidates = {"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                  "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
                  "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
                  "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"};
#endif
    std::vector<Font> loaded;
    for (const auto& path : candidates) {
      std::ifstream file(path, std::ios::binary);
      if (!file) continue;
      Font font;
      font.data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
      loaded.push_back(std::move(font));
      if (!stbtt_InitFont(&loaded.back().info, loaded.back().data.data(),
                          stbtt_GetFontOffsetForIndex(loaded.back().data.data(), 0))) {
        loaded.pop_back();
      }
    }
    return loaded;
  }();
  return fonts;
}

std::u32string Decode(std::string_view s) {
  std::u32string out;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    char32_t cp = extra == 0 ? c : c & (0x3F >> extra);
    for (int k = 1; k <= extra && i + k < s.size(); k++) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    out += cp;
    i += extra + 1;
  }
  return out;
}

struct Glyph {
  const Font* font;
  int index;
};

Glyph FindGlyph(char32_t cp) {
  const auto& fonts = Fonts();
  for (const Font& font : fonts) {
    int index = stbtt_FindGlyphIndex(&font.info, int(cp));
    if (index != 0) return {&font, index};
  }
  return {&fonts.front(), 0};
}

double Advance(const std::u32string& text, double em) {
  double width = 0;
  for (char32_t cp : text) {
    Glyph g = FindGlyph(cp);
    int advance = 0, bearing = 0;
    stbtt_GetGlyphHMetrics(&g.font->info, g.index, &advance, &bearing);
    width += advance * stbtt_ScaleForMappingEmToPixels(&g.font->info, float(em));
  }
  return width;
}

// Centred on its advance width and line box; longer labels shrink to fit.
void DrawLabel(Canvas& canvas, const Transform& t, double cx, double cy, std::string_view label, double max_width,
               Colour ink) {
  std::u32string text = Decode(label);
  double em = 34;
  double width = Advance(text, em);
  while (width > max_width && em > 8) {
    em -= 1;
    width = Advance(text, em);
  }
  const stbtt_fontinfo& primary = Fonts().front().info;
  int ascent = 0, descent = 0, gap = 0;
  stbtt_GetFontVMetrics(&primary, &ascent, &descent, &gap);
  double primary_scale = stbtt_ScaleForMappingEmToPixels(&primary, float(em));
  double baseline = cy - (ascent - descent) * primary_scale / 2 + ascent * primary_scale;

  double pen = cx - width / 2;
  for (char32_t cp : text) {
    Glyph g = FindGlyph(cp);
    double scale = stbtt_ScaleForMappingEmToPixels(&g.font->info, float(em));
    float scale_x = float(scale * t.sx), scale_y = float(scale * t.sy);
    double device_x = pen * t.sx + t.tx, device_y = baseline * t.sy + t.ty;
    float shift_x = float(device_x - std::floor(device_x)), shift_y = float(device_y - std::floor(device_y));
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBoxSubpixel(&g.font->info, g.index, scale_x, scale_y, shift_x, shift_y, &x0, &y0, &x1, &y1);
    int w = x1 - x0, h = y1 - y0;
    if (w > 0 && h > 0) {
      std::vector<unsigned char> coverage(size_t(w) * h);
      stbtt_MakeGlyphBitmapSubpixel(&g.font->info, coverage.data(), w, h, w, scale_x, scale_y, shift_x, shift_y,
                                    g.index);
      int ox = int(std::floor(device_x)) + x0, oy = int(std::floor(device_y)) + y0;
      for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) canvas.Blend(ox + x, oy + y, ink, coverage[size_t(y) * w + x] / 255.f);
    }
    int advance = 0, bearing = 0;
    stbtt_GetGlyphHMetrics(&g.font->info, g.index, &advance, &bearing);
    pen += advance * scale;
  }
}

constexpr int kNoButton = 2;

// side: -1 left button, 1 right button, 0 middle button, kNoButton for none.
void DrawMouse(Canvas& canvas, const Transform& t, double cx, double cy, int side, Colour ink,
               const Rect& face_rect, Colour top, Colour bottom) {
  Rect body{cx - 11, cy - 15, 22, 30};
  Rect inner{body.x + 2.5, body.y + 2.5, body.w - 5, body.h - 5};
  auto face = [&](double, double y) {
    float k = float(std::clamp((y - face_rect.y) / face_rect.h, 0.0, 1.0));
    return Colour{top.r + (bottom.r - top.r) * k, top.g + (bottom.g - top.g) * k, top.b + (bottom.b - top.b) * k, 1};
  };
  FillRoundedRect(canvas, t, body, 10, ink);
  Fill(canvas, t, inner, [&](double x, double y) { return InRoundedRect(inner, 8, x, y); }, face);

  Rect buttons{inner.x, inner.y, inner.w, 11};
  if (side != kNoButton) {
    Rect pressed = side < 0   ? Rect{buttons.x, buttons.y, buttons.w / 2, buttons.h}
                   : side > 0 ? Rect{buttons.x + buttons.w / 2, buttons.y, buttons.w / 2, buttons.h}
                              : Rect{cx - 2.5, buttons.y, 5, buttons.h};
    Fill(canvas, t, pressed,
         [&](double x, double y) {
           return x >= pressed.x && x <= pressed.x + pressed.w && y >= pressed.y && y <= pressed.y + pressed.h &&
                  InRoundedRect(inner, 8, x, y);
         },
         [&](double, double) { return ink; });
  }
  Rect divider{body.x, buttons.y + buttons.h, body.w, 1.5};
  Fill(canvas, t, divider,
       [&](double x, double y) {
         return x >= divider.x && x <= divider.x + divider.w && y >= divider.y && y <= divider.y + divider.h;
       },
       [&](double, double) { return ink; });
}

void DrawKey(Canvas& canvas, const Transform& t, double x, double y, const std::string& label, double shade) {
  Colour edge = Rgb(35, 35, 40, shade);
  Colour top = Rgb(245, 245, 245, shade), bottom = Rgb(205, 205, 205, shade);
  Colour ink = Rgb(20, 20, 25);

  FillRoundedRect(canvas, t, {x + 7, y + 7, 50, 50}, 9, edge);
  Rect face{x + 10, y + 10, 44, 42};
  Fill(canvas, t, face, [&](double px, double py) { return InRoundedRect(face, 7, px, py); },
       [&](double, double py) {
         float k = float(std::clamp((py - face.y) / face.h, 0.0, 1.0));
         return Colour{top.r + (bottom.r - top.r) * k, top.g + (bottom.g - top.g) * k,
                       top.b + (bottom.b - top.b) * k, 1};
       });

  double cx = face.x + face.w / 2, cy = face.y + face.h / 2;
  if (label.starts_with(kMouseLabel)) {
    std::string direction = label.substr(kMouseLabel.size());
    DrawMouse(canvas, t, direction.empty() ? cx : cx - 8, cy, kNoButton, ink, face, top, bottom);
    if (!direction.empty()) DrawLabel(canvas, t, cx + 13, cy, direction, 18, ink);
  } else if (label == "LMB") {
    DrawMouse(canvas, t, cx, cy, -1, ink, face, top, bottom);
  } else if (label == "RMB") {
    DrawMouse(canvas, t, cx, cy, 1, ink, face, top, bottom);
  } else if (label == "MMB") {
    DrawMouse(canvas, t, cx, cy, 0, ink, face, top, bottom);
  } else {
    DrawLabel(canvas, t, cx, cy, label, face.w - 6, ink);
  }
}

// Several keys in a row, or four in the shape of WASD, shrunk to share one cell.
void DrawKeys(Canvas& canvas, const Transform& t, const std::vector<std::string>& keys) {
  if (keys.size() == 1) {
    DrawKey(canvas, t, 0, 0, keys[0], 1.0);
    return;
  }
  // A key's body covers 50 of its 64 units, starting 7 in.
  const bool cross = keys.size() == 4;
  const double scale = cross ? 0.4 : 0.6;
  const double body = 50 * scale, gap = 2;
  const int across = cross ? 3 : int(keys.size());
  const int rows = cross ? 2 : 1;
  const double left = (kCellSize - (across * body + (across - 1) * gap)) / 2;
  const double top = (kCellSize - (rows * body + (rows - 1) * gap)) / 2;
  for (size_t i = 0; i < keys.size(); i++) {
    // Cross order is up, left, down, right: up sits above the middle of the bottom row.
    int column = cross ? (i == 0 ? 1 : int(i) - 1) : int(i);
    int row = cross && i != 0 ? 1 : 0;
    double x = left + column * (body + gap) - 7 * scale;
    double y = top + row * (body + gap) - 7 * scale;
    Transform k{t.sx * scale, t.sy * scale, t.tx + x * t.sx, t.ty + y * t.sy};
    DrawKey(canvas, k, 0, 0, keys[i], 1.0);
  }
}

Image ToImage(const Canvas& canvas) { return {canvas.width, canvas.height, canvas.ToStraightRgba()}; }

// A 64x64 cell per button (normal row, then the dim row), then the ring and dot
// drawn around the active prompt, where the game expects them.
Image RenderSheet(const std::vector<std::string>& labels) {
  Canvas canvas(256, 256);
  Transform t;
  for (size_t column = 0; column < labels.size(); column++) {
    DrawKey(canvas, t, double(column) * kCellSize, 0, labels[column], 1.0);
    DrawKey(canvas, t, double(column) * kCellSize, kCellSize, labels[column], 0.55);
  }
  const Colour green = Rgb(156, 190, 32), orange = Rgb(90, 72, 32);
  FillRing(canvas, t, 48, 176, 26.5, 38.5, green);
  FillRing(canvas, t, 112, 143.5, 0, 6, green);
  FillRing(canvas, t, 176, 176, 24, 40, orange);
  return ToImage(canvas);
}

// One cell of the sheet's normal row.
Image Cell(const Image& sheet, int column) {
  Image cell{kCellSize, kCellSize, Bytes(size_t(kCellSize) * kCellSize * 4)};
  for (int y = 0; y < kCellSize; y++)
    std::copy_n(sheet.rgba.begin() + (size_t(y) * sheet.width + column * kCellSize) * 4, kCellSize * 4,
                cell.rgba.begin() + size_t(y) * kCellSize * 4);
  return cell;
}

Image RenderIcon(const std::vector<std::string>& keys, int size) {
  Canvas canvas(size, size);
  DrawKeys(canvas, {size / double(kCellSize), size / double(kCellSize), 0, 0}, keys);
  return ToImage(canvas);
}

}  // namespace

std::optional<PromptArt> RenderPromptArt(const PromptLabels& labels, std::span<const uint8_t> textures,
                                         std::span<const uint8_t> headers) {
  if (Fonts().empty() || labels.sheet.size() != 4) return std::nullopt;
  PromptArt art;
  Image sheet = RenderSheet(labels.sheet);
  // The single-button icons show the same keys as the sheet's columns (A, B, Y, X).
  for (int column = 0; column < 4; column++) {
    const char letter = "abyx"[column];
    art[std::string("Art\\GUI\\Controller\\icon_button_") + letter + ".tex"] = Cell(sheet, column);
  }
  art[kSheetEntry] = std::move(sheet);
  for (const auto& [name, icon] : labels.icons) art[name] = RenderIcon(icon.keys, icon.size);

  // Keys drawn over the buttons of the original sheet.
  std::map<std::string, Canvas> sheets;
  for (const SheetButton& cell : kSheetButtonCells) {
    auto label = labels.buttons.find(cell.button);
    if (label == labels.buttons.end()) continue;
    auto it = sheets.find(cell.sheet);
    if (it == sheets.end()) {
      std::optional<Image> image = DecodeEntry(textures, headers, cell.sheet);
      if (!image) continue;
      Canvas canvas(image->width, image->height);
      for (size_t p = 0; p < image->rgba.size(); p += 4) {
        float a = image->rgba[p + 3] / 255.f;
        for (int i = 0; i < 3; i++) canvas.pixels[p + i] = image->rgba[p + i] / 255.f * a;
        canvas.pixels[p + 3] = a;
      }
      it = sheets.emplace(cell.sheet, std::move(canvas)).first;
    }
    Transform t{cell.width / kCellSize, cell.height / kCellSize, cell.x - cell.width / 2, cell.y - cell.height / 2};
    DrawKey(it->second, t, 0, 0, label->second, 1.0);
  }
  for (const auto& [name, canvas] : sheets) art[name] = ToImage(canvas);
  return art;
}

}  // namespace fable2::prompts
