#include "pch.h"
#include "Stats.h"
#include "BuildInfo.h"
#include "LabLogger.h"
#include "Utils.hpp"
#include "../Plot/ImGuiPlots.h"
#include "../Streaming/FFMpegDecoder.h"
#include "../Streaming/FrameQueue.h"
#include "../Streaming/LabPacingConfig.h"
#include "../Streaming/Pacer.h"

#include <cmath>

using namespace moonlight_xbox_dx;

namespace {
	double Percentile(std::vector<double> values, double p) {
		if (values.empty()) {
			return 0.0;
		}
		std::sort(values.begin(), values.end());
		size_t idx = static_cast<size_t>(std::ceil((p / 100.0) * values.size())) - 1;
		if (idx >= values.size()) {
			idx = values.size() - 1;
		}
		return values[idx];
	}
}

Stats::Stats() :
	m_avgQueueSize(0.0),
	m_avgMbpsSmoothed(0.0),
	m_activeMissedPresentStreak(0)
{
	ZeroMemory(&m_ActiveWndVideoStats, sizeof(VIDEO_STATS));
	ZeroMemory(&m_LastWndVideoStats, sizeof(VIDEO_STATS));
	ZeroMemory(&m_GlobalVideoStats, sizeof(VIDEO_STATS));
}

