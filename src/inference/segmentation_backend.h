#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "inference/model_descriptor.h"

namespace promatte {

enum class BackendKind { Auto = 0, DirectML, CUDA, TensorRT, CoreML, OpenVINO, CPU };

struct BackendCapabilities {
	BackendKind kind = BackendKind::CPU;
	std::string name;       // "DirectML", "CPU", ...
	std::string deviceName; // e.g. "NVIDIA GeForce RTX 3060"
	bool isGpu = false;
	bool available = false;
	bool supportsDynamicShapes = true;
	bool supportsDeviceBinding = false; // recurrent state can stay on device
};

struct BackendConfig {
	BackendKind kind = BackendKind::Auto;
	int deviceIndex = 0;   // GPU adapter index (DirectML/CUDA)
	int cpuThreads = 0;    // 0 = auto (conservative)
	bool allowFallback = true;
	bool enableGraphOptimization = true;
};

struct InferenceInput {
	const float *data = nullptr;   // preprocessed tensor
	std::vector<int64_t> shape;    // e.g. {1,3,H,W} or {1,H,W,3}
	uint32_t width = 0;
	uint32_t height = 0;
	float downsampleRatio = 1.0f;  // for recurrent RVM-style models
	bool resetState = false;       // discontinuity: reset recurrent state before this frame
};

struct InferenceOutput {
	std::vector<float> alpha;      // w*h, [0,1]
	std::vector<float> foreground; // 3*w*h planar RGB in [0,1] when the model provides it, else empty
	uint32_t width = 0;
	uint32_t height = 0;
};

// Abstract synchronous inference backend. One instance is owned by the inference
// worker thread; all methods are called from that thread except cancel().
class SegmentationBackend {
public:
	virtual ~SegmentationBackend() = default;
	virtual bool initialize(const BackendConfig &config, const ModelDescriptor &model, const std::string &modelPath,
				std::string &error) = 0;
	virtual bool processFrame(const InferenceInput &input, InferenceOutput &output, std::string &error) = 0;
	virtual void shutdown() = 0;
	virtual BackendCapabilities capabilities() const = 0;
	virtual void resetState() {}
	// May be called from any thread to abort an in-flight processFrame().
	virtual void cancel() {}
	virtual bool isInitialized() const = 0;
};

const char *backendKindName(BackendKind kind);
BackendKind backendKindFromString(const std::string &s);

} // namespace promatte
