# SEANCE (GhostBlock) - BEAST. Surge XT's CMake expects to be the top-level project, so it is: this file is handed to it
# with -DCMAKE_PROJECT_INCLUDE=... and adds our two targets once Surge's own targets exist. Surge's files are not edited.
get_property(_beast_done GLOBAL PROPERTY SEANCE_BEAST_INJECTED)
if (NOT _beast_done)
  set_property(GLOBAL PROPERTY SEANCE_BEAST_INJECTED TRUE)
  # EVAL: the path is worked out now (a deferred call would read CMAKE_CURRENT_LIST_DIR later, in Surge's folder)
  cmake_language(EVAL CODE "cmake_language(DEFER DIRECTORY \"${CMAKE_SOURCE_DIR}\" CALL include \"${CMAKE_CURRENT_LIST_DIR}/beast-targets.cmake\")")
endif()
