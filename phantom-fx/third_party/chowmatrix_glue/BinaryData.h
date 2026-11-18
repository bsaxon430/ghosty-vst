// Phantom FX glue for ChowMatrix: its resources (res/, as its res/CMakeLists.txt lists them) are ChowMatrixBinaryData; included inside
// namespace chowmatrix, where they are BinaryData as ChowMatrix and its modules expect.
#pragma once
namespace BinaryData = ::ChowMatrixBinaryData;
