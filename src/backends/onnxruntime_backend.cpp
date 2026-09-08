#include "backends/onnxruntime_backend.h"

#include <algorithm>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#endif
#ifdef PROMATTE_HAVE_DIRECTML
#include <DirectML.h>
#include <dml_provider_factory.h>
#endif

#include "utils/file_utils.h"
#include "utils/logging.h"
#include "utils/system_info.h"
#include "utils/timer.h"

namespace promatte {

namespace {

void ORT_API_CALL ortLogCallback(void *, OrtLoggingLevel severity, const char *, const char *, const char *,
				 const char *message)
{
	switch (severity) {
	case ORT_LOGGING_LEVEL_ERROR:
	case ORT_LOGGING_LEVEL_FATAL:
		PM_LOG_ERROR("onnxruntime: %s", message);
		break;
	case ORT_LOGGING_LEVEL_WARNING:
		PM_LOG_WARN("onnxruntime: %s", message);
		break;
	default:
		PM_LOG_DEBUG("onnxruntime: %s", message);
		break;
	}
}

} // namespace

const char *backendKindName(BackendKind kind)
{
	switch (kind) {
	case BackendKind::Auto:
		return "auto";
	case BackendKind::DirectML:
		return "directml";
	case BackendKind::CUDA:
		return "cuda";
	case BackendKind::TensorRT:
		return "tensorrt";
	case BackendKind::CoreML:
		return "coreml";
	case BackendKind::OpenVINO:
		return "openvino";
	case BackendKind::CPU:
		return "cpu";
	}
	return "auto";
}

BackendKind backendKindFromString(const std::string &s)
{
	if (s == "directml")
		return BackendKind::DirectML;
	if (s == "cuda")
		return BackendKind::CUDA;
	if (s == "tensorrt")
		return BackendKind::TensorRT;
	if (s == "coreml")
		return BackendKind::CoreML;
	if (s == "openvino")
		return BackendKind::OpenVINO;
	if (s == "cpu")
		return BackendKind::CPU;
	return BackendKind::Auto;
}

#if defined(_WIN32) && defined(PROMATTE_HAVE_DIRECTML)
struct OnnxRuntimeBackend::DmlDevice {
	Microsoft::WRL::ComPtr<ID3D12Device> d3d12;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
	Microsoft::WRL::ComPtr<IDMLDevice> dml;
};

// Creates the D3D12 + DirectML device on a specific DXGI adapter and hands it to
// ONNX Runtime (DML1 API). Falls back to the plain device_id API on failure.
bool OnnxRuntimeBackend::appendDirectMLProvider(Ort::SessionOptions &so, int adapterIndex, std::string &deviceName)
{
	using Microsoft::WRL::ComPtr;
	deviceName.clear();
	try {
		const OrtDmlApi *dmlApi = nullptr;
		Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION,
									reinterpret_cast<const void **>(&dmlApi)));
		ComPtr<IDXGIFactory4> factory;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
			throw std::runtime_error("CreateDXGIFactory1 failed");
		ComPtr<IDXGIAdapter1> adapter;
		if (factory->EnumAdapters1(UINT(std::max(adapterIndex, 0)), &adapter) == DXGI_ERROR_NOT_FOUND)
			throw std::runtime_error("adapter index out of range");
		DXGI_ADAPTER_DESC1 desc{};
		adapter->GetDesc1(&desc);
		char name[256]{};
		WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
		deviceName = name;

