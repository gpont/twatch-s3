#include "faces.h"

extern const FaceDef kFaceMinimal, kFaceGameboy, kFacePanel, kFaceAnalog, kFaceTerminal, kFaceLcd, kFaceWords;

namespace faces {

const FaceDef kAll[] = {kFaceMinimal, kFaceGameboy, kFacePanel, kFaceAnalog, kFaceTerminal, kFaceLcd, kFaceWords};
const int kCount = sizeof(kAll) / sizeof(kAll[0]);

} // namespace faces
