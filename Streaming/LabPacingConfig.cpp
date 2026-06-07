#include "pch.h"
#include "LabPacingConfig.h"
#include "../State/LabLogger.h"
#include "Utils.hpp"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>

using namespace moonlight_xbox_dx;

namespace {
	std::once_flag g_initOnce;
	std::string g_variantLabel = "default";
	int g_presentSyncInterval = ML_LAB_PRESENT_SYNC_INTERVAL_DEFAULT;
	bool g_manualPresentWait = true;
	double g_presentLeadMs = 0.0;
	int g_swapchainBuffers = ML_LAB_SWAPCHAIN_BUFFERS_DEFAULT;
	int g_frameQueueHwm = ML_LAB_FRAME_QUEUE_HWM_DEFAULT;
	int g_decoderThrottleMs = ML_LAB_DECODER_THROTTLE_MS_DEFAULT;
	bool g_noLockAroundPresent = ML_LAB_NO_LOCK_PRESENT_DEFAULT != 0;
	bool g_waitableSwapChain = false;
	int g_maxFrameLatency = 1;
	int g_textureRingSize = 1;
	bool g_loadedJsonConfig = false;

	int ReadIntSetting(const wchar_t* key, int fallback) {
		try {
			auto values = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
			auto boxed = values->Lookup(ref new Platform::String(key));
			auto prop = dynamic_cast<Windows::Foundation::IPropertyValue^>(boxed);
			if (prop == nullptr) {
				return fallback;
			}

			switch (prop->Type) {
			case Windows::Foundation::PropertyType::Int32:
				return prop->GetInt32();
			case Windows::Foundation::PropertyType::UInt32:
				return static_cast<int>(prop->GetUInt32());
			case Windows::Foundation::PropertyType::Boolean:
				return prop->GetBoolean() ? 1 : 0;
			default:
				return fallback;
			}
		}
		catch (...) {
			return fallback;
		}
	}

	std::wstring LocalConfigPath() {
		auto folder = Windows::Storage::ApplicationData::Current->LocalFolder->Path;
		return std::wstring(folder->Data()) + L"\\moonlight-lab-pacing.json";
	}

	bool ReadJsonConfig() {
		try {
			std::ifstream file(Utils::WideToNarrowString(LocalConfigPath()));
			if (!file) {
				return false;
			}

			nlohmann::json config = nlohmann::json::parse(file, nullptr, true, true);
			g_variantLabel = config.value("variant_label", g_variantLabel);
			g_presentSyncInterval = config.value("present_sync_interval", g_presentSyncInterval);
			g_manualPresentWait = config.value("manual_present_wait", g_manualPresentWait);
			g_presentLeadMs = config.value("present_lead_ms", g_presentLeadMs);
			g_swapchainBuffers = config.value("swapchain_buffers", g_swapchainBuffers);
			g_frameQueueHwm = config.value("frame_queue_hwm", g_frameQueueHwm);
			g_decoderThrottleMs = config.value("decoder_throttle_ms", g_decoderThrottleMs);
			g_noLockAroundPresent = config.value("no_lock_present", g_noLockAroundPresent);
			g_waitableSwapChain = config.value("waitable_swapchain", g_waitableSwapChain);
			g_maxFrameLatency = config.value("max_frame_latency", g_maxFrameLatency);
			g_textureRingSize = config.value("texture_ring_size", g_textureRingSize);
			Utils::Logf("Loaded lab pacing config from LocalState moonlight-lab-pacing.json\n");
			return true;
		}
		catch (const std::exception& e) {
			Utils::Logf("Failed to load moonlight-lab-pacing.json: %s\n", e.what());
			LabLogger::Event("lab_pacing_config_error", "\"error\":\"json_load_failed\"");
		}
		catch (...) {
			Utils::Logf("Failed to load moonlight-lab-pacing.json\n");
			LabLogger::Event("lab_pacing_config_error", "\"error\":\"json_load_failed\"");
		}
		return false;
	}

	std::wstring LocalVariantIndexPath() {
		auto folder = Windows::Storage::ApplicationData::Current->LocalFolder->Path;
		return std::wstring(folder->Data()) + L"\\moonlight-lab-variant-index.txt";
	}

	void SetVariant(const char* label,
	                int presentSyncInterval,
	                bool manualPresentWait,
	                double presentLeadMs,
	                int swapchainBuffers,
	                int textureRingSize,
	                bool waitableSwapChain) {
		g_variantLabel = label;
		g_presentSyncInterval = presentSyncInterval;
		g_manualPresentWait = manualPresentWait;
		g_presentLeadMs = presentLeadMs;
		g_swapchainBuffers = swapchainBuffers;
		g_frameQueueHwm = 1;
		g_textureRingSize = textureRingSize;
		g_waitableSwapChain = waitableSwapChain;
		g_maxFrameLatency = 1;
	}

	void ApplyAutoVariant() {
		int index = 0;
		try {
			std::ifstream in(Utils::WideToNarrowString(LocalVariantIndexPath()));
			if (in) {
				in >> index;
			}
		}
		catch (...) {
			index = 0;
		}

		switch (((index % 7) + 7) % 7) {
		case 0: SetVariant("A-current", 0, true, 0.0, 5, 1, false); break;
		case 1: SetVariant("B-candidate-lead2-buf3-ring3", 0, true, 2.0, 3, 3, false); break;
		case 2: SetVariant("C-sync1-candidate-buf3-ring3", 1, true, 2.0, 3, 3, false); break;
		case 3: SetVariant("D-sync1-nowait-buf3-ring3", 1, false, 0.0, 3, 3, false); break;
		case 4: SetVariant("E-lead3-buf3-ring3", 0, true, 3.0, 3, 3, false); break;
		case 5: SetVariant("F-lead2-buf2-ring3", 0, true, 2.0, 2, 3, false); break;
		case 6: SetVariant("G-waitable-buf3-ring3", 1, false, 0.0, 3, 3, true); break;
		}

		try {
			std::ofstream out(Utils::WideToNarrowString(LocalVariantIndexPath()), std::ios::trunc);
			out << (index + 1) << "\n";
		}
		catch (...) {
			Utils::Logf("Failed to write moonlight-lab-variant-index.txt\n");
		}
		Utils::Logf("Auto-selected lab pacing variant index=%d label=%s\n", index, g_variantLabel.c_str());
	}

