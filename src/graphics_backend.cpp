#include "graphics_backend.h"

#include <algorithm>
#include <array>
#include <rex/chrono/clock.h>
#include <time.h>
#include <rex/graphics/vulkan/command_processor.h>
#include <rex/graphics/vulkan/graphics_system.h>
#include <rex/logging.h>

namespace wos {
namespace {
struct Clock {
  static uint64_t now() { return rex::chrono::Clock::QueryHostTickCount(); }
};
double Milliseconds(uint64_t nanoseconds) {
  return double(nanoseconds) / 1000000.0;
}
uint64_t ThreadCpuNanoseconds() {
  timespec time{};
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time) != 0) return 0;
  return uint64_t(time.tv_sec) * 1000000000ull + uint64_t(time.tv_nsec);
}
uint64_t ProcessCpuNanoseconds() {
  timespec time{};
  if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time) != 0) return 0;
  return uint64_t(time.tv_sec) * 1000000000ull + uint64_t(time.tv_nsec);
}

// Measure existing backend calls without changing the command stream or
// waiting for GPU completion. Times include any waits inside the backend;
// they are host wall times, not GPU timestamp-query measurements.
class MeasuredCommandProcessor final
    : public rex::graphics::vulkan::VulkanCommandProcessor {
 public:
  using VulkanCommandProcessor::VulkanCommandProcessor;

 protected:
  void PrepareForWait() override {
    wait_start_ = Clock::now();
    VulkanCommandProcessor::PrepareForWait();
  }
  void ReturnFromWait() override {
    VulkanCommandProcessor::ReturnFromWait();
    if (wait_start_) {
      wait_ms_ += Milliseconds(Clock::now() - wait_start_);
      ++waits_;
      wait_start_ = 0;
    }
  }
  bool IssueDraw(rex::graphics::xenos::PrimitiveType type, uint32_t count,
                 IndexBufferInfo* indices, bool explicit_mode) override {
    const auto start = Clock::now();
    const bool result = VulkanCommandProcessor::IssueDraw(type, count, indices, explicit_mode);
    draw_ms_ += Milliseconds(Clock::now() - start);
    ++draws_;
    return result;
  }

  bool IssueCopy() override {
    const auto start = Clock::now();
    const bool result = VulkanCommandProcessor::IssueCopy();
    copy_ms_ += Milliseconds(Clock::now() - start);
    return result;
  }

  void OnPrimaryBufferEnd() override {
    const auto start = Clock::now();
    VulkanCommandProcessor::OnPrimaryBufferEnd();
    buffer_ms_ += Milliseconds(Clock::now() - start);
  }

  void IssueSwap(uint32_t pointer, uint32_t width, uint32_t height) override {
    const auto start = Clock::now();
    VulkanCommandProcessor::IssueSwap(pointer, width, height);
    const auto end = Clock::now();
    const auto cpu = ThreadCpuNanoseconds();
    const auto process_cpu = ProcessCpuNanoseconds();
    swap_ms_ += Milliseconds(end - start);
    if (last_swap_ != 0) {
      const double frame_ms = Milliseconds(end - last_swap_);
      interval_ms_ += frame_ms;
      frame_times_[frames_ % frame_times_.size()] = frame_ms;
      ++total_frames_;
      if (cpu >= last_cpu_) cpu_ms_ += Milliseconds(cpu - last_cpu_);
      if (process_cpu >= last_process_cpu_) process_cpu_ms_ += Milliseconds(process_cpu - last_process_cpu_);
      ++frames_;
      if (interval_ms_ >= 2000.0) {
        const double n = double(frames_);
        REXLOG_INFO("WOS perf: {:.2f} guest swaps/s, {:.2f} ms/frame, "
                    "draw {:.2f} ms ({:.1f} draws), copy {:.2f} ms, "
                    "swap {:.2f} ms, buffer-end {:.2f} ms, graphics-thread CPU {:.2f} ms, {}x{}",
                    n * 1000.0 / interval_ms_, interval_ms_ / n,
                    draw_ms_ / n, double(draws_) / n, copy_ms_ / n,
                    swap_ms_ / n, buffer_ms_ / n, cpu_ms_ / n, width, height);
        const size_t count = std::min(size_t(frames_), frame_times_.size());
        std::sort(frame_times_.begin(), frame_times_.begin() + count);
        REXLOG_INFO("WOS frame times: frames {}-{}, min {:.2f}, median {:.2f}, "
                    "p95 {:.2f}, max {:.2f} ms ({} samples)",
                    total_frames_ - frames_ + 1, total_frames_, frame_times_[0],
                    frame_times_[(count - 1) / 2],
                    frame_times_[size_t(double(count - 1) * 0.95)],
                    frame_times_[count - 1], count);
        REXLOG_INFO("WOS scheduling: command waits {:.2f} ms/frame ({:.1f} waits), "
                    "whole-process CPU {:.2f} ms/frame",
                    wait_ms_ / n, double(waits_) / n, process_cpu_ms_ / n);
        interval_ms_ = draw_ms_ = copy_ms_ = swap_ms_ = buffer_ms_ = cpu_ms_ = 0.0;
        frames_ = draws_ = 0;
        wait_ms_ = process_cpu_ms_ = 0.0;
        waits_ = 0;
      }
    } else {
      draw_ms_ = copy_ms_ = swap_ms_ = buffer_ms_ = 0.0;
      draws_ = 0;
      wait_ms_ = process_cpu_ms_ = 0.0;
      waits_ = 0;
    }
    last_swap_ = end;
    last_cpu_ = cpu;
    last_process_cpu_ = process_cpu;
  }

 private:
  uint64_t last_swap_ = 0, last_cpu_ = 0;
  double interval_ms_ = 0.0, draw_ms_ = 0.0, copy_ms_ = 0.0;
  double swap_ms_ = 0.0, buffer_ms_ = 0.0;
  double cpu_ms_ = 0.0;
  uint64_t frames_ = 0, draws_ = 0;
  uint64_t total_frames_ = 0;
  uint64_t wait_start_ = 0, waits_ = 0, last_process_cpu_ = 0;
  double wait_ms_ = 0.0, process_cpu_ms_ = 0.0;
  std::array<double, 512> frame_times_{};
};

class MeasuredGraphicsSystem final
    : public rex::graphics::vulkan::VulkanGraphicsSystem {
  std::unique_ptr<rex::graphics::CommandProcessor> CreateCommandProcessor() override {
    return std::make_unique<MeasuredCommandProcessor>(this, kernel_state_);
  }
};
}  // namespace

std::unique_ptr<rex::graphics::GraphicsSystem> CreateGraphicsSystem() {
  return std::make_unique<MeasuredGraphicsSystem>();
}
}  // namespace wos
