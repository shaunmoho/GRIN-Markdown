# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)
project(GRIN-Markdown LANGUAGES CXX RC)

if(NOT MSVC)
  message(FATAL_ERROR "GRIN-Markdown is built with MSVC.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the DLL has no runtime dependencies besides Windows itself.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(CMAKE_GENERATOR_PLATFORM MATCHES "^[Ww]in32$" OR CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(WV2_ARCH x86)
elseif(CMAKE_GENERATOR_PLATFORM MATCHES "^ARM64$")
  set(WV2_ARCH arm64)
else()
  set(WV2_ARCH x64)
endif()

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/build/native/include/WebView2.h)
  message(FATAL_ERROR "WebView2 SDK is missing. Run build.ps1, which downloads it into third_party/webview2.")
endif()

set(WEB_DIST ${CMAKE_CURRENT_SOURCE_DIR}/web/dist)
if(NOT EXISTS ${WEB_DIST}/index.html)
  message(FATAL_ERROR "web/dist is missing. Run 'npm ci && npm run build' in the web folder first (build.ps1 does this).")
endif()

add_library(GRIN-Markdown SHARED
  src/Plugin.cpp
  src/PreviewPanel.cpp
  src/PreviewPanel.h
  src/Settings.cpp
  src/Settings.h
  src/ComHandler.h
  src/resource.h
  src/GRIN-Markdown.rc
)

target_include_directories(GRIN-Markdown PRIVATE
  third_party/npp
  third_party/webview2/build/native/include
)
target_compile_definitions(GRIN-Markdown PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(GRIN-Markdown PRIVATE /W4 /permissive- /utf-8 /wd4100)
target_link_options(GRIN-Markdown PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>)
target_link_libraries(GRIN-Markdown PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/build/native/${WV2_ARCH}/WebView2LoaderStatic.lib
  shlwapi shell32 ole32 user32 gdi32 advapi32 version
)

# Lay the output out exactly like the installed plugin folder:
#   GRIN-Markdown/GRIN-Markdown.dll
#   GRIN-Markdown/web/...
set_target_properties(GRIN-Markdown PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/GRIN-Markdown
)
add_custom_command(TARGET GRIN-Markdown POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${WEB_DIST} $<TARGET_FILE_DIR:GRIN-Markdown>/web
  COMMAND ${CMAKE_COMMAND} -E remove -f $<TARGET_FILE_DIR:GRIN-Markdown>/web/preview.js.LEGAL.txt
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md $<TARGET_FILE_DIR:GRIN-Markdown>/
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE $<TARGET_FILE_DIR:GRIN-Markdown>/LICENSE.txt
  VERBATIM
)