	void InitOnce() {
		g_loadedJsonConfig = ReadJsonConfig();
		if (!g_loadedJsonConfig) {
			ApplyAutoVariant();
		}

		g_presentSyncInterval = std::clamp(ReadIntSetting(L"xbox_lab_present_interval", g_presentSyncInterval), 0, 1);
		g_manualPresentWait = ReadIntSetting(L"xbox_lab_manual_present_wait", g_manualPresentWait ? 1 : 0) != 0;
		g_presentLeadMs = std::clamp(g_presentLeadMs, 0.0, 8.0);
		g_swapchainBuffers = std::clamp(ReadIntSetting(L"xbox_lab_swapchain_buffers", g_swapchainBuffers), 2, 5);
		g_frameQueueHwm = std::clamp(ReadIntSetting(L"xbox_lab_frame_queue_hwm", g_frameQueueHwm), 1, g_swapchainBuffers);
		g_decoderThrottleMs = std::clamp(ReadIntSetting(L"xbox_lab_decoder_throttle_ms", g_decoderThrottleMs), 0, 5);
		g_noLockAroundPresent = ReadIntSetting(L"xbox_lab_no_lock_present", g_noLockAroundPresent ? 1 : 0) != 0;
		g_waitableSwapChain = ReadIntSetting(L"xbox_lab_waitable_swapchain", g_waitableSwapChain ? 1 : 0) != 0;
		g_maxFrameLatency = std::clamp(ReadIntSetting(L"xbox_lab_max_frame_latency", g_maxFrameLatency), 1, g_swapchainBuffers);
		g_textureRingSize = std::clamp(ReadIntSetting(L"xbox_lab_texture_ring_size", g_textureRingSize), 1, 5);

		Utils::Logf("Lab pacing config: variant=%s present_interval=%d manual_present_wait=%d present_lead_ms=%.3f swapchain_buffers=%d frame_queue_hwm=%d decoder_throttle_ms=%d no_lock_present=%d waitable_swapchain=%d max_frame_latency=%d texture_ring_size=%d\n",
		            g_variantLabel.c_str(),
		            g_presentSyncInterval,
		            g_manualPresentWait ? 1 : 0,
		            g_presentLeadMs,
		            g_swapchainBuffers,
		            g_frameQueueHwm,
		            g_decoderThrottleMs,
		            g_noLockAroundPresent ? 1 : 0,
		            g_waitableSwapChain ? 1 : 0,
		            g_maxFrameLatency,
		            g_textureRingSize);

		LabLogger::Event("lab_pacing_config", LabPacingConfig::TelemetryFields());
	}
}

void LabPacingConfig::Initialize() {
	std::call_once(g_initOnce, InitOnce);
}

const std::string& LabPacingConfig::VariantLabel() {
	Initialize();
	return g_variantLabel;
}

int LabPacingConfig::PresentSyncInterval() {
	Initialize();
	return g_presentSyncInterval;
}

bool LabPacingConfig::ManualPresentWait() {
	Initialize();
	return g_manualPresentWait;
}

double LabPacingConfig::PresentLeadMs() {
	Initialize();
	return g_presentLeadMs;
}

int LabPacingConfig::SwapChainBufferCount() {
	Initialize();
	return g_swapchainBuffers;
}

int LabPacingConfig::FrameQueueHighWaterMark() {
	Initialize();
	return g_frameQueueHwm;
}

int LabPacingConfig::DecoderThrottleMs() {
	Initialize();
	return g_decoderThrottleMs;
}

bool LabPacingConfig::NoLockAroundPresent() {
	Initialize();
	return g_noLockAroundPresent;
}

bool LabPacingConfig::WaitableSwapChain() {
	Initialize();
	return g_waitableSwapChain;
}

int LabPacingConfig::MaxFrameLatency() {
	Initialize();
	return g_maxFrameLatency;
}

int LabPacingConfig::TextureRingSize() {
	Initialize();
	return g_textureRingSize;
}

std::string LabPacingConfig::TelemetryFields() {
	return "\"variant_label\":\"" + g_variantLabel + "\"" +
		",\"present_interval\":" + std::to_string(g_presentSyncInterval) +
		",\"manual_present_wait\":" + std::to_string(g_manualPresentWait ? 1 : 0) +
		",\"present_lead_ms\":" + std::to_string(g_presentLeadMs) +
		",\"swapchain_buffers\":" + std::to_string(g_swapchainBuffers) +
		",\"frame_queue_hwm\":" + std::to_string(g_frameQueueHwm) +
		",\"decoder_throttle_ms\":" + std::to_string(g_decoderThrottleMs) +
		",\"no_lock_present\":" + std::to_string(g_noLockAroundPresent ? 1 : 0) +
		",\"waitable_swapchain\":" + std::to_string(g_waitableSwapChain ? 1 : 0) +
		",\"max_frame_latency\":" + std::to_string(g_maxFrameLatency) +
		",\"texture_ring_size\":" + std::to_string(g_textureRingSize) +
		",\"config_source\":\"" + (g_loadedJsonConfig ? std::string("json") : std::string("auto-cycle")) + "\"";
}