// Called every frame, if true is returned, the stats text is refreshed
bool Stats::ShouldUpdateDisplay(DX::StepTimer const& timer, bool isVisible, char* output, size_t length)
{
	bool shouldUpdate = false;
	bool shouldWriteTelemetry = false;
	VIDEO_STATS displayStats = {};
	VIDEO_STATS telemetryStats = {};
	float avgQueueSizeSnapshot = 0.0f;
	std::vector<double> presentDxgiCallWindow;
	std::vector<double> presentTotalWindow;
	std::vector<double> presentSubmitEarlyWindow;
	std::vector<double> presentSubmitLateWindow;
	std::vector<double> presentTargetSubmitEarlyWindow;
	std::vector<double> presentTargetSubmitLateWindow;

	if (isVisible && ImGuiPlots::instance().isEnabled()) {
		const double alpha = 0.1f;
		m_avgMbpsSmoothed = (1 - alpha) * m_avgMbpsSmoothed + alpha * m_bwTracker.GetAverageMbps();
		ImGuiPlots::instance().observeFloat(PLOT_BANDWIDTH, (float)m_avgMbpsSmoothed);
	}

	// Process stats once per second
	if (timer.GetTotalSeconds() - m_ActiveWndVideoStats.measurementStartTimestamp >= 1.0) {
		std::lock_guard<std::mutex> lock(m_mutex);

		if (isVisible) {
			// Display using data from the last 2 window periods
			addVideoStats(timer, m_LastWndVideoStats, displayStats);
			addVideoStats(timer, m_ActiveWndVideoStats, displayStats);
			shouldUpdate = true;
		}

		// Accumulate these values into the global stats
		addVideoStats(timer, m_ActiveWndVideoStats, m_GlobalVideoStats);

		addVideoStats(timer, m_ActiveWndVideoStats, telemetryStats);
		avgQueueSizeSnapshot = m_avgQueueSize;
		presentDxgiCallWindow = m_presentDxgiCallWindow;
		presentTotalWindow = m_presentTotalWindow;
		presentSubmitEarlyWindow = m_presentSubmitEarlyWindow;
		presentSubmitLateWindow = m_presentSubmitLateWindow;
		presentTargetSubmitEarlyWindow = m_presentTargetSubmitEarlyWindow;
		presentTargetSubmitLateWindow = m_presentTargetSubmitLateWindow;
		shouldWriteTelemetry = true;

		// Move this window into the last window slot and clear it for next window
		memcpy(&m_LastWndVideoStats, &m_ActiveWndVideoStats, sizeof(VIDEO_STATS));
		ZeroMemory(&m_ActiveWndVideoStats, sizeof(VIDEO_STATS));
		m_ActiveWndVideoStats.measurementStartTimestamp = timer.GetTotalSeconds();
		m_presentDxgiCallWindow.clear();
		m_presentTotalWindow.clear();
		m_presentSubmitEarlyWindow.clear();
		m_presentSubmitLateWindow.clear();
		m_presentTargetSubmitEarlyWindow.clear();
		m_presentTargetSubmitLateWindow.clear();
	}

	if (shouldWriteTelemetry) {
		double avgVideoMbps = m_bwTracker.GetAverageMbps();
		double peakVideoMbps = m_bwTracker.GetPeakMbps();
		double presentDxgiP95 = Percentile(presentDxgiCallWindow, 95.0);
		double presentTotalP95 = Percentile(presentTotalWindow, 95.0);
		double presentSubmitEarlyP95 = Percentile(presentSubmitEarlyWindow, 95.0);
		double presentSubmitLateP95 = Percentile(presentSubmitLateWindow, 95.0);
		double presentTargetSubmitEarlyP95 = Percentile(presentTargetSubmitEarlyWindow, 95.0);
		double presentTargetSubmitLateP95 = Percentile(presentTargetSubmitLateWindow, 95.0);

		if (shouldUpdate) {
			formatVideoStats(timer, displayStats, output, length, avgQueueSizeSnapshot, avgVideoMbps, peakVideoMbps);
		}

		LabLogger::Telemetry(
			"\"received_frames\":" + std::to_string(telemetryStats.receivedFrames) +
			",\"decoded_frames\":" + std::to_string(telemetryStats.decodedFrames) +
			",\"rendered_frames\":" + std::to_string(telemetryStats.renderedFrames) +
			",\"network_dropped_frames\":" + std::to_string(telemetryStats.networkDroppedFrames) +
			",\"pacer_dropped_frames\":" + std::to_string(telemetryStats.pacerDroppedFrames) +
			",\"hit_deadlines\":" + std::to_string(telemetryStats.hitDeadlines) +
			",\"missed_deadlines\":" + std::to_string(telemetryStats.missedDeadlines) +
			",\"avg_mbps\":" + std::to_string(avgVideoMbps) +
			",\"peak_mbps\":" + std::to_string(peakVideoMbps) +
			",\"queue_depth\":" + std::to_string(FrameQueue::instance().count()) +
			",\"frame_queue_hwm\":" + std::to_string(FrameQueue::instance().highWaterMark()) +
			",\"frame_queue_capacity\":" + std::to_string(FrameQueue::instance().maxCapacity()) +
			",\"avg_queue_depth\":" + std::to_string(avgQueueSizeSnapshot) +
			",\"avg_decode_ms\":" + std::to_string(telemetryStats.decodedFrames ? telemetryStats.totalDecodeTime / telemetryStats.decodedFrames : 0.0) +
			",\"avg_wait_for_frame_ms\":" + std::to_string(telemetryStats.renderedFrames ? (double)telemetryStats.totalPreWaitTimeUs / 1000.0 / telemetryStats.renderedFrames : 0.0) +
			",\"avg_render_ms\":" + std::to_string(telemetryStats.renderedFrames ? (double)telemetryStats.totalRenderTimeUs / 1000.0 / telemetryStats.renderedFrames : 0.0) +
			",\"avg_wait_before_present_ms\":" + std::to_string(telemetryStats.renderedFrames ? (double)telemetryStats.totalPresentTimeUs / 1000.0 / telemetryStats.renderedFrames : 0.0) +
			",\"avg_present_call_ms\":" + std::to_string(telemetryStats.renderedFrames ? (double)telemetryStats.totalPresentCallTimeUs / 1000.0 / telemetryStats.renderedFrames : 0.0) +
			",\"avg_present_lock_wait_ms\":" + std::to_string(telemetryStats.renderedFrames ? (double)telemetryStats.totalPresentLockWaitUs / 1000.0 / telemetryStats.renderedFrames : 0.0) +
			",\"avg_present_dxgi_call_ms\":" + std::to_string(telemetryStats.renderedFrames ? (double)telemetryStats.totalPresentDxgiCallUs / 1000.0 / telemetryStats.renderedFrames : 0.0) +
			",\"p95_present_dxgi_call_ms\":" + std::to_string(presentDxgiP95) +
			",\"max_present_dxgi_call_ms\":" + std::to_string(telemetryStats.maxPresentDxgiCallMs) +
			",\"avg_present_total_ms\":" + std::to_string(telemetryStats.renderedFrames ? (double)telemetryStats.totalPresentCallTimeUs / 1000.0 / telemetryStats.renderedFrames : 0.0) +
			",\"p95_present_total_ms\":" + std::to_string(presentTotalP95) +
			",\"max_present_total_ms\":" + std::to_string(telemetryStats.maxPresentTotalMs) +
			",\"avg_present_submit_early_ms\":" + std::to_string(telemetryStats.renderedFrames ? (double)telemetryStats.totalPresentSubmitEarlyUs / 1000.0 / telemetryStats.renderedFrames : 0.0) +
			",\"p95_present_submit_early_ms\":" + std::to_string(presentSubmitEarlyP95) +
			",\"avg_present_submit_late_ms\":" + std::to_string(telemetryStats.renderedFrames ? (double)telemetryStats.totalPresentSubmitLateUs / 1000.0 / telemetryStats.renderedFrames : 0.0) +
			",\"p95_present_submit_late_ms\":" + std::to_string(presentSubmitLateP95) +
			",\"max_present_submit_late_ms\":" + std::to_string(telemetryStats.maxPresentSubmitLateMs) +
			",\"avg_present_target_submit_early_ms\":" + std::to_string(telemetryStats.renderedFrames ? (double)telemetryStats.totalPresentTargetSubmitEarlyUs / 1000.0 / telemetryStats.renderedFrames : 0.0) +
			",\"p95_present_target_submit_early_ms\":" + std::to_string(presentTargetSubmitEarlyP95) +
			",\"avg_present_target_submit_late_ms\":" + std::to_string(telemetryStats.renderedFrames ? (double)telemetryStats.totalPresentTargetSubmitLateUs / 1000.0 / telemetryStats.renderedFrames : 0.0) +
			",\"p95_present_target_submit_late_ms\":" + std::to_string(presentTargetSubmitLateP95) +
			",\"max_present_target_submit_late_ms\":" + std::to_string(telemetryStats.maxPresentTargetSubmitLateMs) +
			",\"avg_present_return_to_next_vblank_ms\":" + std::to_string(telemetryStats.renderedFrames ? (double)telemetryStats.totalPresentReturnToNextVblankUs / 1000.0 / telemetryStats.renderedFrames : 0.0) +
			",\"present_blocked_full_interval_count\":" + std::to_string(telemetryStats.presentBlockedFullIntervalCount) +
			",\"present_submit_late_count\":" + std::to_string(telemetryStats.presentSubmitLateCount) +
			",\"present_target_submit_late_count\":" + std::to_string(telemetryStats.presentTargetSubmitLateCount) +
			",\"late_present_skip_count\":" + std::to_string(telemetryStats.latePresentSkipCount) +
			",\"retained_frame_present_count\":" + std::to_string(telemetryStats.retainedFramePresentCount) +
			",\"same_vblank_gate_count\":" + std::to_string(telemetryStats.sameVblankGateCount) +
			",\"avg_same_vblank_gate_ms\":" + std::to_string(telemetryStats.sameVblankGateCount ? (double)telemetryStats.totalSameVblankGateUs / 1000.0 / telemetryStats.sameVblankGateCount : 0.0) +
			",\"max_same_vblank_gate_ms\":" + std::to_string(telemetryStats.maxSameVblankGateMs) +
			",\"missed_present_streak_max\":" + std::to_string(telemetryStats.missedPresentStreakMax) +
			"," + LabPacingConfig::TelemetryFields() +
			",\"rtt_ms\":" + std::to_string(telemetryStats.lastRtt) +
			",\"rtt_variance_ms\":" + std::to_string(telemetryStats.lastRttVariance));
	}

	return shouldUpdate;
}