		auto dev = std::make_unique<DmlDevice>();
		if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev->d3d12))))
			throw std::runtime_error("D3D12CreateDevice failed");
		D3D12_COMMAND_QUEUE_DESC qd{};
		qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		if (FAILED(dev->d3d12->CreateCommandQueue(&qd, IID_PPV_ARGS(&dev->queue))))
			throw std::runtime_error("CreateCommandQueue failed");
		// DirectML.dll is already loaded by onnxruntime.dll; resolve DMLCreateDevice from it.
		HMODULE dmlLib = LoadLibraryW(L"DirectML.dll");
		if (!dmlLib)
			throw std::runtime_error("DirectML.dll not found");
		using CreateFn = HRESULT(WINAPI *)(ID3D12Device *, DML_CREATE_DEVICE_FLAGS, REFIID, void **);
		auto createFn = reinterpret_cast<CreateFn>(GetProcAddress(dmlLib, "DMLCreateDevice"));
		if (!createFn)
			throw std::runtime_error("DMLCreateDevice not exported");
		if (FAILED(createFn(dev->d3d12.Get(), DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&dev->dml))))
			throw std::runtime_error("DMLCreateDevice failed");
		Ort::ThrowOnError(dmlApi->SessionOptionsAppendExecutionProvider_DML1(so, dev->dml.Get(), dev->queue.Get()));
		dmlDevice_ = std::move(dev);
		PM_LOG_INFO("DirectML on adapter %d: %s", adapterIndex, deviceName.c_str());
		return true;
	} catch (const std::exception &e) {
		PM_LOG_WARN("explicit DirectML adapter selection failed (%s); using default device %d", e.what(),
			    adapterIndex);
		dmlDevice_.reset();
		try {
			Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(so, adapterIndex));
			return true;
		} catch (const std::exception &e2) {
			PM_LOG_ERROR("DirectML provider unavailable: %s", e2.what());
			return false;
		}
	}
}
#endif

Ort::Env &OnnxRuntimeBackend::env()
{
	static Ort::Env instance(ORT_LOGGING_LEVEL_WARNING, "promatte", &ortLogCallback, nullptr);
	return instance;
}

std::vector<std::string> OnnxRuntimeBackend::availableProviders()
{
	try {
		env();
		return Ort::GetAvailableProviders();
	} catch (const std::exception &e) {
		PM_LOG_ERROR("GetAvailableProviders failed: %s", e.what());
		return {};
	}
}

OnnxRuntimeBackend::OnnxRuntimeBackend(BackendKind kind)
	: kind_(kind),
	  cpuMemory_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault))
{
	caps_.kind = kind;
	caps_.name = backendKindName(kind);
	caps_.isGpu = kind != BackendKind::CPU;
}

OnnxRuntimeBackend::~OnnxRuntimeBackend()
{
	shutdown();
}


