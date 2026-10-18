#pragma once
#include <memory>
#include <rex/graphics/graphics_system.h>

namespace wos {
std::unique_ptr<rex::graphics::GraphicsSystem> CreateGraphicsSystem();
}
