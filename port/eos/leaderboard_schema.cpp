#include "leaderboard_schema.h"
#include <cstdio>
#include <limits>
#include <regex>
#include <rex/string.h>
#include <stdexcept>
// Already provided by ReXGlue's BSD/public-domain stb PNG dependency. The raw
// DEFLATE decoder avoids adding a downloadable runtime dependency to the port.
extern "C" int stbi_zlib_decode_noheader_buffer(char *, int, const char *, int);
namespace redbaron::eos {
namespace {
uint32_t BE(std::span<const uint8_t> b, size_t p) {
  if (p + 4 > b.size())
    throw std::runtime_error("Truncated service metadata");
  return uint32_t(b[p]) << 24 | uint32_t(b[p + 1]) << 16 |
         uint32_t(b[p + 2]) << 8 | b[p + 3];
}
uint32_t LE(std::span<const uint8_t> b, size_t p) {
  if (p + 4 > b.size())
    throw std::runtime_error("Truncated gzip footer");
  return uint32_t(b[p + 3]) << 24 | uint32_t(b[p + 2]) << 16 |
         uint32_t(b[p + 1]) << 8 | b[p];
}
uint32_t CRC(std::string_view s) {
  uint32_t c = ~0u;
  for (unsigned char x : s) {
    c ^= x;
    for (int i = 0; i < 8; ++i)
      c = (c >> 1) ^ (0xEDB88320u & uint32_t(-int(c & 1)));
  }
  return ~c;
}
std::string Attr(const std::string &s, const char *key) {
  std::smatch m;
  return std::regex_search(
             s, m, std::regex(std::string("\\b") + key + "=\"([^\"]*)\""))
             ? m[1].str()
             : std::string{};
}
uint32_t Number(const std::string &s) {
  size_t n = 0;
  auto v = std::stoull(s, &n, 0);
  if (n != s.size() || v > UINT32_MAX)
    throw std::runtime_error("Invalid service identifier");
  return uint32_t(v);
}
} // namespace
std::string LeaderboardId(uint32_t view) {
  char s[32];
  std::snprintf(s, sizeof(s), "SFA_V%08X", view);
  return s;
}
std::string LeaderboardStat(uint32_t view, uint32_t prop) {
  char s[48];
  std::snprintf(s, sizeof(s), "SFA_V%08X_P%08X", view, prop);
  return s;
}
std::vector<LeaderboardView> ReadLeaderboardSchema(std::span<const uint8_t> b) {
  if (b.size() < 32 || BE(b, 0) != 0x58535243)
    throw std::runtime_error("Missing native service metadata");
  const size_t name = BE(b, 12);
  if (name > 1024 || 16 + name + 8 > b.size())
    throw std::runtime_error("Invalid service metadata header");
  const auto size = BE(b, 16 + name), packed = BE(b, 20 + name);
  const size_t at = 24 + name;
  if (!size || size > 8 * 1024 * 1024 || packed < 18 || at + packed > b.size())
    throw std::runtime_error("Invalid compressed service metadata size");
  auto gz = b.subspan(at, packed);
  if (gz[0] != 31 || gz[1] != 139 || gz[2] != 8 || gz[3] != 0 ||
      LE(gz, gz.size() - 4) != size)
    throw std::runtime_error("Unsupported native service gzip header");
  std::string xml(size, '\0');
  if (stbi_zlib_decode_noheader_buffer(
          xml.data(), int(size), reinterpret_cast<const char *>(gz.data() + 10),
          int(gz.size() - 18)) != int(size) ||
      CRC(xml) != LE(gz, gz.size() - 8))
    throw std::runtime_error("Corrupt native service metadata");
  if (xml.size() >= 4 && ((uint8_t(xml[0]) == 255 && uint8_t(xml[1]) == 254) ||
                          (xml[0] == '<' && xml[1] == 0))) {
    if (xml.size() % 2)
      throw std::runtime_error("Invalid UTF-16 service metadata");
    std::u16string wide;
    wide.reserve(xml.size() / 2);
    for (size_t i = uint8_t(xml[0]) == 255 ? 2 : 0; i < xml.size(); i += 2)
      wide.push_back(
          char16_t(uint8_t(xml[i]) | (uint16_t(uint8_t(xml[i + 1])) << 8)));
    xml = rex::string::to_utf8(wide);
  }
  const std::regex views("<StatsView\\b([^>]*)>([\\s\\S]*?)</StatsView>"),
      fields("<Field\\b([^>]*)>([\\s\\S]*?)</Field>");
  std::vector<LeaderboardView> out;
  for (auto i = std::sregex_iterator(xml.begin(), xml.end(), views);
       i != std::sregex_iterator(); ++i) {
    LeaderboardView v;
    v.id = Number(Attr((*i)[1], "id"));
    v.name = Attr((*i)[1], "friendlyName");
    v.reset = Attr((*i)[1], "resetType");
    auto body = (*i)[2].str();
    for (auto f = std::sregex_iterator(body.begin(), body.end(), fields);
         f != std::sregex_iterator(); ++f) {
      auto id = Number(Attr((*f)[1], "attributeId"));
      std::smatch p, a;
      auto field = (*f)[2].str();
      if (id > UINT16_MAX ||
          !std::regex_search(field, p, std::regex("<Property\\b([^>]*)>")) ||
          !std::regex_search(field, a, std::regex("<Aggregation\\b([^>]*)/?>")))
        continue;
      LeaderboardColumn c;
      c.id = uint16_t(id);
      c.property = Number(Attr(p[1], "id"));
      c.aggregation = Attr(a[1], "type");
      if (c.aggregation == "Last")
        c.aggregation = "LATEST";
      else if (c.aggregation == "Sum")
        c.aggregation = "SUM";
      else if (c.aggregation == "Max")
        c.aggregation = "MAX";
      else if (c.aggregation == "Min")
        c.aggregation = "MIN";
      else
        throw std::runtime_error("Unsupported native stat aggregation");
      v.columns.push_back(std::move(c));
    }
    if (v.columns.size() > 64 || out.size() >= 64)
      throw std::runtime_error("Native leaderboard schema exceeds limits");
    out.push_back(std::move(v));
  }
  if (out.empty())
    throw std::runtime_error("No native leaderboards found");
  return out;
}
} // namespace redbaron::eos
