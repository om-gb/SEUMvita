cmake_minimum_required(VERSION 3.16)
# One engine, two targets:
#   Linux: cmake -S engine -B build/linux && cmake --build build/linux
#   Vita:  cmake -S engine -B build/vita -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
project(seum CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(SOURCES src/main.cpp src/platform.cpp src/assets.cpp src/level.cpp)

if(VITA)
  include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
  set(VITA_APP_NAME "SEUM")
  set(VITA_TITLEID  "SEUM00001")
  set(VITA_VERSION  "00.02")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -O2 -g -mcpu=cortex-a9 -mfpu=neon -ffast-math")
  # -na: no ASLR, so psp2-tooling profilers can map symbols from the ELF.
  set(VITA_MAKE_FSELF_FLAGS "${VITA_MAKE_FSELF_FLAGS} -na")
  # vdpm's prebuilt libjansson.a is position-independent (R_ARM_BASE_PREL
  # relocations), which vita-elf-create rejects. Build it here without PIC.
  include(FetchContent)
  FetchContent_Declare(jansson_src
    URL https://github.com/akheron/jansson/releases/download/v2.14/jansson-2.14.tar.gz
    URL_HASH SHA256=5798d010e41cf8d76b66236cfb2f2543c8d082181d16bc3085ab49538d4b9929
    # its CMakeLists sets POSITION_INDEPENDENT_CODE on the static library too
    PATCH_COMMAND sed -i "s/POSITION_INDEPENDENT_CODE true/POSITION_INDEPENDENT_CODE false/" CMakeLists.txt)
  set(JANSSON_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(JANSSON_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(JANSSON_WITHOUT_TESTS ON CACHE BOOL "" FORCE)
  set(JANSSON_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
  FetchContent_MakeAvailable(jansson_src)
  add_executable(seum ${SOURCES})
  target_include_directories(seum BEFORE PRIVATE ${jansson_src_BINARY_DIR}/include)
  target_link_libraries(seum
    SDL2 vitaGL vitashark SceShaccCgExt mathneon taihen_stub jansson
    SceShaccCg_stub SceKernelDmacMgr_stub SceGxm_stub SceDisplay_stub
    SceCtrl_stub SceTouch_stub SceHid_stub SceMotion_stub SceAudio_stub
    SceAudioIn_stub SceSysmodule_stub SceAppMgr_stub SceCommonDialog_stub
    SceIme_stub SceAppUtil_stub SceKernelThreadMgr_stub SceLibKernel_stub
    SceIofilemgr_stub ScePower_stub stdc++ pthread m c)
  vita_create_self(eboot.bin seum UNSAFE)
  vita_create_vpk(seum.vpk ${VITA_TITLEID} eboot.bin VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME})
else()
  find_package(SDL2 REQUIRED)
  find_package(OpenGL REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(JANSSON REQUIRED jansson)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter")
  add_executable(seum ${SOURCES})
  target_include_directories(seum PRIVATE ${SDL2_INCLUDE_DIRS} ${JANSSON_INCLUDE_DIRS})
  target_link_libraries(seum ${SDL2_LIBRARIES} OpenGL::GL ${JANSSON_LIBRARIES} m)
endif()
