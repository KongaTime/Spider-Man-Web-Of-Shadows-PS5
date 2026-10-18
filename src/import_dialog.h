#pragma once
#include <functional>
#include <rex/rex_app.h>
namespace wos {
void ShowImporter(rex::ui::ImGuiDrawer* drawer, rex::PathConfig paths,
                  std::function<void(rex::PathConfig)> resume);
}
