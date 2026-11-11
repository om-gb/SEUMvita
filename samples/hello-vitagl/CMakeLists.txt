cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(hello_vitagl C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Hello vitaGL")
set(VITA_TITLEID  "SEUM00001")
set(VITA_VERSION  "01.00")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O2 -g")
# -na (no ASLR) keeps addresses stable so psp2-tooling profilers can symbolize.
set(VITA_MAKE_FSELF_FLAGS "${VITA_MAKE_FSELF_FLAGS} -na")

add_executable(${PROJECT_NAME} main.c)
target_link_libraries(${PROJECT_NAME}
  SDL2 vitaGL vitashark SceShaccCgExt mathneon taihen_stub
  SceShaccCg_stub SceKernelDmacMgr_stub SceGxm_stub SceDisplay_stub
  SceCtrl_stub SceTouch_stub SceHid_stub SceMotion_stub SceAudio_stub
  SceAudioIn_stub SceSysmodule_stub SceAppMgr_stub SceCommonDialog_stub
  SceIme_stub SceAppUtil_stub SceKernelThreadMgr_stub SceLibKernel_stub
  SceIofilemgr_stub ScePower_stub stdc++ pthread m c)

vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME})
