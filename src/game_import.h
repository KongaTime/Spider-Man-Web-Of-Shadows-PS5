#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace wos {
struct DiscEntry {
  std::string path;
  uint64_t offset;
  uint64_t size;
};
struct ImportProgress {
  uint64_t bytes = 0;
  uint64_t total = 0;
  std::string file;
};
using ProgressCallback = std::function<void(const ImportProgress&)>;

// Read-only inventory; also used by the host-side import verification tool.
std::vector<DiscEntry> InspectDisc(const std::filesystem::path& iso);
bool ValidateGame(const std::filesystem::path& folder, std::string& error);
std::filesystem::path FindDisc(const std::filesystem::path& application_folder);
void ImportDisc(const std::filesystem::path& iso, const std::filesystem::path& folder,
                const std::atomic<bool>& cancelled, const ProgressCallback& progress);
}  // namespace wos
