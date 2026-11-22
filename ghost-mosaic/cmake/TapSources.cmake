# The read-only tap (engine side). Owned by the tap engineer.
#   Tap.h / Tap.cpp          cells, the queue, snapshots, the engine's hooks (audio thread + worker)
#   TapWhy.h / TapWhy.cpp    the replay: why the engine picked a piece (no GUI, no engine access)
#   TapModel.h / TapModel.cpp  mosaic::tap::Model, what every view reads (studio/TAP.md)
list(APPEND GHOSTMOSAIC_ENGINE_SOURCES Source/Engine/Tap.cpp Source/Engine/TapWhy.cpp Source/Engine/TapModel.cpp)
