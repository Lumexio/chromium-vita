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
option(CHROMIUM_VITA_USE_MBEDTLS "Use mbedTLS for HTTP/HTTPS over sockets" ON)
set(CHROMIUM_VITA_MBEDTLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mbedtls" CACHE PATH
    "Path to mbedTLS source directory")

set(CHROMIUM_VITA_HTTP_CLIENT_SRC src/browser/http_client.cpp)
set(CHROMIUM_VITA_MBEDTLS_FOUND OFF)
set(CHROMIUM_VITA_MBEDTLS_PORT_SRC "")

if (CHROMIUM_VITA_USE_MBEDTLS)
  if (EXISTS "${CHROMIUM_VITA_MBEDTLS_DIR}/CMakeLists.txt")
    set(MBEDTLS_USER_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/vita/mbedtls_user_config.h"
      CACHE FILEPATH "Mbed TLS user config file" FORCE)
    set(TF_PSA_CRYPTO_USER_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/vita/tf_psa_user_config.h"
      CACHE FILEPATH "TF-PSA-Crypto user config file" FORCE)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "Disable Mbed TLS/TF-PSA programs" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "Disable Mbed TLS/TF-PSA tests" FORCE)
    add_subdirectory(${CHROMIUM_VITA_MBEDTLS_DIR} mbedtls_build)
    set(CHROMIUM_VITA_HTTP_CLIENT_SRC src/browser/http_client_mbedtls.cpp)
    set(CHROMIUM_VITA_MBEDTLS_PORT_SRC src/platform/vita/mbedtls_port.c)
    set(CHROMIUM_VITA_MBEDTLS_FOUND ON)
  else()
    message(WARNING "mbedTLS not found in ${CHROMIUM_VITA_MBEDTLS_DIR}. Falling back to SceHttp backend.")
    set(CHROMIUM_VITA_USE_MBEDTLS OFF)
  endif()
endif()

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
  ${CHROMIUM_VITA_HTTP_CLIENT_SRC}
  ${CHROMIUM_VITA_MBEDTLS_PORT_SRC}
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
  SceAppUtil_stub
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

if (CHROMIUM_VITA_MBEDTLS_FOUND)
  target_compile_definitions(${PROJECT_NAME} PRIVATE CHROMIUM_VITA_USE_MBEDTLS=1)
  target_include_directories(${PROJECT_NAME} PRIVATE
    ${CHROMIUM_VITA_MBEDTLS_DIR}/include
    ${CHROMIUM_VITA_MBEDTLS_DIR}/tf-psa-crypto/include
  )
  target_link_libraries(${PROJECT_NAME} mbedtls mbedx509)
  target_link_libraries(${PROJECT_NAME} SceLibKernel_stub)
endif()

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
