#pragma once
#include <memory>
#include <vector>

#include "inference/segmentation_backend.h"

namespace promatte {

// Probes which execution backends are actually usable on this machine.
// Result is cached after the first call (probing can take ~100 ms).
const std::vector<BackendCapabilities> &enumerateBackends();

// Picks the best available backend for BackendKind::Auto:
//   TensorRT > CUDA > DirectML > CoreML > OpenVINO > CPU (only those actually available).
BackendKind resolveAutoBackend();

// Returns the concrete backend implementation for the kind (all current kinds are
// execution providers of the ONNX Runtime backend).
std::unique_ptr<SegmentationBackend> createBackend(BackendKind kind);

bool backendAvailable(BackendKind kind);

// Chooses the DXGI adapter index for GPU inference: the adapter whose name
// matches `preferName` (typically the adapter OBS renders on), else the
// hardware adapter with the most dedicated video memory. Returns 0 if unknown.
int preferredGpuAdapter(const std::string &preferName);

} // namespace promatte
