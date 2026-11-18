# TERRAIN (shared, GhostBlock's own code): the landscape control surface. A plugin adds these sources to its own target and
# links juce_gui_basics + juce_data_structures. Include from the plugin's CMakeLists with:
#   include(${PATH_TO}/ghosty-pro/Shared/Terrain/Terrain.cmake)   then   target_sources(<target> PRIVATE ${GHOST_TERRAIN_SOURCES})
#   target_include_directories(<target> PRIVATE ${GHOST_TERRAIN_DIR})
set(GHOST_TERRAIN_DIR ${CMAKE_CURRENT_LIST_DIR})
set(GHOST_TERRAIN_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/TerrainModel.cpp
  ${CMAKE_CURRENT_LIST_DIR}/TerrainMapping.cpp
  ${CMAKE_CURRENT_LIST_DIR}/TerrainFeatures.cpp
  ${CMAKE_CURRENT_LIST_DIR}/TerrainSerial.cpp
  ${CMAKE_CURRENT_LIST_DIR}/TerrainInteraction.cpp
  ${CMAKE_CURRENT_LIST_DIR}/TerrainDsp.cpp
  ${CMAKE_CURRENT_LIST_DIR}/TerrainView.cpp)
