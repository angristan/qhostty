cmake_minimum_required(VERSION 3.25)
project(qhostty VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

include(cmake/Ghostty.cmake)

qt_add_executable(qhostty
  src/main.cpp
  src/GhosttyApp.cpp
  src/GhosttyApp.h
  src/MainWindow.cpp
  src/MainWindow.h
  src/TerminalWidget.cpp
  src/TerminalWidget.h
)
add_dependencies(qhostty qhostty_libghostty)

target_compile_features(qhostty PRIVATE cxx_std_20)
target_compile_options(qhostty PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(qhostty PRIVATE
  Ghostty::Core
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::OpenGL
  Qt6::OpenGLWidgets
)

set_target_properties(qhostty PROPERTIES
  OUTPUT_NAME qhostty
  BUILD_RPATH "${GHOSTTY_PREFIX}/lib"
  INSTALL_RPATH "$ORIGIN/../lib/qhostty"
)

include(GNUInstallDirs)
install(TARGETS qhostty RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${GHOSTTY_SHARED}"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/qhostty"
  RENAME "libghostty.so"
)
