cmake_minimum_required(VERSION 3.25)
project(qhostty VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core DBus Gui Network Widgets OpenGL OpenGLWidgets)
find_package(LayerShellQt QUIET)
find_package(KF6WindowSystem QUIET)
find_package(KF6GlobalAccel QUIET)

include(cmake/Ghostty.cmake)

qt_add_library(qhostty_frontend STATIC
  src/CommandPalette.cpp
  src/CommandPalette.h
  src/GhosttyApp.cpp
  src/GhosttyApp.h
  src/InspectorWindow.cpp
  src/InspectorWindow.h
  src/GlobalShortcuts.cpp
  src/GlobalShortcuts.h
  src/MainWindow.cpp
  src/MainWindow.h
  src/QuickTerminal.cpp
  src/QuickTerminal.h
  src/SingleInstance.cpp
  src/SingleInstance.h
  src/SplitAlgorithms.cpp
  src/SplitAlgorithms.h
  src/TerminalTab.cpp
  src/TerminalTab.h
  src/TerminalWidget.cpp
  src/TerminalWidget.h
)
add_dependencies(qhostty_frontend qhostty_libghostty)
target_include_directories(qhostty_frontend PUBLIC src)
target_compile_features(qhostty_frontend PUBLIC cxx_std_20)
target_compile_options(qhostty_frontend PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(qhostty_frontend PUBLIC
  Ghostty::Core
  Qt6::Core
  Qt6::DBus
  Qt6::Gui
  Qt6::Network
  Qt6::Widgets
  Qt6::OpenGL
  Qt6::OpenGLWidgets
)
if(TARGET LayerShellQt::Interface)
  target_link_libraries(qhostty_frontend PRIVATE LayerShellQt::Interface)
  target_compile_definitions(qhostty_frontend PRIVATE
    QHOSTTY_HAS_LAYER_SHELL=1)
endif()
if(TARGET KF6::WindowSystem)
  target_link_libraries(qhostty_frontend PRIVATE KF6::WindowSystem)
  target_compile_definitions(qhostty_frontend PRIVATE
    QHOSTTY_HAS_KWINDOWSYSTEM=1)
endif()
if(TARGET KF6::GlobalAccel)
  target_link_libraries(qhostty_frontend PRIVATE KF6::GlobalAccel)
  target_compile_definitions(qhostty_frontend PRIVATE
    QHOSTTY_HAS_GLOBAL_ACCEL=1)
endif()

qt_add_executable(qhostty src/main.cpp)
target_compile_options(qhostty PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(qhostty PRIVATE qhostty_frontend)
set_target_properties(qhostty PROPERTIES
  OUTPUT_NAME qhostty
  BUILD_RPATH "${GHOSTTY_PREFIX}/lib"
  INSTALL_RPATH "$ORIGIN/../lib/qhostty"
)

include(CTest)
if(BUILD_TESTING)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
  qt_add_executable(qhostty_split_algorithms_test
    tests/tst_SplitAlgorithms.cpp
    src/SplitAlgorithms.cpp
    src/SplitAlgorithms.h
  )
  add_dependencies(qhostty_split_algorithms_test qhostty_libghostty)
  target_include_directories(qhostty_split_algorithms_test PRIVATE
    src
    "${GHOSTTY_PREFIX}/include"
  )
  target_compile_features(qhostty_split_algorithms_test PRIVATE cxx_std_20)
  target_compile_options(qhostty_split_algorithms_test PRIVATE
    -Wall -Wextra -Wpedantic)
  target_link_libraries(qhostty_split_algorithms_test PRIVATE
    Qt6::Core
    Qt6::Test
  )
  add_test(NAME qhostty_split_algorithms
    COMMAND qhostty_split_algorithms_test)

  qt_add_executable(qhostty_window_actions_test
    tests/tst_WindowActions.cpp)
  target_compile_options(qhostty_window_actions_test PRIVATE
    -Wall -Wextra -Wpedantic)
  target_link_libraries(qhostty_window_actions_test PRIVATE
    qhostty_frontend
    Qt6::Test
  )
  set_target_properties(qhostty_window_actions_test PROPERTIES
    BUILD_RPATH "${GHOSTTY_PREFIX}/lib")
  add_test(NAME qhostty_window_actions COMMAND qhostty_window_actions_test)
  set_tests_properties(qhostty_window_actions PROPERTIES
    ENVIRONMENT
      "QT_QPA_PLATFORM=offscreen;QHOSTTY_DISABLE_GLOBAL_SHORTCUTS=1")

  qt_add_executable(qhostty_single_instance_test
    tests/tst_SingleInstance.cpp
    src/SingleInstance.cpp
    src/SingleInstance.h
  )
  target_include_directories(qhostty_single_instance_test PRIVATE src)
  target_compile_features(qhostty_single_instance_test PRIVATE cxx_std_20)
  target_compile_options(qhostty_single_instance_test PRIVATE
    -Wall -Wextra -Wpedantic)
  target_link_libraries(qhostty_single_instance_test PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
  )
  add_test(NAME qhostty_single_instance COMMAND qhostty_single_instance_test)
endif()

include(GNUInstallDirs)
set(QHOSTTY_APP_ID "io.github.angristan.qhostty")

install(TARGETS qhostty RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${GHOSTTY_SHARED}"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/qhostty"
  RENAME "libghostty.so"
)
install(FILES resources/linux/${QHOSTTY_APP_ID}.desktop
  DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES resources/linux/${QHOSTTY_APP_ID}.metainfo.xml
  DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
install(FILES resources/linux/${QHOSTTY_APP_ID}-servicemenu.desktop
  DESTINATION "${CMAKE_INSTALL_DATADIR}/kio/servicemenus"
  RENAME "${QHOSTTY_APP_ID}.desktop")
foreach(size 16 32 64 128 256 512)
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../images/gnome/${size}.png"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps"
    RENAME "${QHOSTTY_APP_ID}.png")
endforeach()
