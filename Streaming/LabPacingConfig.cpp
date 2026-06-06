#include "pch.h"
#include "LabPacingConfig.h"
#include "../State/LabLogger.h"
#include "Utils.hpp"

#include <algorithm>
#include <mutex>

using namespace moonlight_xbox_dx;

namespace {
	std::once_flag g_initOnce;
	int g_presentSyncInterval = ML_LAB_PRESENT_SYNC_INTERVAL_DEFAULT;
	int g_swapchainBuffers = ML_LAB_SWAPCHAIN_BUFFERS_DEFAULT;
	int g_frameQueueHwm = ML_LAB_FRAME_QUEUE_HWM_DEFAULT;
	int g_decoderThrottleMs = ML_LAB_DECODER_THROTTLE_MS_DEFAULT;
	bool g_noLockAroundPresent = ML_LAB_NO_LOCK_PRESENT_DEFAULT != 0;

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

	void InitOnce() {
		g_presentSyncInterval = std::clamp(ReadIntSetting(L"xbox_lab_present_interval", g_presentSyncInterval), 0, 1);
		g_swapchainBuffers = std::clamp(ReadIntSetting(L"xbox_lab_swapchain_buffers", g_swapchainBuffers), 2, 5);
		g_frameQueueHwm = std::clamp(ReadIntSetting(L"xbox_lab_frame_queue_hwm", g_frameQueueHwm), 1, g_swapchainBuffers);
		g_decoderThrottleMs = std::clamp(ReadIntSetting(L"xbox_lab_decoder_throttle_ms", g_decoderThrottleMs), 0, 5);
		g_noLockAroundPresent = ReadIntSetting(L"xbox_lab_no_lock_present", g_noLockAroundPresent ? 1 : 0) != 0;

		Utils::Logf("Lab pacing config: present_interval=%d swapchain_buffers=%d frame_queue_hwm=%d decoder_throttle_ms=%d no_lock_present=%d\n",
		            g_presentSyncInterval,
		            g_swapchainBuffers,
		            g_frameQueueHwm,
		            g_decoderThrottleMs,
		            g_noLockAroundPresent ? 1 : 0);

		LabLogger::Event("lab_pacing_config", LabPacingConfig::TelemetryFields());
	}
}

void LabPacingConfig::Initialize() {
	std::call_once(g_initOnce, InitOnce);
}

int LabPacingConfig::PresentSyncInterval() {
	Initialize();
	return g_presentSyncInterval;
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

std::string LabPacingConfig::TelemetryFields() {
	return "\"present_interval\":" + std::to_string(g_presentSyncInterval) +
		",\"swapchain_buffers\":" + std::to_string(g_swapchainBuffers) +
		",\"frame_queue_hwm\":" + std::to_string(g_frameQueueHwm) +
		",\"decoder_throttle_ms\":" + std::to_string(g_decoderThrottleMs) +
		",\"no_lock_present\":" + std::to_string(g_noLockAroundPresent ? 1 : 0);
}
