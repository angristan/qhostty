find_program(ZIG_EXECUTABLE zig REQUIRED)

get_filename_component(GHOSTTY_SOURCE_DIR
  "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(QHOSTTY_GHOSTTY_OPTIMIZE Debug CACHE STRING
  "Zig optimization mode used for Ghostty")
set(QHOSTTY_GHOSTTY_CPU baseline CACHE STRING
  "Zig CPU target used for Ghostty")
set(GHOSTTY_PREFIX "${CMAKE_BINARY_DIR}/ghostty")
set(GHOSTTY_SHARED "${GHOSTTY_PREFIX}/lib/ghostty-internal.so")
set(GHOSTTY_SONAME "${GHOSTTY_PREFIX}/lib/libghostty.so")
set(GHOSTTY_STATIC "${GHOSTTY_PREFIX}/lib/ghostty-internal.a")
set(GHOSTTY_HEADER "${GHOSTTY_PREFIX}/include/ghostty.h")

file(MAKE_DIRECTORY "${GHOSTTY_PREFIX}/include")

add_custom_target(qhostty_libghostty
  COMMAND "${ZIG_EXECUTABLE}" build
    -Demit-lib-vt=false
    -Dapp-runtime=none
    -Drenderer=opengl
    -Demit-exe=false
    -Demit-docs=false
    -Demit-macos-app=false
    -Demit-xcframework=false
    # Qt loads system Fontconfig too. A second static copy exports the same
    # symbols and can parse newer host configuration with an older parser.
    -fsys=fontconfig
    "-Doptimize=${QHOSTTY_GHOSTTY_OPTIMIZE}"
    "-Dcpu=${QHOSTTY_GHOSTTY_CPU}"
    --prefix "${GHOSTTY_PREFIX}"
  COMMAND "${CMAKE_COMMAND}" -E create_symlink
    "ghostty-internal.so" "${GHOSTTY_SONAME}"
  WORKING_DIRECTORY "${GHOSTTY_SOURCE_DIR}"
  BYPRODUCTS
    "${GHOSTTY_SHARED}"
    "${GHOSTTY_SONAME}"
    "${GHOSTTY_STATIC}"
    "${GHOSTTY_HEADER}"
  USES_TERMINAL
  VERBATIM
)

add_library(qhostty_ghostty SHARED IMPORTED GLOBAL)
add_library(Ghostty::Core ALIAS qhostty_ghostty)
set_target_properties(qhostty_ghostty PROPERTIES
  IMPORTED_LOCATION "${GHOSTTY_SHARED}"
  INTERFACE_INCLUDE_DIRECTORIES "${GHOSTTY_PREFIX}/include"
)
add_dependencies(qhostty_ghostty qhostty_libghostty)
