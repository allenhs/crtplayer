#pragma once
#include "render/Geometry.h"
#include "settings/CrtParams.h"

// Everything that decides how the picture is presented, shared by the flat view and
// the 3D desk view so both compute identical CRT output.
struct ViewSettings {
    CrtParams params;
    bool bypass = false;
    bool compare = false;
    double split = 0.5;
    ScaleMode mode = ScaleMode::Fit;
    CropFractions crop;
    double aspectOverride = 0;
};
