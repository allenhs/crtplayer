cmake_minimum_required(VERSION 3.16)
project(CRTPlayer VERSION 2.8.0 LANGUAGES CXX)

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
  src/settings/CrtParams.cpp src/settings/CrtParams.h
  src/settings/PresetManager.cpp src/settings/PresetManager.h)
target_include_directories(crtcore PUBLIC src)
target_link_libraries(crtcore PUBLIC Qt6::Core Qt6::Gui)

add_executable(crtplayer
  src/main.cpp
  src/app/MainWindow.cpp      src/app/MainWindow.h
  src/app/Automation.cpp      src/app/Automation.h
  src/app/DeskWindow.cpp      src/app/DeskWindow.h
  src/playback/Player.cpp     src/playback/Player.h
  src/render/CrtRenderer.cpp  src/render/CrtRenderer.h
  src/render/VideoWidget.cpp  src/render/VideoWidget.h
  src/render/ViewSettings.h
  src/render/DeskRenderer.cpp src/render/DeskRenderer.h
  src/render/DeskView.cpp     src/render/DeskView.h
  src/ui/Icons.cpp            src/ui/Icons.h
  src/ui/JellyfinPanel.cpp    src/ui/JellyfinPanel.h
  src/playback/Thumbnailer.cpp src/playback/Thumbnailer.h
  src/app/SleepInhibitor.cpp  src/app/SleepInhibitor.h
  src/render/ModelLibrary.cpp src/render/ModelLibrary.h
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
  resources.qrc)
target_include_directories(crtplayer PRIVATE src)
target_link_libraries(crtplayer PRIVATE crtcore
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

if(CRTPLAYER_BUILD_TESTS)
  enable_testing()
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
endif()
