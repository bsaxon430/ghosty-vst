# The new mode (screen side). Owned by the screen engineers. Example:
#   list(APPEND GHOSTMOSAIC_UI_SOURCES Source/Plugin/Scope/ScopePage.cpp)
# The shared part (leave these lines as they are):
list(APPEND GHOSTMOSAIC_UI_SOURCES
  Source/Plugin/ParamRow.cpp
  Source/Plugin/Scope/ScopePage.cpp
  Source/Plugin/Scope/ActiveView.cpp
  Source/Plugin/Scope/BaselineView.cpp)
# A design's own files go below, one per line (see studio/README.md):
#   list(APPEND GHOSTMOSAIC_UI_SOURCES Source/Plugin/Scope/MyView.cpp)
list(APPEND GHOSTMOSAIC_UI_SOURCES Source/Plugin/Scope/RiverView.cpp)