/// Hooks for stat producers, where possible these are combined into one call

// 1. The size in bytes of one video frame, we use this to also increment frame counters.
// 2. Time in milliseconds from first packet of a frame until fully reassembled frame is ready for decoding
//    Includes time spent in FEC reassembly
// 3. Host processing latency (encode time)
// 4. network packet loss (caller reports frame sequence number holes)
void Stats::SubmitVideoBytesAndReassemblyTime(uint32_t length, PDECODE_UNIT decodeUnit, uint32_t droppedFrames)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_ActiveWndVideoStats.receivedFrames++;
	m_ActiveWndVideoStats.totalFrames++;

	// bandwidth
	m_bwTracker.AddBytes(length);

	// reassembly time
	uint32_t reassemblyUs = (uint32_t)(decodeUnit->enqueueTimeUs - decodeUnit->receiveTimeUs);
	m_ActiveWndVideoStats.totalReassemblyTimeUs += reassemblyUs;

	// Host processing latency
	uint16_t frameHPL = decodeUnit->frameHostProcessingLatency;
	if (frameHPL != 0) {
		if (m_ActiveWndVideoStats.minHostProcessingLatency != 0) {
			m_ActiveWndVideoStats.minHostProcessingLatency = std::min(m_ActiveWndVideoStats.minHostProcessingLatency, frameHPL);
		} else {
			m_ActiveWndVideoStats.minHostProcessingLatency = frameHPL;
		}
		m_ActiveWndVideoStats.framesWithHostProcessingLatency += 1;
		m_ActiveWndVideoStats.maxHostProcessingLatency = std::max(m_ActiveWndVideoStats.maxHostProcessingLatency, frameHPL);
		m_ActiveWndVideoStats.totalHostProcessingLatency += frameHPL;
	}

	// Network packet loss
	if (droppedFrames > 0) {
		m_ActiveWndVideoStats.networkDroppedFrames += droppedFrames;
		m_ActiveWndVideoStats.totalFrames += droppedFrames;
	}
	ImGuiPlots::instance().observeFloat(PLOT_DROPPED_NETWORK, (float)droppedFrames);

	// Host frametime graph, uses raw 90kHz units to avoid rounding errors
	static uint32_t lastHostPts = 0;
	if (lastHostPts != 0) {
		const uint32_t delta90k = (uint32_t)(decodeUnit->rtpTimestamp - lastHostPts); // wrap-safe
		ImGuiPlots::instance().observeFloat(PLOT_HOST_FRAMETIME, (float)(delta90k / 90.0f));
	}
	lastHostPts = (uint32_t)decodeUnit->rtpTimestamp;
}

