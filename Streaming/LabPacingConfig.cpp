#include "pch.h"
#include "LabPacingConfig.h"
#include "../State/LabLogger.h"
#include "Utils.hpp"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>

using namespace moonlight_xbox_dx;

namespace {
	std::mutex g_configMutex;
	std::atomic<bool> g_initialized{false};
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
	bool g_leadAwareFrameWait = false;
	bool g_skipLatePresent = false;
	bool g_loadedJsonConfig = false;
	bool g_preparedForPendingStream = false;
	ULONGLONG g_lastStreamConfigMs = 0;

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
			g_leadAwareFrameWait = config.value("lead_aware_frame_wait", g_leadAwareFrameWait);
			g_skipLatePresent = config.value("skip_late_present", g_skipLatePresent);
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

	std::wstring LocalPendingConfigPath() {
		auto folder = Windows::Storage::ApplicationData::Current->LocalFolder->Path;
		return std::wstring(folder->Data()) + L"\\moonlight-lab-pending-pacing.json";
	}

	void SetVariant(const char* label,
	                int presentSyncInterval,
	                bool manualPresentWait,
	                double presentLeadMs,
	                int swapchainBuffers,
	                int textureRingSize,
	                bool waitableSwapChain,
	                bool leadAwareFrameWait = false,
	                bool skipLatePresent = false) {
		g_variantLabel = label;
		g_presentSyncInterval = presentSyncInterval;
		g_manualPresentWait = manualPresentWait;
		g_presentLeadMs = presentLeadMs;
		g_swapchainBuffers = swapchainBuffers;
		g_frameQueueHwm = 1;
		g_textureRingSize = textureRingSize;
		g_waitableSwapChain = waitableSwapChain;
		g_maxFrameLatency = 1;
		g_leadAwareFrameWait = leadAwareFrameWait;
		g_skipLatePresent = skipLatePresent;
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

		switch (((index % 8) + 8) % 8) {
		case 0: SetVariant("A-current", 0, true, 0.0, 5, 1, false); break;
		case 1: SetVariant("B-candidate-lead2-buf3-ring3", 0, true, 2.0, 3, 3, false); break;
		case 2: SetVariant("C-sync1-candidate-buf3-ring3", 1, true, 2.0, 3, 3, false); break;
		case 3: SetVariant("D-sync1-nowait-buf3-ring3", 1, false, 0.0, 3, 3, false); break;
		case 4: SetVariant("H-lead2-targetwait-skiplate-buf3-ring3", 0, true, 2.0, 3, 3, false, true, true); break;
		case 5: SetVariant("F-lead2-buf2-ring3", 0, true, 2.0, 2, 3, false); break;
		case 6: SetVariant("H-lead2-targetwait-skiplate-buf3-ring3", 0, true, 2.0, 3, 3, false, true, true); break;
		case 7: SetVariant("E-lead3-buf3-ring3", 0, true, 3.0, 3, 3, false); break;
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

	void ResetDefaults() {
		g_variantLabel = "default";
		g_presentSyncInterval = ML_LAB_PRESENT_SYNC_INTERVAL_DEFAULT;
		g_manualPresentWait = true;
		g_presentLeadMs = 0.0;
		g_swapchainBuffers = ML_LAB_SWAPCHAIN_BUFFERS_DEFAULT;
		g_frameQueueHwm = ML_LAB_FRAME_QUEUE_HWM_DEFAULT;
		g_decoderThrottleMs = ML_LAB_DECODER_THROTTLE_MS_DEFAULT;
		g_noLockAroundPresent = ML_LAB_NO_LOCK_PRESENT_DEFAULT != 0;
		g_waitableSwapChain = false;
		g_maxFrameLatency = 1;
		g_textureRingSize = 1;
		g_leadAwareFrameWait = false;
		g_skipLatePresent = false;
		g_loadedJsonConfig = false;
	}

	void LoadConfig() {
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
		g_leadAwareFrameWait = ReadIntSetting(L"xbox_lab_lead_aware_frame_wait", g_leadAwareFrameWait ? 1 : 0) != 0;
		g_skipLatePresent = ReadIntSetting(L"xbox_lab_skip_late_present", g_skipLatePresent ? 1 : 0) != 0;

		Utils::Logf("Lab pacing config: variant=%s present_interval=%d manual_present_wait=%d present_lead_ms=%.3f swapchain_buffers=%d frame_queue_hwm=%d decoder_throttle_ms=%d no_lock_present=%d waitable_swapchain=%d max_frame_latency=%d texture_ring_size=%d lead_aware_frame_wait=%d skip_late_present=%d\n",
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
		            g_textureRingSize,
		            g_leadAwareFrameWait ? 1 : 0,
		            g_skipLatePresent ? 1 : 0);

		LabLogger::Event("lab_pacing_config", LabPacingConfig::TelemetryFields());
	}

	bool LoadPendingConfig(ULONGLONG nowMs) {
		try {
			std::ifstream file(Utils::WideToNarrowString(LocalPendingConfigPath()));
			if (!file) {
				return false;
			}

			nlohmann::json config = nlohmann::json::parse(file, nullptr, true, true);
			ULONGLONG selectedAtMs = config.value("selected_at_ms", 0ULL);
			if (selectedAtMs == 0 || nowMs < selectedAtMs || nowMs - selectedAtMs > 5000) {
				return false;
			}

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
			g_leadAwareFrameWait = config.value("lead_aware_frame_wait", g_leadAwareFrameWait);
			g_skipLatePresent = config.value("skip_late_present", g_skipLatePresent);
			g_loadedJsonConfig = config.value("loaded_json_config", g_loadedJsonConfig);
			Utils::Logf("Reused pending lab pacing config from LocalState: variant=%s\n", g_variantLabel.c_str());
			LabLogger::Event("lab_pacing_config_reuse", LabPacingConfig::TelemetryFields());
			return true;
		}
		catch (const std::exception& e) {
			Utils::Logf("Failed to load pending lab pacing config: %s\n", e.what());
		}
		catch (...) {
			Utils::Logf("Failed to load pending lab pacing config\n");
		}
		return false;
	}

	void SavePendingConfig(ULONGLONG selectedAtMs) {
		try {
			nlohmann::json config;
			config["selected_at_ms"] = selectedAtMs;
			config["variant_label"] = g_variantLabel;
			config["present_sync_interval"] = g_presentSyncInterval;
			config["manual_present_wait"] = g_manualPresentWait;
			config["present_lead_ms"] = g_presentLeadMs;
			config["swapchain_buffers"] = g_swapchainBuffers;
			config["frame_queue_hwm"] = g_frameQueueHwm;
			config["decoder_throttle_ms"] = g_decoderThrottleMs;
			config["no_lock_present"] = g_noLockAroundPresent;
			config["waitable_swapchain"] = g_waitableSwapChain;
			config["max_frame_latency"] = g_maxFrameLatency;
			config["texture_ring_size"] = g_textureRingSize;
			config["lead_aware_frame_wait"] = g_leadAwareFrameWait;
			config["skip_late_present"] = g_skipLatePresent;
			config["loaded_json_config"] = g_loadedJsonConfig;

			std::ofstream out(Utils::WideToNarrowString(LocalPendingConfigPath()), std::ios::trunc);
			out << config.dump() << "\n";
		}
		catch (...) {
			Utils::Logf("Failed to save pending lab pacing config\n");
		}
	}
}

void LabPacingConfig::Initialize() {
	if (g_initialized.load(std::memory_order_acquire)) {
		return;
	}

	std::lock_guard<std::mutex> lock(g_configMutex);
	if (!g_initialized.load(std::memory_order_relaxed)) {
		ULONGLONG nowMs = GetTickCount64();
		ResetDefaults();
		if (LoadPendingConfig(nowMs)) {
			g_preparedForPendingStream = true;
			g_lastStreamConfigMs = nowMs;
		}
		else {
			LoadConfig();
			g_preparedForPendingStream = true;
			g_lastStreamConfigMs = nowMs;
			SavePendingConfig(nowMs);
		}
		g_initialized.store(true, std::memory_order_release);
	}
}

void LabPacingConfig::ReloadForStream() {
	std::lock_guard<std::mutex> lock(g_configMutex);
	ULONGLONG nowMs = GetTickCount64();
	bool hasFreshPreparedConfig = g_lastStreamConfigMs != 0 && nowMs >= g_lastStreamConfigMs && nowMs - g_lastStreamConfigMs < 5000;
	if (g_preparedForPendingStream && hasFreshPreparedConfig) {
		Utils::Logf("Reusing pending lab pacing config for stream setup: variant=%s\n", g_variantLabel.c_str());
		return;
	}
	g_preparedForPendingStream = false;
	ResetDefaults();
	if (LoadPendingConfig(nowMs)) {
		g_preparedForPendingStream = true;
		g_lastStreamConfigMs = nowMs;
		g_initialized.store(true, std::memory_order_release);
		return;
	}
	LoadConfig();
	g_preparedForPendingStream = true;
	g_lastStreamConfigMs = nowMs;
	SavePendingConfig(nowMs);
	g_initialized.store(true, std::memory_order_release);
}

void LabPacingConfig::MarkStreamStarted() {
	std::lock_guard<std::mutex> lock(g_configMutex);
	g_preparedForPendingStream = false;
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

bool LabPacingConfig::LeadAwareFrameWait() {
	Initialize();
	return g_leadAwareFrameWait;
}

bool LabPacingConfig::SkipLatePresent() {
	Initialize();
	return g_skipLatePresent;
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
		",\"lead_aware_frame_wait\":" + std::to_string(g_leadAwareFrameWait ? 1 : 0) +
		",\"skip_late_present\":" + std::to_string(g_skipLatePresent ? 1 : 0) +
		",\"config_source\":\"" + (g_loadedJsonConfig ? std::string("json") : std::string("auto-cycle")) + "\"";
}
