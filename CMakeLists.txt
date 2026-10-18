cmake_minimum_required(VERSION 3.25)
project(wos_ps5 LANGUAGES C CXX)
option(WOS_FRAME_COST_DIAGNOSTICS "Enable coarse renderer cost logging for PS5 tests" OFF)
option(WOS_REUSE_TEXTURE_BINDINGS "Test exact texture descriptor reuse" OFF)
set(WOS_COMMAND_RING_SPIN_LIMIT 500 CACHE STRING "GPU command-ring spin limit (0-500)")
if(NOT WOS_COMMAND_RING_SPIN_LIMIT MATCHES "^(0|[1-9][0-9]*)$" OR WOS_COMMAND_RING_SPIN_LIMIT GREATER 500)
  message(FATAL_ERROR "WOS_COMMAND_RING_SPIN_LIMIT must be an integer from 0 to 500")
endif()

set(WOS_GENERATED_DIR "" CACHE PATH "Directory containing wos_recomp_init.h and sources.cmake")
set(REXGLUE_SRC "" CACHE PATH "KongaTime/rexglue-sdk source checkout")
set(REXGLUE_PS5_BUILD "" CACHE PATH "Configured PS5 runtime build")
set(PS5_VULKAN_DIR "" CACHE PATH "KongaTime/PS5_Vulkan checkout")
set(PS5_PAYLOAD_SDK "" CACHE PATH "Installed pinned payload SDK")
foreach(required WOS_GENERATED_DIR REXGLUE_SRC REXGLUE_PS5_BUILD PS5_VULKAN_DIR PS5_PAYLOAD_SDK)
  if(NOT IS_DIRECTORY "${${required}}")
    message(FATAL_ERROR "Set ${required} to its local directory")
  endif()
endforeach()
if(NOT EXISTS "${WOS_GENERATED_DIR}/sources.cmake")
  message(FATAL_ERROR "Run Web of Shadows code generation before configuring")
endif()

# The PS5 mapping has the same physical-memory offset as Windows. This changes
# the generated host-address macro, not the guest instructions.
set(pch "${WOS_GENERATED_DIR}/wos_recomp_pch.h")
file(READ "${pch}" pch_text)
set(old_condition "#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64)")
set(new_condition "#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64) || REX_PLATFORM_PS5")
if(NOT pch_text MATCHES "REX_ARCH_ARM64\\) \\|\\| REX_PLATFORM_PS5")
  string(FIND "${pch_text}" "${old_condition}" condition_position)
  if(condition_position EQUAL -1)
    message(FATAL_ERROR "Unrecognized generated physical-memory offset condition")
  endif()
  string(REPLACE "${old_condition}" "${new_condition}" pch_text "${pch_text}")
  file(WRITE "${pch}" "${pch_text}")
endif()

set(rex_includes "${REXGLUE_SRC}/include" "${REXGLUE_PS5_BUILD}/include"
  "${REXGLUE_SRC}/thirdparty/fmt/include" "${REXGLUE_SRC}/thirdparty/spdlog/include"
  "${REXGLUE_SRC}/thirdparty/simde" "${REXGLUE_SRC}/thirdparty/simde/simde"
  "${REXGLUE_SRC}/thirdparty/utfcpp/source" "${REXGLUE_SRC}/thirdparty/imgui"
  "${REXGLUE_SRC}/thirdparty/tomlplusplus/include"
  "${REXGLUE_SRC}/thirdparty/xxHash"
  "${REXGLUE_SRC}/thirdparty/vulkan-headers/include"
  "${WOS_GENERATED_DIR}")

include("${WOS_GENERATED_DIR}/sources.cmake")
add_library(wos_guest OBJECT ${GENERATED_SOURCES})
add_library(wos_app OBJECT src/app.cpp src/title_main.cpp src/game_import.cpp src/import_dialog.cpp src/graphics_backend.cpp src/fast_clock.cpp src/guest_frame_diagnostics.cpp
  "${REXGLUE_SRC}/src/ui/rex_app.cpp")
