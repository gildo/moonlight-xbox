#pragma once

#include "pch.h"
#include <cstdint>
#include <string>

namespace moonlight_xbox_dx {
namespace LabLogger {
	void Event(const char* name, const std::string& fields = std::string());
	void Telemetry(const std::string& fields);
	void NoteFrame(uint32_t frameNumber);
	void NoteVideoPacket();
	void NoteAudioPacket();
	void NoteControlPacket();
	uint32_t LastFrameNumber();
	uint64_t LastVideoMs();
	uint64_t LastAudioMs();
	uint64_t LastControlMs();
	uint64_t NowMs();
	std::string JsonString(const std::string& input);
}
}
