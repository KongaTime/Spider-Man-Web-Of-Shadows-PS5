// Startup follows KongaTime/PS5_EdgeOfTimeTitle's MIT-licensed title entry.
#include "platform.h"
#include "volk.h"
#include <cstdio>

extern "C" int rex_ps5_app_main(int argc, char** argv);
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char* name);

extern "C" void prospero_startup_log(const char* message) {
  say("%s", message ? message : "(null)");
  if (FILE* log = std::fopen("/app0/wos-startup.log", "a")) {
    std::fprintf(log, "%s\n", message ? message : "(null)");
    std::fclose(log);
  }
}

int main() {
  platform_init("Web of Shadows");
  if (FILE* log = std::fopen("/app0/wos-startup.log", "w")) std::fclose(log);
  pad_open();
  volkInitializeCustom(vk_icdGetInstanceProcAddr);
  char program[] = "/app0/eboot.bin";
  char logging[] = "--log_level=info";
  char* arguments[] = {program, logging, nullptr};
  const int result = rex_ps5_app_main(2, arguments);
  say("Web of Shadows exited with status %d", result);
  return result;
}