// Time in milliseconds we spent decoding one frame, it is added up to later be divided by decodedFrames
void Stats::SubmitDecodeMs(double decodeMs) {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_ActiveWndVideoStats.totalDecodeTime += decodeMs;
	m_ActiveWndVideoStats.decodedFrames++;
}

void Stats::SubmitDroppedFrame(int count) {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_ActiveWndVideoStats.pacerDroppedFrames += count;
}

void Stats::SubmitAvgQueueSize(float avgQueueSize) {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_avgQueueSize = avgQueueSize;
}

// Time in microseconds we spent in the frame pacer, and time for rendering the frame.
// Also increments the rendered frame count.
void Stats::SubmitPacerTime(int64_t pacerTimeQpc) {
	std::lock_guard<std::mutex> lock(m_mutex);
	int64_t pacerTimeUs = QpcToUs(pacerTimeQpc);
	m_ActiveWndVideoStats.totalPacerTimeUs += pacerTimeUs;
}

// Present to display latency (how close to hitting vblank we are)
void Stats::SubmitPresentPacing(double presentDisplayMs) {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_ActiveWndVideoStats.totalPresentDisplayMs += presentDisplayMs;
}

// High-level render loop timings
void Stats::SubmitRenderStats(double preWaitTimeMs,
                              double renderTimeMs,
                              double waitBeforePresentMs,
                              double presentLockWaitMs,
                              double presentDxgiCallMs,
                              double presentTotalMs,
                              double presentSubmitEarlyMs,
                              double presentSubmitLateMs,
                              double presentTargetSubmitEarlyMs,
                              double presentTargetSubmitLateMs,
                              double presentReturnToNextVblankMs,
                              double sameVblankGateMs,
                              bool sameVblankGated,
                              bool hitDeadline,
                              bool skippedLatePresent,
                              bool retainedFramePresented) {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_ActiveWndVideoStats.totalRenderTimeUs += static_cast<uint64_t>(renderTimeMs * 1000);
	m_ActiveWndVideoStats.renderedFrames++;

	if (hitDeadline) {
		m_ActiveWndVideoStats.hitDeadlines++;
		m_activeMissedPresentStreak = 0;
	} else {
		m_ActiveWndVideoStats.missedDeadlines++;
		m_activeMissedPresentStreak++;
		m_ActiveWndVideoStats.missedPresentStreakMax = std::max(m_ActiveWndVideoStats.missedPresentStreakMax, m_activeMissedPresentStreak);
	}

	// Only shown in debug builds
	m_ActiveWndVideoStats.totalPreWaitTimeUs += static_cast<uint64_t>(preWaitTimeMs * 1000);
	m_ActiveWndVideoStats.totalPresentTimeUs += static_cast<uint64_t>(waitBeforePresentMs * 1000);
	m_ActiveWndVideoStats.totalPresentCallTimeUs += static_cast<uint64_t>(presentTotalMs * 1000);
	m_ActiveWndVideoStats.totalPresentLockWaitUs += static_cast<uint64_t>(presentLockWaitMs * 1000);
	m_ActiveWndVideoStats.totalPresentDxgiCallUs += static_cast<uint64_t>(presentDxgiCallMs * 1000);
	m_ActiveWndVideoStats.totalPresentSubmitEarlyUs += static_cast<uint64_t>(std::max(0.0, presentSubmitEarlyMs) * 1000);
	m_ActiveWndVideoStats.totalPresentSubmitLateUs += static_cast<uint64_t>(std::max(0.0, presentSubmitLateMs) * 1000);
	m_ActiveWndVideoStats.totalPresentTargetSubmitEarlyUs += static_cast<uint64_t>(std::max(0.0, presentTargetSubmitEarlyMs) * 1000);
	m_ActiveWndVideoStats.totalPresentTargetSubmitLateUs += static_cast<uint64_t>(std::max(0.0, presentTargetSubmitLateMs) * 1000);
	m_ActiveWndVideoStats.totalPresentReturnToNextVblankUs += static_cast<uint64_t>(std::max(0.0, presentReturnToNextVblankMs) * 1000);
	m_ActiveWndVideoStats.maxPresentDxgiCallMs = std::max(m_ActiveWndVideoStats.maxPresentDxgiCallMs, presentDxgiCallMs);
	m_ActiveWndVideoStats.maxPresentTotalMs = std::max(m_ActiveWndVideoStats.maxPresentTotalMs, presentTotalMs);
	m_ActiveWndVideoStats.maxPresentSubmitLateMs = std::max(m_ActiveWndVideoStats.maxPresentSubmitLateMs, presentSubmitLateMs);
	m_ActiveWndVideoStats.maxPresentTargetSubmitLateMs = std::max(m_ActiveWndVideoStats.maxPresentTargetSubmitLateMs, presentTargetSubmitLateMs);
	if (presentDxgiCallMs > 10.0) {
		m_ActiveWndVideoStats.presentBlockedFullIntervalCount++;
	}
	if (presentSubmitLateMs > 0.0) {
		m_ActiveWndVideoStats.presentSubmitLateCount++;
	}
	if (presentTargetSubmitLateMs > 0.0) {
		m_ActiveWndVideoStats.presentTargetSubmitLateCount++;
	}
	if (skippedLatePresent) {
		m_ActiveWndVideoStats.latePresentSkipCount++;
	}
	if (retainedFramePresented) {
		m_ActiveWndVideoStats.retainedFramePresentCount++;
	}
	if (sameVblankGated) {
		m_ActiveWndVideoStats.sameVblankGateCount++;
		m_ActiveWndVideoStats.totalSameVblankGateUs += static_cast<uint64_t>(std::max(0.0, sameVblankGateMs) * 1000);
		m_ActiveWndVideoStats.maxSameVblankGateMs = std::max(m_ActiveWndVideoStats.maxSameVblankGateMs, sameVblankGateMs);
	}
	m_presentDxgiCallWindow.push_back(presentDxgiCallMs);
	m_presentTotalWindow.push_back(presentTotalMs);
	m_presentSubmitEarlyWindow.push_back(std::max(0.0, presentSubmitEarlyMs));
	m_presentSubmitLateWindow.push_back(std::max(0.0, presentSubmitLateMs));
	m_presentTargetSubmitEarlyWindow.push_back(std::max(0.0, presentTargetSubmitEarlyMs));
	m_presentTargetSubmitLateWindow.push_back(std::max(0.0, presentTargetSubmitLateMs));
}

