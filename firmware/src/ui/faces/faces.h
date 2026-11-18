#pragma once
#include "../common.h"

// Watch face registry. Each face builds its widgets into a 240x240 container and updates them from
// FaceData. Only the active face exists at a time. Remove a face: delete its file + its line here.
struct FaceDef {
    const char *name;
    void (*build)(lv_obj_t *parent);
    void (*update)(const FaceData &d);
};

namespace faces {

extern const FaceDef kAll[];
extern const int kCount;

} // namespace faces
