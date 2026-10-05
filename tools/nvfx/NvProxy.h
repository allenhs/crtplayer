#pragma once
#include <string>

// Opens the NVIDIA Video Effects SDK's libraries from <sdkRoot>/lib. The libraries they
// need in turn (CUDA, TensorRT, the features) are found through LD_LIBRARY_PATH, which
// must have been set before this process started (see nvLibraryPath in NvFx.h).
bool nvProxyLoad(const std::string& sdkRoot, std::string* error);
bool nvProxyLoaded();