/// private methods

void Stats::addVideoStats(DX::StepTimer const& timer, VIDEO_STATS& src, VIDEO_STATS& dst) {
	dst.receivedFrames += src.receivedFrames;
	dst.decodedFrames += src.decodedFrames;
	dst.renderedFrames += src.renderedFrames;
	dst.totalFrames += src.totalFrames;
	dst.networkDroppedFrames += src.networkDroppedFrames;
	dst.pacerDroppedFrames += src.pacerDroppedFrames;
	dst.hitDeadlines += src.hitDeadlines;
	dst.missedDeadlines += src.missedDeadlines;
	dst.totalReassemblyTimeUs += src.totalReassemblyTimeUs;
	dst.totalDecodeTime += src.totalDecodeTime;
	dst.totalPacerTimeUs += src.totalPacerTimeUs;
	dst.totalRenderTimeUs += src.totalRenderTimeUs;
	dst.totalPreWaitTimeUs += src.totalPreWaitTimeUs;
	dst.totalPresentTimeUs += src.totalPresentTimeUs;
	dst.totalPresentCallTimeUs += src.totalPresentCallTimeUs;
	dst.totalPresentLockWaitUs += src.totalPresentLockWaitUs;
	dst.totalPresentDxgiCallUs += src.totalPresentDxgiCallUs;
	dst.totalPresentSubmitEarlyUs += src.totalPresentSubmitEarlyUs;
	dst.totalPresentSubmitLateUs += src.totalPresentSubmitLateUs;
	dst.totalPresentTargetSubmitEarlyUs += src.totalPresentTargetSubmitEarlyUs;
	dst.totalPresentTargetSubmitLateUs += src.totalPresentTargetSubmitLateUs;
	dst.totalPresentReturnToNextVblankUs += src.totalPresentReturnToNextVblankUs;
	dst.totalPresentDisplayMs += src.totalPresentDisplayMs;
	dst.maxPresentDxgiCallMs = std::max(dst.maxPresentDxgiCallMs, src.maxPresentDxgiCallMs);
	dst.maxPresentTotalMs = std::max(dst.maxPresentTotalMs, src.maxPresentTotalMs);
	dst.maxPresentSubmitLateMs = std::max(dst.maxPresentSubmitLateMs, src.maxPresentSubmitLateMs);
	dst.maxPresentTargetSubmitLateMs = std::max(dst.maxPresentTargetSubmitLateMs, src.maxPresentTargetSubmitLateMs);
	dst.presentBlockedFullIntervalCount += src.presentBlockedFullIntervalCount;
	dst.presentSubmitLateCount += src.presentSubmitLateCount;
	dst.presentTargetSubmitLateCount += src.presentTargetSubmitLateCount;
	dst.latePresentSkipCount += src.latePresentSkipCount;
	dst.retainedFramePresentCount += src.retainedFramePresentCount;
	dst.sameVblankGateCount += src.sameVblankGateCount;
	dst.totalSameVblankGateUs += src.totalSameVblankGateUs;
	dst.maxSameVblankGateMs = std::max(dst.maxSameVblankGateMs, src.maxSameVblankGateMs);
	dst.missedPresentStreakMax = std::max(dst.missedPresentStreakMax, src.missedPresentStreakMax);

	if (dst.minHostProcessingLatency == 0) {
		dst.minHostProcessingLatency = src.minHostProcessingLatency;
	}
	else if (src.minHostProcessingLatency != 0) {
		dst.minHostProcessingLatency = std::min(dst.minHostProcessingLatency, src.minHostProcessingLatency);
	}
	dst.maxHostProcessingLatency = std::max(dst.maxHostProcessingLatency, src.maxHostProcessingLatency);
	dst.totalHostProcessingLatency += src.totalHostProcessingLatency;
	dst.framesWithHostProcessingLatency += src.framesWithHostProcessingLatency;

	if (!LiGetEstimatedRttInfo(&dst.lastRtt, &dst.lastRttVariance)) {
		dst.lastRtt = 0;
		dst.lastRttVariance = 0;
	}
	else {
		// Our logic to determine if RTT is valid depends on us never
		// getting an RTT of 0. ENet currently ensures RTTs are >= 1.
		assert(dst.lastRtt > 0);
	}

	double now = timer.GetTotalSeconds();

	// Initialize the measurement start point if this is the first video stat window
	if (!dst.measurementStartTimestamp) {
		dst.measurementStartTimestamp = src.measurementStartTimestamp;
	}

	// The following code assumes the global measure was already started first
	assert(dst.measurementStartTimestamp <= src.measurementStartTimestamp);

	dst.totalFps = (double)dst.totalFrames / (now - dst.measurementStartTimestamp);
	dst.receivedFps = (double)dst.receivedFrames / (now - dst.measurementStartTimestamp);
	dst.decodedFps = (double)dst.decodedFrames / (now - dst.measurementStartTimestamp);
	dst.renderedFps = (double)dst.renderedFrames / (now - dst.measurementStartTimestamp);
}

