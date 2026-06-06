#include "pch.h"
#include "LabLogger.h"
#include "Utils.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>

using namespace moonlight_xbox_dx;

namespace {
	std::mutex g_fileMutex;
	std::atomic<uint32_t> g_lastFrameNumber{0};
	std::atomic<uint64_t> g_lastVideoMs{0};
	std::atomic<uint64_t> g_lastAudioMs{0};
	std::atomic<uint64_t> g_lastControlMs{0};
	std::atomic<bool> g_initialized{false};
	std::atomic<uint32_t> g_storageFailureCount{0};

	std::string LocalStatePath(const char* fileName) {
		try {
			auto folder = Windows::Storage::ApplicationData::Current->LocalFolder->Path;
			std::string path = Utils::PlatformStringToStdString(folder);
			path += "\\";
			path += fileName;
			return path;
		}
		catch (...) {
			return std::string(fileName);
		}
	}

	bool AppendLineCrt(const char* fileName, const std::string& line) {
		std::lock_guard<std::mutex> lock(g_fileMutex);
		std::string path = LocalStatePath(fileName);
		FILE* f = nullptr;
		if (fopen_s(&f, path.c_str(), "ab") != 0 || f == nullptr) {
			return false;
		}
		fwrite(line.data(), 1, line.size(), f);
		fwrite("\n", 1, 1, f);
		fclose(f);
		return true;
	}

	void AppendLineStorage(const char* fileName, const std::string& line) {
		try {
			auto folder = Windows::Storage::ApplicationData::Current->LocalFolder;
			auto name = ref new Platform::String(Utils::NarrowToWideString(fileName ? fileName : "moonlight-lab-events.ndjson").c_str());
			auto text = ref new Platform::String(Utils::NarrowToWideString(line + "\n").c_str());

			concurrency::create_task(folder->CreateFileAsync(name, Windows::Storage::CreationCollisionOption::OpenIfExists))
				.then([text](Windows::Storage::StorageFile^ file) {
					return Windows::Storage::FileIO::AppendTextAsync(file, text);
				})
				.then([](concurrency::task<void> task) {
					try {
						task.get();
					}
					catch (Platform::Exception^ ex) {
						if (g_storageFailureCount.fetch_add(1, std::memory_order_relaxed) < 3) {
							Utils::Logf("[LabLogger] Storage append failed: 0x%08X\n", ex->HResult);
						}
					}
				});
		}
		catch (Platform::Exception^ ex) {
			if (g_storageFailureCount.fetch_add(1, std::memory_order_relaxed) < 3) {
				Utils::Logf("[LabLogger] Storage append setup failed: 0x%08X\n", ex->HResult);
			}
		}
		catch (...) {
			if (g_storageFailureCount.fetch_add(1, std::memory_order_relaxed) < 3) {
				Utils::Log("[LabLogger] Storage append setup failed with unknown exception\n");
			}
		}
	}

	void AppendLine(const char* fileName, const std::string& line) {
		if (AppendLineCrt(fileName, line)) {
			return;
		}
		AppendLineStorage(fileName, line);
	}
}

void LabLogger::Initialize() {
	bool expected = false;
	if (!g_initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
		return;
	}

	Event("app_launch",
		"\"local_state_path\":" + JsonString(LocalStatePath("")));
}

uint64_t LabLogger::NowMs() {
	auto now = std::chrono::system_clock::now().time_since_epoch();
	return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string LabLogger::JsonString(const std::string& input) {
	std::string out;
	out.reserve(input.size() + 2);
	out.push_back('"');
	for (char ch : input) {
		switch (ch) {
		case '\\': out += "\\\\"; break;
		case '"': out += "\\\""; break;
		case '\b': out += "\\b"; break;
		case '\f': out += "\\f"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if ((unsigned char)ch < 0x20) {
				char buf[8];
				snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)ch);
				out += buf;
			}
			else {
				out.push_back(ch);
			}
			break;
		}
	}
	out.push_back('"');
	return out;
}

void LabLogger::Event(const char* name, const std::string& fields) {
	std::string line = "{\"ts_ms\":" + std::to_string(NowMs()) +
		",\"event\":" + JsonString(name ? name : "unknown");
	if (!fields.empty()) {
		line += ",";
		line += fields;
	}
	line += "}";
	AppendLine("moonlight-lab-events.ndjson", line);
}

void LabLogger::Telemetry(const std::string& fields) {
	std::string line = "{\"ts_ms\":" + std::to_string(NowMs());
	if (!fields.empty()) {
		line += ",";
		line += fields;
	}
	line += "}";
	AppendLine("moonlight-lab-telemetry.ndjson", line);
}

void LabLogger::NoteFrame(uint32_t frameNumber) {
	g_lastFrameNumber.store(frameNumber, std::memory_order_release);
	NoteVideoPacket();
}

void LabLogger::NoteVideoPacket() {
	g_lastVideoMs.store(NowMs(), std::memory_order_release);
}

void LabLogger::NoteAudioPacket() {
	g_lastAudioMs.store(NowMs(), std::memory_order_release);
}

void LabLogger::NoteControlPacket() {
	g_lastControlMs.store(NowMs(), std::memory_order_release);
}

uint32_t LabLogger::LastFrameNumber() {
	return g_lastFrameNumber.load(std::memory_order_acquire);
}

uint64_t LabLogger::LastVideoMs() {
	return g_lastVideoMs.load(std::memory_order_acquire);
}

uint64_t LabLogger::LastAudioMs() {
	return g_lastAudioMs.load(std::memory_order_acquire);
}

uint64_t LabLogger::LastControlMs() {
	return g_lastControlMs.load(std::memory_order_acquire);
}
