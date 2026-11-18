# SEANCE (GhostBlock) - BEAST: the C door over Surge's engine library, and its own check.
set(BEAST_DIR "${CMAKE_CURRENT_LIST_DIR}")
add_library(beast STATIC "${BEAST_DIR}/beast.cpp")
target_include_directories(beast PUBLIC "${BEAST_DIR}")
target_link_libraries(beast PUBLIC surge-common)
set_target_properties(beast PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON POSITION_INDEPENDENT_CODE ON)
add_executable(beast-check "${BEAST_DIR}/beast-check.cpp")
target_link_libraries(beast-check PRIVATE beast)
