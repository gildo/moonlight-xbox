#pragma once

#include <string>

#ifndef ML_LAB_PRESENT_SYNC_INTERVAL_DEFAULT
#define ML_LAB_PRESENT_SYNC_INTERVAL_DEFAULT 0
#endif

#ifndef ML_LAB_SWAPCHAIN_BUFFERS_DEFAULT
#define ML_LAB_SWAPCHAIN_BUFFERS_DEFAULT 5
#endif

#ifndef ML_LAB_FRAME_QUEUE_HWM_DEFAULT
#define ML_LAB_FRAME_QUEUE_HWM_DEFAULT 1
#endif

#ifndef ML_LAB_DECODER_THROTTLE_MS_DEFAULT
#define ML_LAB_DECODER_THROTTLE_MS_DEFAULT 0
#endif

#ifndef ML_LAB_NO_LOCK_PRESENT_DEFAULT
#define ML_LAB_NO_LOCK_PRESENT_DEFAULT 0
#endif

namespace moonlight_xbox_dx {
namespace LabPacingConfig {
	void Initialize();
	void ReloadForStream();
	void MarkStreamStarted();
	const std::string& VariantLabel();
	int PresentSyncInterval();
	bool ManualPresentWait();
	double PresentLeadMs();
	int SwapChainBufferCount();
	int FrameQueueHighWaterMark();
	int DecoderThrottleMs();
	bool NoLockAroundPresent();
	bool WaitableSwapChain();
	int MaxFrameLatency();
	int TextureRingSize();
	bool LeadAwareFrameWait();
	bool SkipLatePresent();
	double LatePresentSkipGraceMs();
	double RenderSafetyMs();
	bool AdaptivePacingBudget();
	bool RetainedFrameFallback();
	double RetainedFrameFallbackMarginMs();
	std::string TelemetryFields();
}
}
