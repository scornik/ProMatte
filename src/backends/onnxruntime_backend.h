#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

#include "inference/segmentation_backend.h"

namespace promatte {

// ONNX Runtime backend. All GPU vendors are reached through execution providers:
//   DirectML (Windows, any D3D12 GPU), CUDA / TensorRT (when an ORT build with those
//   providers is installed), CoreML (macOS), OpenVINO (Intel) and CPU.
// Which providers exist is discovered at runtime; nothing is assumed.
class OnnxRuntimeBackend final : public SegmentationBackend {
public:
	explicit OnnxRuntimeBackend(BackendKind kind);
	~OnnxRuntimeBackend() override;

	bool initialize(const BackendConfig &config, const ModelDescriptor &model, const std::string &modelPath,
			std::string &error) override;
	bool processFrame(const InferenceInput &input, InferenceOutput &output, std::string &error) override;
	void shutdown() override;
	BackendCapabilities capabilities() const override { return caps_; }
	void resetState() override;
	void cancel() override;
	bool isInitialized() const override { return session_ != nullptr; }

	// Shared process-wide environment (thread-safe, created on first use).
	static Ort::Env &env();
	static std::vector<std::string> availableProviders();

private:
	struct IoNames {
		std::string src;
		std::vector<std::string> recIn;  // r1i..r4i
		std::vector<std::string> recOut; // r1o..r4o
		std::string ratio;               // downsample_ratio
		std::string alpha;               // pha / output
		std::string foreground;          // fgr (optional)
	};

	bool discoverIo(std::string &error);
	bool runRecurrent(const InferenceInput &input, InferenceOutput &output, std::string &error);
	bool runSimple(const InferenceInput &input, InferenceOutput &output, std::string &error);
	void resetRecurrentState();

	BackendKind kind_;
	BackendCapabilities caps_;
	BackendConfig config_;
	ModelDescriptor model_;
	std::unique_ptr<Ort::Session> session_;
	std::unique_ptr<Ort::SessionOptions> sessionOptions_;
	Ort::RunOptions runOptions_;
	Ort::MemoryInfo cpuMemory_;
	std::unique_ptr<Ort::MemoryInfo> deviceMemory_;
	std::unique_ptr<Ort::IoBinding> binding_;
	IoNames io_;
	std::vector<Ort::Value> recState_; // recurrent state values (device or CPU)
	std::vector<float> zeroState_;
	bool useDeviceBinding_ = false;
	bool stateValid_ = false;
	uint32_t stateW_ = 0, stateH_ = 0;
	float stateRatio_ = 0.f;
	std::atomic<bool> cancelled_{false};

	// D3D12/DirectML objects created for explicit adapter selection (DML1 API).
	struct DmlDevice;
	std::unique_ptr<DmlDevice> dmlDevice_;
	bool appendDirectMLProvider(Ort::SessionOptions &so, int adapterIndex, std::string &deviceName);
};

} // namespace promatte