bool OnnxRuntimeBackend::initialize(const BackendConfig &config, const ModelDescriptor &model,
				    const std::string &modelPath, std::string &error)
{
	shutdown();
	config_ = config;
	model_ = model;
	cancelled_ = false;
	Stopwatch sw;
	try {
		env();
		sessionOptions_ = std::make_unique<Ort::SessionOptions>();
		auto &so = *sessionOptions_;
		so.SetGraphOptimizationLevel(config.enableGraphOptimization ? ORT_ENABLE_ALL : ORT_DISABLE_ALL);
		so.SetLogSeverityLevel(ORT_LOGGING_LEVEL_WARNING);

		const unsigned cores = std::max(1u, sysinfo::logicalCores());
		int threads = config.cpuThreads;
		if (threads <= 0) {
			// Conservative default: leave cores for OBS encoding/rendering.
			// Leave at least one core for OBS' render/encode threads.
			threads = kind_ == BackendKind::CPU
					  ? int(std::clamp(std::min(cores / 2, cores > 1 ? cores - 1 : 1u), 1u, 4u))
					  : 1;
		}

		switch (kind_) {
		case BackendKind::DirectML: {
#if defined(PROMATTE_HAVE_DIRECTML) && defined(_WIN32)
			so.DisableMemPattern();
			so.SetExecutionMode(ORT_SEQUENTIAL);
			so.SetIntraOpNumThreads(threads);
			std::string devName;
			if (!appendDirectMLProvider(so, config.deviceIndex, devName)) {
				error = "DirectML provider could not be created";
				return false;
			}
			caps_.deviceName = devName;
			if (caps_.deviceName.empty()) {
				auto gpus = sysinfo::enumerateGpus();
				for (const auto &g : gpus)
					if (g.adapterIndex == config.deviceIndex)
						caps_.deviceName = g.name;
			}
#else
			error = "DirectML support not compiled in";
			return false;
#endif
			break;
		}
		case BackendKind::CUDA: {
			OrtCUDAProviderOptionsV2 *cudaOpts = nullptr;
			Ort::ThrowOnError(Ort::GetApi().CreateCUDAProviderOptions(&cudaOpts));
			std::unique_ptr<OrtCUDAProviderOptionsV2, decltype(Ort::GetApi().ReleaseCUDAProviderOptions)> guard(
				cudaOpts, Ort::GetApi().ReleaseCUDAProviderOptions);
			std::string dev = std::to_string(config.deviceIndex);
			const char *keys[] = {"device_id"};
			const char *vals[] = {dev.c_str()};
			Ort::ThrowOnError(Ort::GetApi().UpdateCUDAProviderOptions(cudaOpts, keys, vals, 1));
			so.AppendExecutionProvider_CUDA_V2(*cudaOpts);
			so.SetIntraOpNumThreads(threads);
			caps_.deviceName = "CUDA device " + dev;
			break;
		}
		case BackendKind::TensorRT: {
			OrtTensorRTProviderOptionsV2 *trtOpts = nullptr;
			Ort::ThrowOnError(Ort::GetApi().CreateTensorRTProviderOptions(&trtOpts));
			std::unique_ptr<OrtTensorRTProviderOptionsV2, decltype(Ort::GetApi().ReleaseTensorRTProviderOptions)>
				guard(trtOpts, Ort::GetApi().ReleaseTensorRTProviderOptions);
			std::string dev = std::to_string(config.deviceIndex);
			const char *keys[] = {"device_id", "trt_fp16_enable", "trt_engine_cache_enable"};
			const char *vals[] = {dev.c_str(), "1", "1"};
			Ort::ThrowOnError(Ort::GetApi().UpdateTensorRTProviderOptions(trtOpts, keys, vals, 3));
			so.AppendExecutionProvider_TensorRT_V2(*trtOpts);
			so.SetIntraOpNumThreads(threads);
			caps_.deviceName = "TensorRT device " + dev;
			break;
		}
		case BackendKind::CoreML: {
			std::unordered_map<std::string, std::string> opts;
			opts["MLComputeUnits"] = "ALL";
			so.AppendExecutionProvider("CoreML", opts);
			caps_.deviceName = "Apple Neural Engine / GPU";
			break;
		}
		case BackendKind::OpenVINO: {
			std::unordered_map<std::string, std::string> opts;
			opts["device_type"] = "GPU";
			so.AppendExecutionProvider("OpenVINO", opts);
			caps_.deviceName = "OpenVINO GPU";
			break;
		}
		case BackendKind::CPU:
		case BackendKind::Auto:
			so.SetIntraOpNumThreads(threads);
			so.SetInterOpNumThreads(1);
			caps_.deviceName = sysinfo::cpuName() + " (" + std::to_string(threads) + " threads)";
			caps_.isGpu = false;
			break;
		}

#ifdef _WIN32
		std::wstring wpath = fs::toWide(modelPath);
		session_ = std::make_unique<Ort::Session>(env(), wpath.c_str(), so);
#else
		session_ = std::make_unique<Ort::Session>(env(), modelPath.c_str(), so);
#endif
		if (!discoverIo(error)) {
			shutdown();
			return false;
		}

		// Device-resident recurrent state (avoids a CPU round-trip of ~3 MB per frame).
		useDeviceBinding_ = false;
		if (model_.recurrent && kind_ == BackendKind::DirectML) {
			try {
				// The DML EP registers its allocator as device 0 regardless of the DXGI adapter.
				deviceMemory_ = std::make_unique<Ort::MemoryInfo>("DML", OrtDeviceAllocator, 0, OrtMemTypeDefault);
				binding_ = std::make_unique<Ort::IoBinding>(*session_);
				useDeviceBinding_ = true;
			} catch (const std::exception &e) {
				PM_LOG_WARN("DML IO binding unavailable (%s); recurrent state will round-trip via CPU", e.what());
				deviceMemory_.reset();
				binding_.reset();
			}
		}
		caps_.supportsDeviceBinding = useDeviceBinding_;
		caps_.available = true;
		resetRecurrentState();
	} catch (const Ort::Exception &e) {
		error = std::string("ONNX Runtime: ") + e.what();
		shutdown();
		return false;
	} catch (const std::exception &e) {
		error = std::string("backend init failed: ") + e.what();
		shutdown();
		return false;
	}
	PM_LOG_INFO("backend %s initialised model '%s' in %.0f ms (device: %s, device binding: %s)", caps_.name.c_str(),
		    model_.id.c_str(), sw.elapsedMs(), caps_.deviceName.c_str(), useDeviceBinding_ ? "yes" : "no");
	return true;
}

