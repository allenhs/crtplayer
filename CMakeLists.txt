cmake_minimum_required(VERSION 3.16)
project(CRTPlayer VERSION 2.16.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(CRTPLAYER_BUILD_TESTS "Build unit tests" ON)

# D-Bus (media keys, keep-awake) is Linux-only; Windows uses its own calls.
if(WIN32)
  find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Network)
else()
  find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Network DBus)
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
  gstreamer-1.0>=1.18
  gstreamer-app-1.0
  gstreamer-video-1.0
  gstreamer-audio-1.0
  gstreamer-pbutils-1.0
  gstreamer-tag-1.0)

# Pure logic shared by the app and the tests (no GL, no GStreamer).
add_library(crtcore STATIC
  src/render/Geometry.cpp  src/render/Geometry.h
  src/render/FmvPalette.cpp src/render/FmvPalette.h
  src/tv/TvSchedule.cpp    src/tv/TvSchedule.h
  src/settings/CrtParams.cpp src/settings/CrtParams.h
  src/settings/PresetManager.cpp src/settings/PresetManager.h)
target_include_directories(crtcore PUBLIC src)
target_link_libraries(crtcore PUBLIC Qt6::Core Qt6::Gui)

# Your 3D models for the 90s CG room: the readers (OBJ, STL, PLY, glTF; FBX through ufbx).
add_library(crtmodels STATIC
  src/render/ModelLibrary.cpp src/render/ModelLibrary.h
  src/render/ModelFormats.cpp src/render/ModelRaw.h
  third_party/ufbx/ufbx.c     third_party/ufbx/ufbx.h)
target_include_directories(crtmodels PUBLIC src PRIVATE third_party/ufbx)
target_link_libraries(crtmodels PUBLIC Qt6::Core Qt6::Gui)
if(NOT WIN32)
  target_link_libraries(crtmodels PRIVATE m)
endif()

add_executable(crtplayer
  src/main.cpp
  src/app/MainWindow.cpp      src/app/MainWindow.h
  src/app/Everyday.cpp
  src/app/FastPath.cpp
  src/app/EnhanceUi.cpp
  src/app/Automation.cpp      src/app/Automation.h
  src/app/DeskWindow.cpp      src/app/DeskWindow.h
  src/playback/Player.cpp     src/playback/Player.h
  src/playback/SubtitleFeed.cpp src/playback/SubtitleFeed.h src/playback/AssScript.cpp src/playback/AssScript.h
  src/render/CrtRenderer.cpp
    src/render/Enhance.cpp  src/render/CrtRenderer.h
  src/render/NvEnhancer.cpp   src/render/NvEnhancer.h
  src/render/GlSurfaceWidget.cpp src/render/GlSurfaceWidget.h
  src/render/VideoWidget.cpp  src/render/VideoWidget.h
  src/render/ViewSettings.h
  src/render/DeskRenderer.cpp src/render/DeskRenderer.h
  src/render/DeskView.cpp     src/render/DeskView.h
  src/ui/Icons.cpp            src/ui/Icons.h
  src/ui/JellyfinPanel.cpp    src/ui/JellyfinPanel.h
  src/playback/Thumbnailer.cpp src/playback/Thumbnailer.h
  src/app/SleepInhibitor.cpp  src/app/SleepInhibitor.h
  src/playback/TapeAudio.cpp  src/playback/TapeAudio.h
  src/playback/Distro.cpp     src/playback/Distro.h
  src/app/Gamepad.cpp         src/app/Gamepad.h
  src/settings/ResumeStore.cpp src/settings/ResumeStore.h
  src/jellyfin/JellyfinClient.cpp src/jellyfin/JellyfinClient.h
  src/ui/WheelGuard.h
  src/ui/Theme.cpp            src/ui/Theme.h
  src/ui/ControlBar.cpp       src/ui/ControlBar.h
  src/ui/CrtPanel.cpp         src/ui/CrtPanel.h
  src/ui/DisplayPanel.cpp     src/ui/DisplayPanel.h
  src/ui/PlaybackPanel.cpp    src/ui/PlaybackPanel.h
  src/ui/PlaylistPanel.cpp    src/ui/PlaylistPanel.h
  src/settings/AppSettings.cpp  src/settings/AppSettings.h
  src/edit/LosslessCutter.cpp src/edit/LosslessCutter.h
  src/tv/TvController.cpp     src/tv/TvController.h
  src/ui/TvPanel.cpp          src/ui/TvPanel.h
  src/ui/CutDialog.cpp        src/ui/CutDialog.h
  src/edit/GifEncoder.cpp     src/edit/GifEncoder.h
  src/edit/GifRecorder.cpp    src/edit/GifRecorder.h
  src/ui/GifDialog.cpp        src/ui/GifDialog.h
  resources.qrc)
target_include_directories(crtplayer PRIVATE src)
target_link_libraries(crtplayer PRIVATE crtcore crtmodels
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Network PkgConfig::GST)
if(WIN32)
  # A windowed app (no console window); the icon and version details come from the resource file.
  enable_language(RC)
  set_target_properties(crtplayer PROPERTIES WIN32_EXECUTABLE ON)
  set(CRTPLAYER_ICO "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/crtplayer.ico")
  configure_file(packaging/windows/crtplayer.rc.in ${CMAKE_CURRENT_BINARY_DIR}/crtplayer.rc @ONLY)
  target_sources(crtplayer PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/crtplayer.rc)
else()
  target_sources(crtplayer PRIVATE src/app/Mpris.cpp src/app/Mpris.h)
  target_link_libraries(crtplayer PRIVATE Qt6::DBus)
