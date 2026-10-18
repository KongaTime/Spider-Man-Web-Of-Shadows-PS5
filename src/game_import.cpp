// A read-only XDVDFS importer. Filesystem layout reference: XboxDev/extract-xiso.
// This implementation uses absolute paths and never changes the process CWD.
#include "game_import.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#define XXH_INLINE_ALL
#include <xxhash.h>

namespace wos {
namespace fs = std::filesystem;
namespace {
constexpr uint64_t kExecutableHash = 0xebcee0506fed61e8ULL;
constexpr uint64_t kExecutableSize = 14528512;
constexpr char kOwnership[] = "wos-import-v1-ebcee0506fed61e8";
struct RequiredFile { const char* path; uint64_t size; };
constexpr RequiredFile kRequired[] = {
#include "file_layout.inc"
};
using File = std::unique_ptr<FILE, decltype(&std::fclose)>;
File Open(const fs::path& path, const char* mode) {
  File file(std::fopen(path.string().c_str(), mode), &std::fclose);
  if (!file) throw std::runtime_error("Cannot open " + path.string());
  return file;
}
class Descriptor {
 public:
  Descriptor(const fs::path& path, int flags) : value_(::open(path.c_str(), flags, 0666)) {
    if (value_ < 0) throw std::runtime_error("Cannot open " + path.string() + ": " + std::strerror(errno));
  }
  ~Descriptor() { if (value_ >= 0) ::close(value_); }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  int get() const { return value_; }
  bool Close() { const int value = value_; value_ = -1; return ::close(value) == 0; }
 private:
  int value_;
};
uint16_t Le16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t Le32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
bool SupportedExecutable(const uint8_t* bytes, size_t size) {
  return size == kExecutableSize && std::memcmp(bytes, "XEX2", 4) == 0 &&
         XXH64(bytes, size, 0) == kExecutableHash;
}
bool SkipName(const std::string& name) { return name == "$SystemUpdate"; }
void SafeName(const std::string& name) {
  if (name.empty() || name == "." || name == ".." ||
      name.find_first_of("/\\:") != std::string::npos || name.find('\0') != std::string::npos)
    throw std::runtime_error("Invalid name in disc directory");
}

class Disc {
 public:
  explicit Disc(const fs::path& path) : file_(path, O_RDONLY), size_(fs::file_size(path)) {
    static_assert(sizeof(off_t) >= 8, "A 64-bit file-offset platform is required");
    constexpr uint64_t partitions[] = {0, 0x0FD90000, 0x02080000, 0x18300000};
    std::array<uint8_t, 2048> header{};
    bool found = false;
    for (uint64_t partition : partitions) {
      if (partition + 0x10800 > size_) continue;
      Read(partition + 0x10000, header.data(), header.size());
      if (std::memcmp(header.data(), "MICROSOFT*XBOX*MEDIA", 20) == 0 &&
          std::memcmp(header.data() + 2028, "MICROSOFT*XBOX*MEDIA", 20) == 0) {
        partition_ = partition;
        found = true;
        break;
      }
    }
    if (!found) throw std::runtime_error("This file is not a supported Xbox disc image");
    ReadDirectory(Le32(header.data() + 20), Le32(header.data() + 24), "", 0);
  }
  void Read(uint64_t offset, void* destination, size_t count) {
    if (offset > size_ || count > size_ - offset)
      throw std::runtime_error("Disc image is truncated or cannot be read");
    auto* bytes = static_cast<uint8_t*>(destination);
    while (count) {
      const auto received = ::pread(file_.get(), bytes, count, static_cast<off_t>(offset));
      if (received < 0 && errno == EINTR) continue;
      if (received <= 0) throw std::runtime_error("Disc image is truncated or cannot be read");
      bytes += received;
      offset += static_cast<uint64_t>(received);
      count -= static_cast<size_t>(received);
    }
  }
  void Verify() {
    std::map<std::string, const DiscEntry*> files;
    for (const auto& entry : entries) {
      if (!files.emplace(entry.path, &entry).second)
        throw std::runtime_error("Duplicate path in disc image");
    }
    for (const auto& required : kRequired) {
      auto it = files.find(required.path);
      if (it == files.end() || it->second->size != required.size)
        throw std::runtime_error("Unsupported or incomplete disc: " + std::string(required.path));
    }
    const auto* executable = files.at("default.xex");
    std::vector<uint8_t> bytes(executable->size);
    Read(executable->offset, bytes.data(), bytes.size());
    if (!SupportedExecutable(bytes.data(), bytes.size()))
      throw std::runtime_error("Unsupported game executable; this build needs the USA retail revision");
  }
  std::vector<DiscEntry> entries;

