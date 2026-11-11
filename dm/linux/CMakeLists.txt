cmake_minimum_required(VERSION 3.22)
project(dm_linux C CXX ASM)

add_compile_definitions(_GNU_SOURCE DM_GAME_DOOM=1 DM_GAME_D3=1 DM_LINUX=1)
set(DM ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(HOST ${DM}/host)
get_filename_component(DM_REPO_ROOT ${DM}/.. ABSOLUTE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)

add_subdirectory(${DM}/gfx/vulkan ${CMAKE_CURRENT_BINARY_DIR}/vulkan)

# Diablo III uses the lean DOOM host core and does not need D2R-derived tables.
file(GLOB HLE_SRCS CONFIGURE_DEPENDS ${HOST}/hle_*.c)
list(REMOVE_ITEM HLE_SRCS
  ${HOST}/hle_tact.c ${HOST}/hle_granny.c ${HOST}/hle_charname.c
  ${HOST}/hle_private_aim.c ${HOST}/hle_target.c)
file(GLOB GFX_SRCS CONFIGURE_DEPENDS ${DM}/gfx/*.c ${DM}/gfx/nvn/*.c ${DM}/gfx/tex/*.c)
set(HOST_SRCS
  ${HOST}/crash.c ${HOST}/hle.c ${HOST}/loader.c ${HOST}/thread.c
  ${HOST}/dm_prof.c ${HOST}/dm_movie_decode.c ${HOST}/doom_stubs.c
  ${DM}/ios/Sources/dm_touch_controls.c)

set(ASM_SRCS)
foreach(source ${HOST}/entry.S ${DM}/gfx/nvn/nvn_entry.S ${DM}/gfx/nvn/nvn_thunks.S)
  get_filename_component(name ${source} NAME)
  set(output ${CMAKE_CURRENT_BINARY_DIR}/asm/${name})
  add_custom_command(OUTPUT ${output}
    COMMAND ${Python3_EXECUTABLE} ${DM}/android/tools/asm_elf.py ${source} ${output}
    DEPENDS ${source} ${DM}/android/tools/asm_elf.py VERBATIM)
  list(APPEND ASM_SRCS ${output})
endforeach()

add_library(dmcore STATIC
  ${HOST_SRCS} ${HLE_SRCS} ${GFX_SRCS} ${ASM_SRCS}
  ${DM}/gfx/vulkan/nvn_vk.c
  ${CMAKE_CURRENT_SOURCE_DIR}/front_linux.c
  ${CMAKE_CURRENT_SOURCE_DIR}/front_linux_ui.c
  ${CMAKE_CURRENT_SOURCE_DIR}/vulkan_present.cpp
  ${DM}/android/compat/clonefile.c ${DM}/android/compat/digest.c
  ${DM}/android/compat/mach_thread.c ${DM}/android/compat/mach_symbols.c)
set_source_files_properties(${HOST}/hle_thread.c PROPERTIES
  COMPILE_OPTIONS "-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=0")
target_include_directories(dmcore PUBLIC
  ${HOST} ${DM}/gfx/nvn/include ${DM}/gfx/vulkan
  ${DM}/ios/Sources ${DM}/android/compat/include)
target_compile_options(dmcore PUBLIC
  $<$<COMPILE_LANGUAGE:C>:-O1;-g;-Wall;-Wno-unused-function;-fno-strict-aliasing;-std=gnu17;-include${DM}/android/compat/include/dm_android_compat.h>
  $<$<COMPILE_LANGUAGE:CXX>:-O1;-g;-Wall;-Wno-unused-function;-fno-strict-aliasing;-std=gnu++17>)
target_link_libraries(dmcore PUBLIC dm_vulkan_engine Threads::Threads Vulkan::Vulkan
  PkgConfig::SDL2 PkgConfig::FREETYPE m dl)

add_executable(dmhost ${HOST}/main.c)
target_link_libraries(dmhost PRIVATE -Wl,--whole-archive dmcore -Wl,--no-whole-archive)
target_link_options(dmhost PRIVATE -Wl,--no-undefined -Wl,-z,nostart-stop-gc -Wl,--export-dynamic)
install(TARGETS dmhost RUNTIME DESTINATION bin)