bool OnnxRuntimeBackend::discoverIo(std::string &error)
{
	Ort::AllocatorWithDefaultOptions alloc;
	io_ = IoNames{};
	const size_t nIn = session_->GetInputCount();
	const size_t nOut = session_->GetOutputCount();
	std::vector<std::string> inputs, outputs;
	for (size_t i = 0; i < nIn; ++i)
		inputs.emplace_back(session_->GetInputNameAllocated(i, alloc).get());
	for (size_t i = 0; i < nOut; ++i)
		outputs.emplace_back(session_->GetOutputNameAllocated(i, alloc).get());

	if (model_.recurrent) {
		for (const auto &n : inputs) {
			if (n == "src" || n == model_.inputName)
				io_.src = n;
			else if (n == "downsample_ratio")
				io_.ratio = n;
			else if (n.size() == 3 && n[0] == 'r' && n[2] == 'i')
				io_.recIn.push_back(n);
		}
		for (const auto &n : outputs) {
			if (n == "pha" || n == model_.outputName)
				io_.alpha = n;
			else if (n == "fgr")
				io_.foreground = n;
			else if (n.size() == 3 && n[0] == 'r' && n[2] == 'o')
				io_.recOut.push_back(n);
		}
		std::sort(io_.recIn.begin(), io_.recIn.end());
		std::sort(io_.recOut.begin(), io_.recOut.end());
		if (io_.src.empty() || io_.alpha.empty() || io_.recIn.size() != io_.recOut.size() || io_.recIn.empty()) {
			error = "recurrent model has unexpected inputs/outputs";
			return false;
		}
	} else {
		if (inputs.empty() || outputs.empty()) {
			error = "model has no inputs/outputs";
			return false;
		}
		io_.src = model_.inputName.empty() ? inputs.front() : model_.inputName;
		io_.alpha = model_.outputName.empty() ? outputs.front() : model_.outputName;
		if (std::find(inputs.begin(), inputs.end(), io_.src) == inputs.end()) {
			error = "input '" + io_.src + "' not found in model";
			return false;
		}
		if (std::find(outputs.begin(), outputs.end(), io_.alpha) == outputs.end()) {
			error = "output '" + io_.alpha + "' not found in model";
			return false;
		}
	}

	// Log shapes for diagnostics.
	for (size_t i = 0; i < nIn; ++i) {
		// TypeInfo::GetTensorTypeAndShapeInfo() hands back a non-owning view, so
		// the TypeInfo has to outlive it. Calling it on the temporary returned by
		// GetInputTypeInfo() left the view dangling: harmless-looking on ONNX
		// Runtime 1.24 but on 1.23 the garbage dimension count made GetShape()
		// throw length_error and every model failed to load.
		Ort::TypeInfo typeInfo = session_->GetInputTypeInfo(i);
		auto info = typeInfo.GetTensorTypeAndShapeInfo();
		auto shape = info.GetShape();
		std::string s;
		for (auto d : shape)
			s += (s.empty() ? "" : "x") + (d < 0 ? std::string("?") : std::to_string(d));
		PM_LOG_DEBUG("model input %zu '%s' shape %s", i, inputs[i].c_str(), s.c_str());
	}
	return true;
}

void OnnxRuntimeBackend::resetRecurrentState()
{
	recState_.clear();
	stateValid_ = false;
	stateW_ = stateH_ = 0;
	stateRatio_ = 0.f;
}

void OnnxRuntimeBackend::resetState()
{
	resetRecurrentState();
}

void OnnxRuntimeBackend::cancel()
{
	cancelled_ = true;
	try {
		runOptions_.SetTerminate();
	} catch (...) {
	}
}

void OnnxRuntimeBackend::shutdown()
{
	binding_.reset();
	recState_.clear();
	deviceMemory_.reset();
	session_.reset();
	sessionOptions_.reset();
#if defined(_WIN32) && defined(PROMATTE_HAVE_DIRECTML)
	dmlDevice_.reset();
#endif
	stateValid_ = false;
	caps_.available = false;
}

