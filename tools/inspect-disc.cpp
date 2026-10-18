#include "game_import.h"
#include <iostream>
#include <algorithm>
int main(int argc, char** argv) {
  try {
    if (argc == 4 && (std::string(argv[1]) == "--import" || std::string(argv[1]) == "--import-cancel")) {
      const bool interrupt = std::string(argv[1]) == "--import-cancel";
      std::atomic<bool> cancelled{false};
      wos::ImportDisc(argv[2], argv[3], cancelled, [&](const wos::ImportProgress& progress) {
        if (interrupt && progress.bytes >= 3 * 1024 * 1024) cancelled = true;
      });
      std::cout << "Import complete\n";
      return 0;
    }
    if (argc != 3) { std::cerr << "usage: inspect-disc --list ISO | --verify FOLDER | --import ISO FOLDER\n"; return 2; }
    if (std::string(argv[1]) == "--list") {
      auto entries = wos::InspectDisc(argv[2]);
      std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) { return a.path < b.path; });
      for (const auto& entry : entries) std::cout << entry.size << '\t' << entry.path << '\n';
    } else if (std::string(argv[1]) == "--verify") {
      std::string error;
      if (!wos::ValidateGame(argv[2], error)) { std::cerr << error << '\n'; return 1; }
      std::cout << "Supported executable and all required game files verified\n";
    } else return 2;
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
