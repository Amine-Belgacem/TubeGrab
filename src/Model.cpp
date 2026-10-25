#include "Model.h"
namespace cf {
static std::string Str(const Json& j, const char* key, const std::string& fallback = {}) {
    auto it = j.find(key); return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
}
static double Number(const Json& j, const char* key, double fallback) {
    auto it = j.find(key); return it != j.end() && it->is_number() ? it->get<double>() : fallback;
}
Video ParseVideo(const Json& j) {
    Video v; v.id = Str(j, "id");
    if (!IsVideoId(v.id)) throw std::runtime_error("The engine returned an invalid YouTube video ID.");
    v.title = Wide(Str(j, "title", v.id)); v.duration = Number(j, "duration", -1);
    v.playlist = Wide(Str(j, "playlist_title")); v.playlistId = Wide(Str(j, "playlist_id"));
    auto availability = Str(j, "availability");
    v.available = availability != "private" && availability != "premium_only" && availability != "subscriber_only" &&
        v.title != L"[Deleted video]" && v.title != L"[Private video]" && Str(j, "live_status") != "is_live" && Str(j, "live_status") != "is_upcoming";
    return v;
}
std::wstring StateName(JobState state) {
    switch (state) {
    case JobState::Pending: return L"Queued";
    case JobState::Downloading: return L"Downloading";
    case JobState::Processing: return L"Processing";
    case JobState::Paused: return L"Paused · partial files saved";
    case JobState::Completed: return L"Completed";
    case JobState::Failed: return L"Failed · select for details";
    case JobState::Cancelled: return L"Cancelled";
    } return L"Unknown";
}
std::wstring OutputTemplate(const Video& video) {
    auto name = SafeName(video.title, 90) + L" [" + Wide(video.id) + L"]";
    std::wstring escaped;
    for (auto c : name) { escaped += c; if (c == L'%') escaped += c; }
    return escaped + L".%(ext)s";
}
Json Encode(const State& s) {
    Json j = {{"version", 1}, {"settings", {{"destination", Utf8(s.settings.destination)}, {"tools", Utf8(s.settings.tools)},
        {"quality", s.settings.quality}, {"format", s.settings.format}, {"playlistFolder", s.settings.playlistFolder},
        {"skipDownloaded", s.settings.skipDownloaded}, {"playlistPercent", s.settings.playlistPercent}}},
        {"queue", Json::array()}, {"history", Json::array()}};
    for (const auto& q : s.queue) j["queue"].push_back({{"id", q.video.id}, {"title", Utf8(q.video.title)}, {"playlist_title", Utf8(q.video.playlist)},
        {"playlist_id", Utf8(q.video.playlistId)}, {"duration", q.video.duration}, {"token", Utf8(q.token)}, {"destination", Utf8(q.destination)},
        {"outputTemplate", Utf8(q.outputTemplate)}, {"format", q.format}, {"quality", q.quality}, {"state", static_cast<int>(q.state)},
        {"detail", Utf8(q.detail)}, {"file", Utf8(q.file)}});
    for (const auto& h : s.history) j["history"].push_back({{"id", h.id}, {"title", Utf8(h.title)}, {"format", h.format},
        {"quality", h.quality}, {"file", Utf8(h.file)}, {"date", Utf8(h.date)}});
    return j;
}
static int Quality(int q) { return q == 480 || q == 720 || q == 1080 ? q : 0; }
State Decode(const Json& j) {
    if (j.at("version").get<int>() != 1) throw std::runtime_error("Unsupported local data version.");
    State s; const auto& x = j.at("settings");
    s.settings.destination = Wide(x.at("destination").get<std::string>()); s.settings.tools = Wide(x.at("tools").get<std::string>());
    s.settings.quality = Quality(x.value("quality", 1080)); s.settings.format = x.value("format", "mp4") == "mp3" ? "mp3" : "mp4";
    s.settings.playlistFolder = x.value("playlistFolder", true);
    s.settings.skipDownloaded = x.value("skipDownloaded", true);
    s.settings.playlistPercent = std::clamp(x.value("playlistPercent", 48), 35, 65);
    for (const auto& v : j.at("queue")) {
        Job q; q.video = ParseVideo(v); q.token = Wide(v.at("token").get<std::string>());
        q.destination = Wide(v.at("destination").get<std::string>()); q.outputTemplate = Wide(v.at("outputTemplate").get<std::string>());
        q.format = v.value("format", "mp4") == "mp3" ? "mp3" : "mp4"; q.quality = Quality(v.value("quality", 1080));
        int state = v.value("state", 0);
        if (state < 0 || state > static_cast<int>(JobState::Cancelled)) throw std::runtime_error("Invalid queue state.");
        q.state = static_cast<JobState>(state);
        if (q.state == JobState::Downloading || q.state == JobState::Processing || q.state == JobState::Pending) q.state = JobState::Paused;
        q.detail = Wide(Str(v, "detail")); q.file = Wide(Str(v, "file"));
        if (q.state == JobState::Completed) q.percent = 100;
        s.queue.push_back(std::move(q));
    }
    for (const auto& v : j.at("history")) {
        History h; h.id = v.at("id").get<std::string>(); h.title = Wide(Str(v, "title")); h.format = v.at("format").get<std::string>();
        h.file = Wide(Str(v, "file")); h.date = Wide(Str(v, "date")); h.quality = Quality(v.value("quality", 0));
        s.history.push_back(std::move(h));
    }
    return s;
}
State LoadState(const fs::path& path, std::wstring& notice) {
    auto backup = path; backup += L".bak";
    if (!fs::exists(path) && !fs::exists(backup)) { State s; s.settings.destination = DefaultDownloads().wstring(); s.settings.tools = (ExeDirectory() / L"tools").wstring(); return s; }
    try { return Decode(Json::parse(ReadFile(path))); }
    catch (const std::exception&) {
        try {
            auto s = Decode(Json::parse(ReadFile(backup)));
            auto damaged = path; damaged += L".damaged";
            bool hadPrimary = fs::exists(path);
            if (hadPrimary) fs::copy_file(path, damaged, fs::copy_options::overwrite_existing);
            // Restore before the next save, so a damaged primary cannot overwrite the good backup.
            fs::copy_file(backup, path, fs::copy_options::overwrite_existing);
            notice = hadPrimary ? L"Local data was recovered from its backup. The damaged file was preserved as state.json.damaged." : L"The missing local data file was restored from its backup."; return s;
        } catch (...) { throw std::runtime_error("Local state and its backup could not be read. They were left intact. Move state.json and state.json.bak out of %LOCALAPPDATA%\\ClearFetch to start fresh."); }
    }
}
void SaveState(const fs::path& path, const State& state) { AtomicWrite(path, Encode(state).dump(2, ' ', false, Json::error_handler_t::replace)); }
}
