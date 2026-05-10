cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(chromium-vita CXX C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Chromium Vita")
set(VITA_TITLEID  "CVIT00001")
set(VITA_VERSION  "01.00")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CHROMIUM_VITA_NETSURF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/netsurf" CACHE PATH
    "Path to NetSurf source/scaffolding directory")

# Size-optimised release build
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-Os -ffunction-sections -fdata-sections)
  add_link_options(-Wl,--gc-sections)
endif()

add_library(netsurf_scaffold STATIC
  ${CHROMIUM_VITA_NETSURF_DIR}/scaffold/src/scaffold.c
)

target_include_directories(netsurf_scaffold PUBLIC
  ${CHROMIUM_VITA_NETSURF_DIR}/scaffold/include
)

add_executable(${PROJECT_NAME}
  src/main.cpp
  src/browser/http_client.cpp
  src/browser/session.cpp
  src/platform/vita/display.cpp
  src/platform/vita/ime.cpp
  src/platform/vita/input.cpp
  src/platform/vita/netsurf_frontend.cpp
  src/ui/shell.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
  src
  ${CHROMIUM_VITA_NETSURF_DIR}/scaffold/include
)

target_link_libraries(${PROJECT_NAME}
  vita2d
  SceDisplay_stub
  SceCtrl_stub
  SceTouch_stub
  SceSysmodule_stub
  SceHttp_stub
  SceSsl_stub
  SceNet_stub
  SceNetCtl_stub
  SceIme_stub
  SceGxm_stub
  SceCommonDialog_stub
  SceAppMgr_stub
  ScePgf_stub
  freetype
  png
  jpeg
  z
  m
  c
  netsurf_scaffold
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)

vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME    ${VITA_APP_NAME}
  FILE sce_sys/pic0.png                      sce_sys/pic0.png
  FILE sce_sys/icon0.png                     sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg0.png     sce_sys/livearea/contents/bg0.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