set_source_files_properties(src/graphics_backend.cpp PROPERTIES
  INCLUDE_DIRECTORIES "${REXGLUE_SRC}/thirdparty/glslang;${REXGLUE_SRC}/thirdparty/renderdoc;${REXGLUE_SRC}/thirdparty/vulkan-memory-allocator/include")
foreach(target wos_guest wos_app)
  target_include_directories(${target} PRIVATE ${rex_includes} vendor/platform vendor/volk)
  target_compile_definitions(${target} PRIVATE SPDLOG_COMPILED_LIB SPDLOG_FMT_EXTERNAL
    REX_HAS_VULKAN=1 REXGLUE_BUILD_CONFIG="Release" NDEBUG)
  target_compile_options(${target} PRIVATE -std=c++23 -fexperimental-library
    -march=znver2 -mcmodel=large -msse4.1 -fno-strict-aliasing
    -fno-omit-frame-pointer -ffunction-sections -fdata-sections -O2)
endforeach()
target_compile_definitions(wos_app PRIVATE WOS_FRAME_COST_DIAGNOSTICS=$<BOOL:${WOS_FRAME_COST_DIAGNOSTICS}>)
target_compile_definitions(wos_app PRIVATE WOS_REUSE_TEXTURE_BINDINGS=$<BOOL:${WOS_REUSE_TEXTURE_BINDINGS}>)
target_compile_definitions(wos_app PRIVATE WOS_COMMAND_RING_SPIN_LIMIT=${WOS_COMMAND_RING_SPIN_LIMIT})
# Optimize direct calls across generated translation units while preserving
# the original floating-point and memory-access semantics.
target_compile_options(wos_guest PRIVATE -O3 -flto=thin)
target_precompile_headers(wos_guest PRIVATE "${pch}")
add_library(wos_platform OBJECT vendor/platform/platform.c vendor/volk/volk.c)
target_include_directories(wos_platform PRIVATE vendor/platform vendor/volk
  "${REXGLUE_SRC}/thirdparty/vulkan-headers/include")
target_compile_options(wos_platform PRIVATE -O2 -fno-omit-frame-pointer
  -ffunction-sections -fdata-sections)

set(runtime_dir "${REXGLUE_SRC}/out/ps5-amd64")
file(GLOB runtime_archives CONFIGURE_DEPENDS "${runtime_dir}/*.a")
set(objects "$<FILTER:$<TARGET_OBJECTS:wos_guest>,INCLUDE,\\.o$>"
  "$<TARGET_OBJECTS:wos_app>" "$<TARGET_OBJECTS:wos_platform>")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/eboot.elf"
  COMMAND bash "${CMAKE_SOURCE_DIR}/tools/link-title.sh" "${CMAKE_BINARY_DIR}"
    "${PS5_VULKAN_DIR}" "${PS5_PAYLOAD_SDK}" "${runtime_dir}" ${objects}
  DEPENDS wos_guest wos_app wos_platform ${objects} ${runtime_archives}
    tools/link-title.sh tools/wos-pie.ld tools/stage-notices.py tools/release_licenses.py
    tools/check-load-segments.py tools/runtime_patches.py
    patches/rexglue-frame-diagnostics.patch patches/rexglue-disable-phase-timing.patch
    patches/rexglue-single-range.patch patches/rexglue-valid-page-fast-path.patch
    patches/rexglue-ps5-settings-navigation.patch
    patches/rexglue-coarse-frame-costs.patch
    patches/rexglue-texture-descriptor-reuse.patch
    patches/rexglue-command-wait-breakdown.patch
    patches/rexglue-command-ring-wait.patch
    dependencies.lock.json LICENSE sce_sys/param.json sce_sys/icon0.png
  COMMAND_EXPAND_LISTS VERBATIM)
add_custom_target(title DEPENDS "${CMAKE_BINARY_DIR}/eboot.elf")