bool OnnxRuntimeBackend::processFrame(const InferenceInput &input, InferenceOutput &output, std::string &error)
{
	if (!session_) {
		error = "backend not initialised";
		return false;
	}
	if (cancelled_) {
		error = "cancelled";
		return false;
	}
	try {
		return model_.recurrent ? runRecurrent(input, output, error) : runSimple(input, output, error);
	} catch (const Ort::Exception &e) {
		if (useDeviceBinding_) {
			// Device-resident state binding is an optimisation; fall back to the
			// portable CPU round-trip path once and retry this frame.
			PM_LOG_WARN("device binding failed (%s); using CPU round-trip for recurrent state", e.what());
			useDeviceBinding_ = false;
			caps_.supportsDeviceBinding = false;
			binding_.reset();
			resetRecurrentState();
			try {
				return runRecurrent(input, output, error);
			} catch (const Ort::Exception &e2) {
				error = std::string("inference failed: ") + e2.what();
				resetRecurrentState();
				return false;
			}
		}
		error = std::string("inference failed: ") + e.what();
		resetRecurrentState();
		return false;
	} catch (const std::exception &e) {
		error = std::string("inference failed: ") + e.what();
		return false;
	}
}

bool OnnxRuntimeBackend::runSimple(const InferenceInput &input, InferenceOutput &output, std::string &error)
{
	const size_t count = size_t(input.width) * input.height * 3;
	Ort::Value in = Ort::Value::CreateTensor<float>(cpuMemory_, const_cast<float *>(input.data), count,
							input.shape.data(), input.shape.size());
	const char *inNames[] = {io_.src.c_str()};
	const char *outNames[] = {io_.alpha.c_str()};
	auto results = session_->Run(runOptions_, inNames, &in, 1, outNames, 1);
	if (results.empty() || !results[0].IsTensor()) {
		error = "model returned no tensor";
		return false;
	}
	auto info = results[0].GetTensorTypeAndShapeInfo();
	auto shape = info.GetShape();
	if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
		error = "model output is not float32";
		return false;
	}
	output.width = input.width;
	output.height = input.height;
	output.alpha.resize(size_t(input.width) * input.height);
	output.foreground.clear();
	if (!decodeModelOutput(model_, results[0].GetTensorData<float>(), shape, input.width, input.height,
			       output.alpha.data())) {
		std::string s;
		for (auto d : shape)
			s += (s.empty() ? "" : "x") + std::to_string(d);
		error = "unexpected output shape " + s + " for " + std::to_string(input.width) + "x" +
			std::to_string(input.height);
		return false;
	}
	return true;
}