void Stats::formatVideoStats(DX::StepTimer const& timer,
                             VIDEO_STATS& stats,
                             char* output,
                             size_t length,
                             float avgQueueSize,
                             double avgVideoMbps,
                             double peakVideoMbps) {
	FFMpegDecoder& ffmpeg = FFMpegDecoder::instance();
	Pacer& pacer = Pacer::instance();

	int offset = 0;
	const char* codecString;
	int ret = -1;

	// Start with an empty string
	output[offset] = 0;

	switch (ffmpeg.videoFormat)
	{
	case VIDEO_FORMAT_H264:
		codecString = "H.264";
		break;

	case VIDEO_FORMAT_H264_HIGH8_444:
		codecString = "H.264 4:4:4";
		break;

	case VIDEO_FORMAT_H265:
		codecString = "HEVC";
		break;

	case VIDEO_FORMAT_H265_REXT8_444:
		codecString = "HEVC 4:4:4";
		break;

	case VIDEO_FORMAT_H265_MAIN10:
		if (LiGetCurrentHostDisplayHdrMode()) {
			codecString = "HEVC 10-bit HDR";
		}
		else {
			codecString = "HEVC 10-bit SDR";
		}
		break;

	case VIDEO_FORMAT_H265_REXT10_444:
		if (LiGetCurrentHostDisplayHdrMode()) {
			codecString = "HEVC 10-bit HDR 4:4:4";
		}
		else {
			codecString = "HEVC 10-bit SDR 4:4:4";
		}
		break;

	case VIDEO_FORMAT_AV1_MAIN8:
		codecString = "AV1";
		break;

	case VIDEO_FORMAT_AV1_HIGH8_444:
		codecString = "AV1 4:4:4";
		break;

	case VIDEO_FORMAT_AV1_MAIN10:
		if (LiGetCurrentHostDisplayHdrMode()) {
			codecString = "AV1 10-bit HDR";
		}
		else {
			codecString = "AV1 10-bit SDR";
		}
		break;

	case VIDEO_FORMAT_AV1_HIGH10_444:
		if (LiGetCurrentHostDisplayHdrMode()) {
			codecString = "AV1 10-bit HDR 4:4:4";
		}
		else {
			codecString = "AV1 10-bit SDR 4:4:4";
		}
		break;

	default:
		codecString = "UNKNOWN";
		break;
	}

	if (stats.receivedFps > 0) {
		ret = snprintf(&output[offset],
						length - offset,
						"Moonlight Xbox %s %s [%s]\n"
						"Video stream: %dx%d %.2f FPS (%s)\n",
						MOONLIGHT_LAB_BUILD_NAME,
						MOONLIGHT_LAB_GIT_SHA,
						MOONLIGHT_LAB_GIT_BRANCH,
						ffmpeg.width,
						ffmpeg.height,
						stats.totalFps,
						codecString);
		if (ret < 0 || (size_t)ret >= (length - offset)) {
			Utils::Log("Error: stringifyVideoStats length overflow\n");
			return;
		}

		offset += ret;

		ret = snprintf(&output[offset],
					   length - offset,
					   "Bitrate: %.1f Mbps, Peak (%us): %.1f\n"
					   "Pacing: %s, display %.3f Hz, stream %.3f FPS\n"
					   "Incoming frame rate from network: %.2f FPS\n"
					   "Decoding frame rate: %.2f FPS\n"
					   "Rendering frame rate: %.2f FPS\n",
					   avgVideoMbps,
					   m_bwTracker.GetWindowSeconds(),
					   peakVideoMbps,
					   pacer.getPacingImmediate() ? "immediate" : "display-locked",
					   pacer.getObservedDisplayHz(),
					   pacer.getObservedStreamFps(),
					   stats.receivedFps,
					   stats.decodedFps,
					   stats.renderedFps);
		if (ret < 0 || (size_t)ret >= (length - offset)) {
			Utils::Log("Error: stringifyVideoStats length overflow\n");
			return;
		}

		offset += ret;
	}

	if (stats.framesWithHostProcessingLatency > 0) {
		ret = snprintf(&output[offset],
					   length - offset,
					   "Host processing latency min/max/average: %.1f/%.1f/%.1f ms\n",
					   (double)stats.minHostProcessingLatency / 10,
					   (double)stats.maxHostProcessingLatency / 10,
					   (double)stats.totalHostProcessingLatency / 10 / stats.framesWithHostProcessingLatency);
		if (ret < 0 || (size_t)ret >= (length - offset)) {
			Utils::Log("Error: stringifyVideoStats length overflow\n");
			return;
		}

		offset += ret;
	}
	else {
		// If all frames are duplicates this can happen, but let's avoid having the whole stats area change height
		ret = snprintf(&output[offset],
					   length - offset,
					   "Host processing latency min/max/avg: -/-/- ms\n");
		if (ret < 0 || (size_t)ret >= (length - offset)) {
			Utils::Log("Error: stringifyVideoStats length overflow\n");
			return;
		}

		offset += ret;
	}

	if (stats.renderedFrames != 0) {
		char rttString[32];

		if (stats.lastRtt != 0) {
			snprintf(rttString, sizeof(rttString), "%u ms (variance: %u ms)", stats.lastRtt, stats.lastRttVariance);
		}
		else {
			snprintf(rttString, sizeof(rttString), "N/A");
		}

		ret = snprintf(&output[offset],
					   length - offset,
					   "Frames dropped by your network connection: %.2f%%\n"
					   "Frames dropped due to network jitter: %.2f%%\n"
					   "Missed present deadlines: %u/%u (%.2f%%)\n"
					   "Average network latency: %s\n"
					   "Average reassembly/decoding time: %.2f/%.2f ms\n"
					   "Average frames in queue: %.1f\n"
					   "Average frame queue/render/present: %.2f/%.2f/%.2f ms\n",
					   stats.totalFrames ? (double)stats.networkDroppedFrames / stats.totalFrames * 100 : 0.0f,
					   stats.totalFrames ? (double)stats.pacerDroppedFrames / stats.totalFrames * 100 : 0.0f,
					   stats.missedDeadlines,
					   stats.missedDeadlines + stats.hitDeadlines,
					   (stats.missedDeadlines + stats.hitDeadlines) ? ((double)stats.missedDeadlines / (stats.missedDeadlines + stats.hitDeadlines)) * 100 : 0.0f,
					   rttString,
					   stats.decodedFrames ? (double)stats.totalReassemblyTimeUs / 1000.0 / stats.decodedFrames : 0.0f,
					   stats.decodedFrames ? (double)stats.totalDecodeTime / stats.decodedFrames : 0.0f,
					   avgQueueSize,
					   stats.renderedFrames ? (double)stats.totalPacerTimeUs / 1000.0 / stats.renderedFrames : 0.0f,
					   stats.renderedFrames ? (double)stats.totalRenderTimeUs / 1000.0 / stats.renderedFrames : 0.0f,
					   stats.renderedFrames ? (double)stats.totalPresentTimeUs / 1000.0 / stats.renderedFrames : 0.0f);
		if (ret < 0 || (size_t)ret >= (length - offset)) {
			Utils::Log("Error: stringifyVideoStats length overflow\n");
			return;
		}

		offset += ret;
	}

#if defined(_DEBUG)
	// Developer-only stats that might be too confusing
	// If you add lines here, add more height pixels in StatsRenderer::CreateWindowSizeDependentResources()
	if (stats.renderedFrames != 0) {
		ret = snprintf(&output[offset],
					   length - offset,
					   "------\n"
					   "Missed present rate: %.2f%%\n"
					   "PreWait/Render: %.2f/%.2f ms\n",
					   stats.hitDeadlines ? ((double)stats.missedDeadlines / (stats.missedDeadlines + stats.hitDeadlines)) * 100 : 0.0f,
					   (double)stats.totalPreWaitTimeUs / 1000.0 / stats.renderedFrames,
					   (double)stats.totalRenderTimeUs / 1000.0 / stats.renderedFrames);
		if (ret < 0 || (size_t)ret >= (length - offset)) {
			Utils::Log("Error: stringifyVideoStats length overflow\n");
			return;
		}

		offset += ret;
	}
#endif
}