 private:
  void ReadDirectory(uint32_t sector, uint32_t bytes, const std::string& prefix, unsigned depth) {
    if (bytes == 0) return;
    if (depth > 32 || bytes > 16 * 1024 * 1024 || !directories_.emplace(sector).second)
      throw std::runtime_error("Invalid or cyclic disc directory");
    std::vector<uint8_t> table(bytes);
    Read(partition_ + uint64_t(sector) * 2048, table.data(), table.size());
    std::vector<uint32_t> pending{0};
    std::set<uint32_t> visited;
    while (!pending.empty()) {
      const uint32_t position = pending.back();
      pending.pop_back();
      if (position > bytes || bytes - position < 14 || !visited.emplace(position).second)
        throw std::runtime_error("Invalid directory entry or cycle");
      const auto* record = table.data() + position;
      const uint16_t left = Le16(record), right = Le16(record + 2);
      if (left == 0xffff && position == 0) continue;  // Empty directory.
      if (left) pending.push_back(uint32_t(left) * 4);
      if (right) pending.push_back(uint32_t(right) * 4);
      const uint32_t child_sector = Le32(record + 4), length = Le32(record + 8);
      const size_t name_length = record[13];
      if (name_length > bytes - position - 14)
        throw std::runtime_error("Truncated disc filename");
      const std::string name(reinterpret_cast<const char*>(record + 14), name_length);
      SafeName(name);
      if (SkipName(name)) continue;
      const std::string path = prefix.empty() ? name : prefix + "/" + name;
      if (record[12] & 0x10) {
        ReadDirectory(child_sector, length, path, depth + 1);
      } else {
        const uint64_t offset = partition_ + uint64_t(child_sector) * 2048;
        if (offset > size_ || length > size_ - offset || entries.size() >= 100000)
          throw std::runtime_error("Invalid disc file extent");
        entries.push_back({path, offset, length});
      }
    }
  }
  Descriptor file_;
  uint64_t size_;
  uint64_t partition_ = 0;
  std::set<uint32_t> directories_;
};

void NoSymlink(const fs::path& path) {
  // PS5 titles refuse lstat, which filesystem::symlink_status uses. Opening
  // without following the final component preserves the check and allows a
  // missing destination to be created by the importer.
  const int descriptor = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  if (descriptor >= 0) {
    ::close(descriptor);
    return;
  }
  const int error = errno;
  if (error == ENOENT) return;
  if (error == ELOOP)
    throw std::runtime_error("Import destination contains a symbolic link: " + path.string());
  throw std::runtime_error("Cannot check import destination " + path.string() + ": " + std::strerror(error));
}
}  // namespace

std::vector<DiscEntry> InspectDisc(const fs::path& iso) { return Disc(iso).entries; }

bool ValidateGame(const fs::path& folder, std::string& error) {
  try {
    NoSymlink(folder);
    for (const auto& required : kRequired) {
      auto file = folder / required.path;
      NoSymlink(file);
      if (!fs::is_regular_file(file) || fs::file_size(file) != required.size) {
        error = "Missing or incomplete game file: " + std::string(required.path);
        return false;
      }
    }
    auto file = Open(folder / "default.xex", "rb");
    std::vector<uint8_t> bytes(kExecutableSize);
    if (std::fread(bytes.data(), 1, bytes.size(), file.get()) != bytes.size() ||
        !SupportedExecutable(bytes.data(), bytes.size())) {
      error = "The game executable does not match the revision supported by this build";
      return false;
    }
    error.clear();
    return true;
  } catch (const std::exception& failure) {
    error = failure.what();
    return false;
  }
}

fs::path FindDisc(const fs::path& application_folder) {
  std::vector<fs::path> candidates;
  for (const auto& directory : {application_folder, application_folder / "import"}) {
    if (!fs::is_directory(directory)) continue;
    for (const auto& entry : fs::directory_iterator(directory)) {
      std::string extension = entry.path().extension().string();
      std::transform(extension.begin(), extension.end(), extension.begin(),
                     [](unsigned char c) { return char(std::tolower(c)); });
      if (entry.is_regular_file() && extension == ".iso") candidates.push_back(entry.path());
    }
  }
  if (candidates.size() > 1) throw std::runtime_error("Keep only one ISO in the app folder or import folder");
  return candidates.empty() ? fs::path{} : candidates.front();
}

void ImportDisc(const fs::path& iso, const fs::path& folder,
                const std::atomic<bool>& cancelled, const ProgressCallback& progress) {
  Disc disc(iso);
  disc.Verify();  // Verify before creating any output files.
  if (fs::exists(folder))
    throw std::runtime_error("The gamedata folder is incomplete; finish copying it or move it aside before importing");
  fs::path staging = folder;
  staging += ".installing";
  NoSymlink(staging);
  const auto ownership = staging / ".wos-import-owner";
  if (fs::exists(staging)) {
    auto marker = Open(ownership, "rb");
    std::array<char, sizeof(kOwnership)> text{};
    if (std::fread(text.data(), 1, sizeof(kOwnership) - 1, marker.get()) != sizeof(kOwnership) - 1 ||
        std::memcmp(text.data(), kOwnership, sizeof(kOwnership) - 1) != 0)
      throw std::runtime_error("The staging folder belongs to another import");
  } else {
    fs::create_directories(staging);
    auto marker = Open(ownership, "wb");
    if (std::fwrite(kOwnership, 1, sizeof(kOwnership) - 1, marker.get()) != sizeof(kOwnership) - 1)
      throw std::runtime_error("Cannot write import marker");
  }
  ImportProgress state;
  for (const auto& entry : disc.entries) state.total += entry.size;
  std::vector<uint8_t> buffer(1024 * 1024);
  for (const auto& entry : disc.entries) {
    if (cancelled) throw std::runtime_error("Import interrupted; it will resume on the next launch");
    const auto output = staging / entry.path;
    for (auto parent = output.parent_path(); parent != staging; parent = parent.parent_path())
      NoSymlink(parent);
    fs::create_directories(output.parent_path());
    NoSymlink(output);
    state.file = entry.path;
    if (fs::is_regular_file(output) && fs::file_size(output) == entry.size) {
      state.bytes += entry.size;
      progress(state);
      continue;
    }
    fs::path temporary = output;
    temporary += ".partial";
    NoSymlink(temporary);
    Descriptor target(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW);
    for (uint64_t offset = 0; offset < entry.size;) {
      if (cancelled) throw std::runtime_error("Import interrupted; it will resume on the next launch");
      const auto count = size_t(std::min<uint64_t>(buffer.size(), entry.size - offset));
      disc.Read(entry.offset + offset, buffer.data(), count);
      size_t written = 0;
      while (written < count) {
        const auto result = ::write(target.get(), buffer.data() + written, count - written);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) throw std::runtime_error("Cannot write game data; check available storage");
        written += static_cast<size_t>(result);
      }
      offset += count;
      state.bytes += count;
      progress(state);
    }
    if (!target.Close())
      throw std::runtime_error("Cannot finish writing game file");
    fs::rename(temporary, output);
  }
  std::string error;
  if (!ValidateGame(staging, error)) throw std::runtime_error(error);
  fs::rename(staging, folder);
}
}  // namespace wos