endif()
target_compile_definitions(crtplayer PRIVATE CRTPLAYER_VERSION="${PROJECT_VERSION}")
install(TARGETS crtplayer RUNTIME DESTINATION bin)

# NVIDIA's AI methods for Enhance (Linux): a helper program beside the player. It opens the
# NVIDIA Video Effects SDK at run time, from wherever the user installed it; nothing of the
# SDK is needed to build it (the three API headers in third_party/nvidia-vfx are MIT-licensed).
# Its C++ runtime is linked in, so that the SDK's libraries meet no second copy of it.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(crtplayer-nvfx
    tools/nvfx/main.cpp tools/nvfx/Serve.cpp
    tools/nvfx/NvFx.cpp tools/nvfx/NvFx.h tools/nvfx/NvShm.h
    tools/nvfx/NvProxy.cpp tools/nvfx/NvProxy.h)
  set_target_properties(crtplayer-nvfx PROPERTIES AUTOMOC OFF AUTORCC OFF CXX_VISIBILITY_PRESET hidden)
  target_include_directories(crtplayer-nvfx PRIVATE third_party/nvidia-vfx)
  target_link_libraries(crtplayer-nvfx PRIVATE ${CMAKE_DL_LIBS} pthread)
  # (Some distributions keep the static C++ runtime in a package of its own: without it, the shared one.)
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_LINK_OPTIONS -static-libstdc++ -static-libgcc)
  check_cxx_source_compiles("#include <string>\nint main() { std::string s(\"x\"); return int(s.size()) - 1; }" CRTPLAYER_STATIC_CXX_RUNTIME)
  unset(CMAKE_REQUIRED_LINK_OPTIONS)
  if(CRTPLAYER_STATIC_CXX_RUNTIME)
    target_link_options(crtplayer-nvfx PRIVATE -static-libstdc++ -static-libgcc)
  endif()
  install(TARGETS crtplayer-nvfx RUNTIME DESTINATION bin)
endif()

if(CRTPLAYER_BUILD_TESTS)
  enable_testing()
  find_package(Python3 COMPONENTS Interpreter)
  add_executable(test_geometry tests/test_geometry.cpp)
  target_link_libraries(test_geometry PRIVATE crtcore)
  add_test(NAME geometry COMMAND test_geometry)
  add_executable(test_presets tests/test_presets.cpp)
  target_link_libraries(test_presets PRIVATE crtcore)
  add_test(NAME presets COMMAND test_presets)
  add_executable(test_tape tests/test_tape.cpp src/playback/TapeAudio.cpp)
  target_include_directories(test_tape PRIVATE src)
  target_link_libraries(test_tape PRIVATE PkgConfig::GST)
  add_test(NAME tape COMMAND test_tape)
  add_executable(test_ass tests/test_ass.cpp src/playback/AssScript.cpp)
  target_include_directories(test_ass PRIVATE src)
  target_link_libraries(test_ass PRIVATE Qt6::Core)
  add_test(NAME ass COMMAND test_ass)
  add_executable(test_tv tests/test_tv.cpp)
  target_link_libraries(test_tv PRIVATE crtcore)
  add_test(NAME tv COMMAND test_tv)
  add_executable(test_fmv tests/test_fmv.cpp)
  target_link_libraries(test_fmv PRIVATE crtcore)
  add_test(NAME fmv COMMAND test_fmv)
  add_executable(test_models tests/test_models.cpp)
  target_link_libraries(test_models PRIVATE crtmodels)
  add_test(NAME models COMMAND test_models)
  add_executable(fuzz_models tests/fuzz_models.cpp)   # damaged files; run by hand (see VERIFICATION.md)
  target_link_libraries(fuzz_models PRIVATE crtmodels)
  add_executable(test_gif tests/test_gif.cpp src/edit/GifEncoder.cpp src/edit/GifEncoder.h)
  target_include_directories(test_gif PRIVATE src)
  target_link_libraries(test_gif PRIVATE Qt6::Core Qt6::Gui)
  add_test(NAME gif COMMAND test_gif)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND Python3_Interpreter_FOUND)
    # The helper against a stand-in for the SDK (plain arithmetic in place of the AI models).
    set(NVFX_MOCK ${CMAKE_CURRENT_BINARY_DIR}/nvfx-mock-sdk)
    file(MAKE_DIRECTORY ${NVFX_MOCK}/lib ${NVFX_MOCK}/features/nvvfxvideosuperres/lib ${NVFX_MOCK}/features/nvvfxvideoframegeneration/lib)
    add_library(NVCVImage SHARED tests/nvfx_mock/mock_image.cpp)
    add_library(VideoFX SHARED tests/nvfx_mock/mock_vfx.cpp)
    target_link_libraries(VideoFX PRIVATE NVCVImage)
    foreach(t NVCVImage VideoFX)
      target_include_directories(${t} PRIVATE third_party/nvidia-vfx)
      set_target_properties(${t} PROPERTIES AUTOMOC OFF AUTORCC OFF CXX_VISIBILITY_PRESET hidden LIBRARY_OUTPUT_DIRECTORY ${NVFX_MOCK}/lib)
    endforeach()
    add_test(NAME nvfx COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/nvfx_serve_test.py $<TARGET_FILE:crtplayer-nvfx> ${NVFX_MOCK})
  endif()
  # Command-line front end to the lossless cutter, for the verification scripts.
  add_executable(cut_tool tests/cut_tool.cpp src/edit/LosslessCutter.cpp src/edit/LosslessCutter.h)
  target_include_directories(cut_tool PRIVATE src)
  target_link_libraries(cut_tool PRIVATE Qt6::Core PkgConfig::GST)
endif()
