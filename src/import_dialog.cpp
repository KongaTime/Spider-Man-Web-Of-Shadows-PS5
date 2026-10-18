#include "import_dialog.h"
#include "game_import.h"
#include <rex/ui/imgui_dialog.h>
#include <rex/logging.h>
#include <imgui.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace wos {
class ImportDialog final : public rex::ui::ImGuiDialog {
 public:
  ImportDialog(rex::ui::ImGuiDrawer* drawer, rex::PathConfig paths,
               std::function<void(rex::PathConfig)> resume)
      : ImGuiDialog(drawer), paths_(std::move(paths)), resume_(std::move(resume)),
        worker_([this] { Run(); }) {}
  ~ImportDialog() override {
    cancelled_ = true;
    if (worker_.joinable()) worker_.join();
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    bool finished;
    ImportProgress progress;
    std::string message, error;
    {
      std::lock_guard lock(mutex_);
      finished = finished_;
      progress = progress_;
      message = message_;
      error = error_;
    }
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(800, 0), ImGuiCond_Always);
    ImGui::Begin("Web of Shadows setup", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextWrapped("%s", message.c_str());
    if (!error.empty()) {
      ImGui::Spacing();
      ImGui::TextWrapped("Import stopped: %s", error.c_str());
      ImGui::TextWrapped("Correct the files and restart the app to try again.");
    } else if (progress.total) {
      ImGui::ProgressBar(float(double(progress.bytes) / double(progress.total)), ImVec2(-1, 0));
      ImGui::TextWrapped("%s", progress.file.c_str());
      ImGui::Text("%.1f / %.1f GB", double(progress.bytes) / 1e9, double(progress.total) / 1e9);
    }
    ImGui::End();
    if (finished) {
      auto resume = std::move(resume_);
      Close();
      resume(paths_);  // ReXApp defers this onto its UI event loop.
    }
  }

 private:
  void Run() {
    try {
      std::string validation_error;
      REXLOG_INFO("Game setup: checking {}", paths_.game_data_root.string());
      {
        std::lock_guard lock(mutex_);
        message_ = "Checking game files...";
      }
      if (!ValidateGame(paths_.game_data_root, validation_error)) {
        REXLOG_INFO("Game setup: import needed ({})", validation_error);
        const auto app = paths_.game_data_root.parent_path();
        std::filesystem::create_directories(app / "import");
        std::filesystem::path iso;
        while (!cancelled_ && iso.empty()) {
          {
            std::lock_guard lock(mutex_);
            message_ = "Copy your Xbox 360 Web of Shadows ISO into this app's import folder. "
                       "Setup will start automatically when the file is present.";
          }
          iso = FindDisc(app);
          if (iso.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        if (cancelled_) return;
        REXLOG_INFO("Game setup: found ISO {}", iso.string());
        {
          std::lock_guard lock(mutex_);
          message_ = "Verifying and importing your game. Keep the app open.";
        }
        uint64_t reported = 0;
        ImportDisc(iso, paths_.game_data_root, cancelled_, [this, &reported](const ImportProgress& progress) {
          if (progress.bytes >= reported + (uint64_t(1) << 30) || progress.bytes == progress.total) {
            reported = progress.bytes;
            REXLOG_INFO("Game setup: imported {} / {} bytes", progress.bytes, progress.total);
          }
          std::lock_guard lock(mutex_);
          progress_ = progress;
        });
      }
      REXLOG_INFO("Game setup: files verified; resuming runtime setup");
      std::lock_guard lock(mutex_);
      message_ = "Game files verified. Starting Web of Shadows...";
      finished_ = true;
    } catch (const std::exception& error) {
      REXLOG_ERROR("Game setup failed: {}", error.what());
      std::lock_guard lock(mutex_);
      error_ = error.what();
    }
  }
  rex::PathConfig paths_;
  std::function<void(rex::PathConfig)> resume_;
  std::atomic<bool> cancelled_{false};
  std::mutex mutex_;
  bool finished_ = false;
  ImportProgress progress_;
  std::string message_, error_;
  std::thread worker_;
};

void ShowImporter(rex::ui::ImGuiDrawer* drawer, rex::PathConfig paths,
                  std::function<void(rex::PathConfig)> resume) {
  new ImportDialog(drawer, std::move(paths), std::move(resume));
}
}  // namespace wos
