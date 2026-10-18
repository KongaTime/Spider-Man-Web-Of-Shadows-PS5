"""Exercise extracted descriptor reuse decisions with mock Vulkan descriptor writes."""
from pathlib import Path
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('sdk', type=Path)
parser.add_argument('--work-dir', required=True, type=Path)
parser.add_argument('--cxx', default='clang++')
args = parser.parse_args()
text = (args.sdk / 'src/graphics/vulkan/command_processor.cpp').read_text()
begin = text.index('struct WosTextureDescriptorSnapshot {')
end = text.index('// Disable per-draw host timing', begin)
helper = text[begin:end]
begin = text.index('  // Reuse only sets that were valid')
end = text.index('  // Write the new descriptor sets.', begin)
decision = text[begin:end]
begin = text.index('  if (REXCVAR_GET(wos_frame_costs)) {\n    wos_costs.texture_writes')
end = text.index('  // Bind the new descriptor sets.', begin)
commit = text[begin:end]
code = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include <span>
#include <vector>
using VkDescriptorSetLayout = uint64_t;
constexpr uint64_t VK_NULL_HANDLE = 0;
struct VkDescriptorImageInfo { uint64_t sampler = 0, imageView = 0; uint32_t imageLayout = 0; };
struct VulkanCommandProcessor;
struct SpirvShaderTranslator {
  static constexpr uint32_t kDescriptorSetTexturesVertex = 2, kDescriptorSetTexturesPixel = 3;
};
#define REXCVAR_GET(...) true
struct { uint64_t texture_reuses = 0, texture_writes = 0; } wos_costs;
'''
code += helper
code += r'''
struct Layout {
  uint64_t vertex = 10, pixel = 11;
  auto descriptor_set_layout_textures_vertex_ref() { return vertex; }
  auto descriptor_set_layout_textures_pixel_ref() { return pixel; }
};
bool Same(const std::vector<VkDescriptorImageInfo>& a, const std::vector<VkDescriptorImageInfo>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].sampler != b[i].sampler || a[i].imageView != b[i].imageView || a[i].imageLayout != b[i].imageLayout) return false;
  return true;
}
struct VulkanCommandProcessor {
  uint32_t current_graphics_descriptor_set_values_up_to_date_ = 0;
  std::vector<VkDescriptorImageInfo> descriptor_write_image_info_, gpu_vertex, gpu_pixel;
  Layout layout;
  Layout* current_guest_graphics_pipeline_layout_ = &layout;
  bool Run(bool reuse_texture_descriptors, uint32_t texture_count_vertex, uint32_t sampler_count_vertex,
           uint32_t texture_count_pixel, uint32_t sampler_count_pixel,
           const std::vector<VkDescriptorImageInfo>& vertex,
           const std::vector<VkDescriptorImageInfo>& pixel, bool fail) {
    const uint32_t texture_values_before = current_graphics_descriptor_set_values_up_to_date_;
    constexpr uint32_t vb = 1u << 2, pb = 1u << 3;
    current_graphics_descriptor_set_values_up_to_date_ &= ~(vb | pb);
    bool write_vertex_textures = !vertex.empty(), write_pixel_textures = !pixel.empty();
    const size_t vertex_texture_image_info_offset = 0, pixel_texture_image_info_offset = vertex.size();
    descriptor_write_image_info_ = vertex;
    descriptor_write_image_info_.insert(descriptor_write_image_info_.end(), pixel.begin(), pixel.end());
'''
code += decision
code += r'''
    // Allocation failures must leave misses invalid and must not publish snapshots.
    if (fail) return false;
    uint32_t write_descriptor_set_bits = 0;
    if (write_vertex_textures) { gpu_vertex = vertex; write_descriptor_set_bits |= vb; }
    if (write_pixel_textures) { gpu_pixel = pixel; write_descriptor_set_bits |= pb; }
'''
code += commit
code += r'''
    assert(vertex.empty() || Same(gpu_vertex, vertex));
    assert(pixel.empty() || Same(gpu_pixel, pixel));
    return true;
  }
};
int main() {
  WosTextureDescriptorSnapshot snapshot;
  std::vector<VkDescriptorImageInfo> inputs{{1, 2, 3}, {4, 5, 6}};
  snapshot.Store(10, 1, 1, inputs);
  assert(snapshot.Matches(10, 1, 1, inputs));
  for (int field = 0; field < 3; ++field) {
    auto changed = inputs;
    if (field == 0) ++changed[0].sampler;
    if (field == 1) ++changed[0].imageView;
    if (field == 2) ++changed[0].imageLayout;
    assert(!snapshot.Matches(10, 1, 1, changed));
  }
  assert(!snapshot.Matches(11, 1, 1, inputs));
  assert(!snapshot.Matches(10, 2, 0, inputs));
  VulkanCommandProcessor a, b;
  std::mt19937_64 random(0x57534f53);
  std::vector<VkDescriptorImageInfo> vertex{{0, 12, 5}, {99, 0, 0}};
  std::vector<VkDescriptorImageInfo> pixel{{0, 21, 5}, {44, 0, 0}};
  for (int i = 0; i < 20000; ++i) {
    auto& cp = i % 79 == 0 ? b : a;
    if (i % 37 == 0) cp.current_graphics_descriptor_set_values_up_to_date_ = 0;
    if (i % 5 == 0) pixel[0].imageView = 20 + random() % 5; // newly streamed texture
    if (i % 7 == 0) pixel[1].sampler = 40 + random() % 5;
    if (i % 11 == 0) vertex[0].imageLayout = 4 + random() % 3;
    if (i % 13 == 0) cp.layout.pixel = 11 + random() % 3;
    const bool enabled = i % 31 != 0, fail = i % 43 == 0;
    assert(cp.Run(enabled, 1, 1, 1, 1, vertex, pixel, fail) == !fail);
    if (i % 19 == 0) assert(cp.Run(true, 0, 0, 1, 1, {}, pixel, false));
    if (i % 23 == 0) assert(cp.Run(true, 2, 0, 1, 1, vertex, pixel, false));
  }
  // Disable with different inputs, then re-enable with old cached inputs.
  assert(a.Run(true, 1, 1, 1, 1, vertex, pixel, false));
  auto other = pixel; ++other[0].imageView;
  assert(a.Run(false, 1, 1, 1, 1, vertex, other, false));
  assert(a.Run(true, 1, 1, 1, 1, vertex, pixel, false));
  assert(wos_costs.texture_reuses > 1000 && wos_costs.texture_writes > 1000);
  std::printf("PASS: exact descriptor inputs; 20000 streaming/reset/owner/failure cases; toggle-off/on; reused %llu, wrote %llu\n",
              (unsigned long long)wos_costs.texture_reuses, (unsigned long long)wos_costs.texture_writes);
}
'''
args.work_dir.mkdir(parents=True, exist_ok=True)
path = args.work_dir / 'texture-descriptor-reuse-test.cpp'
path.write_text(code)
binary = args.work_dir / 'texture-descriptor-reuse-test'
subprocess.run([args.cxx, '-std=c++23', '-O2', '-fsanitize=address,undefined',
                str(path), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