bool OnnxRuntimeBackend::runRecurrent(const InferenceInput &input, InferenceOutput &output, std::string &error)
{
	const size_t nRec = io_.recIn.size();
	const bool needReset = input.resetState || !stateValid_ || stateW_ != input.width || stateH_ != input.height ||
			       stateRatio_ != input.downsampleRatio || recState_.size() != nRec;
	if (needReset)
		resetRecurrentState();

	const size_t count = size_t(input.width) * input.height * 3;
	Ort::Value src = Ort::Value::CreateTensor<float>(cpuMemory_, const_cast<float *>(input.data), count,
							 input.shape.data(), input.shape.size());
	float ratio = input.downsampleRatio > 0.f ? input.downsampleRatio : 1.f;
	const int64_t ratioShape[1] = {1};
	Ort::Value ratioVal = Ort::Value::CreateTensor<float>(cpuMemory_, &ratio, 1, ratioShape, 1);

	// Initial recurrent state: RVM accepts zeros of shape (1,1,1,1) and broadcasts.
	std::vector<Ort::Value> zeroStates;
	if (recState_.empty()) {
		zeroState_.assign(1, 0.f);
		const int64_t zshape[4] = {1, 1, 1, 1};
		for (size_t i = 0; i < nRec; ++i)
			zeroStates.push_back(Ort::Value::CreateTensor<float>(cpuMemory_, zeroState_.data(), 1, zshape, 4));
	}

	const bool wantFg = model_.providesForeground && !io_.foreground.empty();
	std::vector<Ort::Value> results;
	std::vector<std::string> outOrder; // names in result order

	if (useDeviceBinding_ && binding_) {
		binding_->ClearBoundInputs();
		binding_->ClearBoundOutputs();
		binding_->BindInput(io_.src.c_str(), src);
		if (!io_.ratio.empty())
			binding_->BindInput(io_.ratio.c_str(), ratioVal);
		for (size_t i = 0; i < nRec; ++i)
			binding_->BindInput(io_.recIn[i].c_str(), recState_.empty() ? zeroStates[i] : recState_[i]);
		binding_->BindOutput(io_.alpha.c_str(), cpuMemory_);
		outOrder.push_back(io_.alpha);
		if (wantFg) {
			binding_->BindOutput(io_.foreground.c_str(), cpuMemory_);
			outOrder.push_back(io_.foreground);
		}
		for (size_t i = 0; i < nRec; ++i) {
			binding_->BindOutput(io_.recOut[i].c_str(), *deviceMemory_);
			outOrder.push_back(io_.recOut[i]);
		}
		session_->Run(runOptions_, *binding_);
		results = binding_->GetOutputValues();
		// ORT returns outputs sorted by name; map by name.
		auto names = binding_->GetOutputNames();
		std::vector<Ort::Value> ordered;
		ordered.reserve(outOrder.size());
		for (const auto &want : outOrder) {
			bool found = false;
			for (size_t k = 0; k < names.size(); ++k) {
				if (names[k] == want) {
					ordered.push_back(std::move(results[k]));
					found = true;
					break;
				}
			}
			if (!found) {
				error = "bound output '" + want + "' missing";
				return false;
			}
		}
		results = std::move(ordered);
	} else {
		std::vector<const char *> inNames{io_.src.c_str()};
		std::vector<Ort::Value> inValues;
		inValues.push_back(std::move(src));
		if (!io_.ratio.empty()) {
			inNames.push_back(io_.ratio.c_str());
			inValues.push_back(std::move(ratioVal));
		}
		for (size_t i = 0; i < nRec; ++i) {
			inNames.push_back(io_.recIn[i].c_str());
			inValues.push_back(recState_.empty() ? std::move(zeroStates[i]) : std::move(recState_[i]));
		}
		recState_.clear();
		std::vector<const char *> outNames{io_.alpha.c_str()};
		outOrder.push_back(io_.alpha);
		if (wantFg) {
			outNames.push_back(io_.foreground.c_str());
			outOrder.push_back(io_.foreground);
		}
		for (size_t i = 0; i < nRec; ++i) {
			outNames.push_back(io_.recOut[i].c_str());
			outOrder.push_back(io_.recOut[i]);
		}
		results = session_->Run(runOptions_, inNames.data(), inValues.data(), inValues.size(), outNames.data(),
					outNames.size());
	}

	if (results.size() != outOrder.size()) {
		error = "unexpected number of outputs";
		return false;
	}

	// Alpha
	{
		auto info = results[0].GetTensorTypeAndShapeInfo();
		auto shape = info.GetShape();
		output.width = input.width;
		output.height = input.height;
		output.alpha.resize(size_t(input.width) * input.height);
		ModelDescriptor alphaDesc = model_;
		alphaDesc.outputKind = OutputKind::Alpha;
		alphaDesc.outputLayout = TensorLayout::NCHW;
		if (!decodeModelOutput(alphaDesc, results[0].GetTensorData<float>(), shape, input.width, input.height,
				       output.alpha.data())) {
			error = "unexpected alpha shape";
			return false;
		}
	}
	// Foreground colour (planar RGB)
	size_t recBase = 1;
	if (wantFg) {
		auto info = results[1].GetTensorTypeAndShapeInfo();
		auto shape = info.GetShape();
		const size_t n = size_t(input.width) * input.height;
		if (shape.size() == 4 && shape[1] == 3 && size_t(shape[2]) == input.height && size_t(shape[3]) == input.width) {
			output.foreground.resize(3 * n);
			const float *fg = results[1].GetTensorData<float>();
			for (size_t i = 0; i < 3 * n; ++i)
				output.foreground[i] = std::clamp(fg[i], 0.f, 1.f);
		} else {
			output.foreground.clear();
		}
		recBase = 2;
	} else {
		output.foreground.clear();
	}
	// Keep recurrent state for the next frame.
	recState_.clear();
	for (size_t i = 0; i < nRec; ++i)
		recState_.push_back(std::move(results[recBase + i]));
	stateValid_ = true;
	stateW_ = input.width;
	stateH_ = input.height;
	stateRatio_ = input.downsampleRatio;
	return true;
}

} // namespace promatte
