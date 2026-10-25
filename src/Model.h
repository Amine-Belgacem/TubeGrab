#pragma once
#include "Common.h"
namespace cf {
struct Video {
    std::string id;
    std::wstring title, playlist, playlistId;
    double duration = -1;
    bool available = true;
    std::wstring url() const { return L"https://www.youtube.com/watch?v=" + Wide(id); }
};
enum class JobState { Pending, Downloading, Processing, Paused, Completed, Failed, Cancelled };
std::wstring StateName(JobState state);
struct Job {
    Video video;
    std::wstring token, destination, outputTemplate;
    std::string format = "mp4";
    int quality = 1080;
    JobState state = JobState::Pending;
    double percent = 0, speed = 0, eta = -1;
    std::wstring detail, file;
    std::string key() const { return video.id + ":" + format; }
};
struct History {
    std::string id, format;
    std::wstring title, file, date;
    int quality = 0;
    std::string key() const { return id + ":" + format; }
};
struct Settings {
    std::wstring destination, tools;
    int quality = 1080;
    std::string format = "mp4";
    bool playlistFolder = true;
    bool skipDownloaded = true;
    int playlistPercent = 48;
};
struct State {
    Settings settings;
    std::vector<Job> queue;
    std::vector<History> history;
};
Json Encode(const State& state);
State Decode(const Json& json);
State LoadState(const fs::path& path, std::wstring& notice);
void SaveState(const fs::path& path, const State& state);
Video ParseVideo(const Json& json);
std::wstring OutputTemplate(const Video& video);
}
