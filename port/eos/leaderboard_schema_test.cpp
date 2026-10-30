// Synthetic schema fixture authored for this test, not game metadata.
#include "leaderboard_policy.h"
#include "leaderboard_schema.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
static void Check(bool v, const char *m) {
  if (!v)
    throw std::runtime_error(m);
}
int main(int argc, char **argv) {
  try {
    using redbaron::eos::ActiveLeaderboardPeriod;
    Check(ActiveLeaderboardPeriod(false, 100, -1, 200),
          "All-time ranking is active after its start");
    Check(!ActiveLeaderboardPeriod(true, 100, -1, 200),
          "Weekly ranking cannot silently use an unlimited period");
    Check(ActiveLeaderboardPeriod(true, 100, 100 + 7 * 86400, 200),
          "Current weekly period is accepted");
    Check(!ActiveLeaderboardPeriod(true, 100, 100 + 7 * 86400, 100 + 7 * 86400),
          "Ended weekly period is not reused");
    Check(!ActiveLeaderboardPeriod(true, 1000, 1000 + 7 * 86400, 200),
          "Future weekly period is not selected early");
    std::vector<uint8_t> b{
        88,  83,  82,  67,  0,   0,   0,   1,   0,   0,   0,   0,   0,   0,
        0,   13,  97,  117, 116, 104, 111, 114, 101, 100, 45,  116, 101, 115,
        116, 0,   0,   1,   170, 0,   0,   0,   217, 31,  139, 8,   0,   0,
        0,   0,   0,   0,   255, 109, 209, 81,  75,  2,   65,  20,  197, 241,
        223, 71,  89,  252, 2,   43,  153, 61,  89,  32,  130, 208, 139, 4,
        70,  239, 134, 211, 38,  172, 110, 236, 142, 149, 223, 62,  188, 110,
        104, 24,  243, 116, 239, 220, 115, 207, 255, 204, 76,  204, 172, 100,
        43,  181, 70,  229, 193, 196, 50,  234, 172, 243, 98,  35,  249, 82,
        216, 88,  187, 55,  48,  54,  80,  120, 211, 198, 205, 206, 90,  237,
        96,  97,  101, 43,  197, 196, 212, 94,  246, 174, 209, 74,  214, 10,
        89,  210, 201, 161, 60,  246, 58,  73,  246, 236, 224, 163, 215, 44,
        36,  159, 146, 214, 32,  8,   102, 26,  181, 189, 173, 157, 46,  58,
        243, 240, 171, 99,  223, 145, 45,  7,   193, 107, 120, 37,  143, 61,
        221, 157, 177, 177, 145, 219, 126, 207, 147, 86,  19,  46,  173, 236,
        112, 145, 99,  232, 219, 141, 225, 197, 25,  245, 154, 169, 74,  21,
        156, 85,  56,  109, 52,  118, 145, 226, 204, 187, 12,  182, 99,  158,
        50,  52,  229, 149, 211, 169, 123, 166, 62,  213, 215, 185, 202, 127,
        94,  251, 119, 246, 239, 191, 252, 0,   29,  66,  120, 160, 170, 1,
        0,   0};
    const auto s = redbaron::eos::ReadLeaderboardSchema(b);
    Check(s.size() == 1 && s[0].id == 5 && s[0].columns.size() == 1,
          "UTF-16/gzip schema decoding");
    Check(s[0].columns[0].id == 65534 &&
              s[0].columns[0].property == 0x20000003 &&
              s[0].columns[0].aggregation == "SUM",
          "Native aggregation and primary column preserved");
    b.back() ^= 1;
    bool rejected = false;
    try {
      redbaron::eos::ReadLeaderboardSchema(b);
    } catch (...) {
      rejected = true;
    }
    Check(rejected, "Corrupt gzip size rejected");
    b.back() ^= 1;
    b[b.size() - 8] ^= 1;
    rejected = false;
    try {
      redbaron::eos::ReadLeaderboardSchema(b);
    } catch (...) {
      rejected = true;
    }
    Check(rejected, "Corrupt metadata checksum rejected");
    Check(redbaron::eos::LeaderboardId(5) == "SFA_V00000005" &&
              redbaron::eos::LeaderboardStat(5, 3) == "SFA_V00000005_P00000003",
          "Stable EOS identifiers");
    if (argc == 2) {
      std::ifstream f(argv[1], std::ios::binary);
      Check(bool(f), "Private schema open");
      std::vector<uint8_t> real((std::istreambuf_iterator<char>(f)), {});
      const auto views = redbaron::eos::ReadLeaderboardSchema(real);
      size_t columns = 0;
      for (const auto &v : views)
        columns += v.columns.size();
      std::cout << "Local schema: " << views.size() << " views, " << columns
                << " columns\n";
    }
    std::cout << "Leaderboard schema tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
