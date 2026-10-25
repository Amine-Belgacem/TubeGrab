#include "Engine.h"
namespace cf {
static std::vector<std::wstring> BaseArguments(const fs::path& tools) {
    std::vector<std::wstring> args{L"--ignore-config", L"--no-plugin-dirs", L"--no-colors", L"--encoding", L"utf-8",
        L"--socket-timeout", L"20", L"--extractor-retries", L"3", L"--no-js-runtimes", L"--js-runtimes", L"deno:" + (tools / L"deno.exe").wstring()};
    return args;
}
void Engine::Check(bool media) const {
    std::vector<std::wstring> names{L"yt-dlp.exe", L"deno.exe"};
    if (media) { names.push_back(L"ffmpeg.exe"); names.push_back(L"ffprobe.exe"); }
    for (const auto& name : names) if (!fs::is_regular_file(tools_ / name))
        throw std::runtime_error("Missing " + Utf8(name) + ". Run scripts\\Setup-Tools.ps1, or choose a tools folder containing yt-dlp.exe, ffmpeg.exe, ffprobe.exe and deno.exe.");
}
ProcessResult Engine::Analyze(const std::wstring& url, std::atomic<Stop>& stop,
    const std::function<void(Video)>& video, const LineSink& log) const {
    Check(false);
    auto args = BaseArguments(tools_);
    args.insert(args.end(), {L"--flat-playlist", L"--dump-json", L"--skip-download", L"--yes-playlist", L"--lazy-playlist", L"--", url});
    return RunProcess(tools_ / L"yt-dlp.exe", args, stop, [&](const std::string& line) {
        if (!line.empty() && line.front() == '{') {
            try { video(ParseVideo(Json::parse(line))); }
            catch (const std::exception& e) { log(std::string("Skipped an unreadable entry: ") + e.what()); }
        } else log(line);
    });
}
std::vector<std::wstring> DownloadArguments(const Job& job, const fs::path& tools) {
    auto args = BaseArguments(tools);
    args.insert(args.end(), {L"--no-playlist", L"--continue", L"--part", L"--no-overwrites", L"--windows-filenames",
        L"--retries", L"5", L"--fragment-retries", L"5", L"--retry-sleep", L"http:exp=1:20", L"--abort-on-unavailable-fragments",
        L"--newline", L"--progress", L"--progress-delta", L"0.3", L"--no-simulate",
        L"--progress-template", L"download:CFPROGRESS:{\"downloaded_bytes\":%(progress.downloaded_bytes|0)j,\"total_bytes\":%(progress.total_bytes|0)j,\"total_bytes_estimate\":%(progress.total_bytes_estimate|0)j,\"speed\":%(progress.speed|0)j,\"eta\":%(progress.eta|-1)j}",
        L"--progress-template", L"postprocess:CFPOST:%(progress.status)s",
        L"--print", L"after_move:CFFILE:%(filepath)j", L"--ffmpeg-location", tools.wstring(),
        L"--paths", job.destination, L"--output", job.outputTemplate});
    if (job.format == "mp3") args.insert(args.end(), {L"-f", L"bestaudio/best", L"--extract-audio", L"--audio-format", L"mp3", L"--audio-quality", L"0"});
    else {
        auto height = job.quality ? L"[height<=" + std::to_wstring(job.quality) + L"]" : L"";
        auto format = L"bv[ext=mp4][vcodec^=avc1]" + height + L"+ba[ext=m4a][acodec^=mp4a]/b[ext=mp4][vcodec^=avc1][acodec^=mp4a]" + height;
        args.insert(args.end(), {L"-f", format, L"--merge-output-format", L"mp4", L"--remux-video", L"mp4",
            L"--postprocessor-args", L"Merger+ffmpeg_o:-movflags +faststart"});
    }
    args.insert(args.end(), {L"--", job.video.url()}); return args;
}
static double Num(const Json& j, const char* key, double fallback = 0) {
    auto i = j.find(key); return i != j.end() && i->is_number() ? i->get<double>() : fallback;
}
bool ParseProgress(const std::string& line, Progress& p) {
    if (line.rfind("CFPOST:", 0) == 0) { p.processing = true; p.percent = 100; return true; }
    if (line.rfind("CFPROGRESS:", 0) != 0) return false;
    try {
        auto j = Json::parse(line.substr(11));
        double total = Num(j, "total_bytes", Num(j, "total_bytes_estimate"));
        // total_bytes can be null while total_bytes_estimate is present.
        if (total <= 0) total = Num(j, "total_bytes_estimate");
        p.percent = total > 0 ? std::clamp(Num(j, "downloaded_bytes") * 100 / total, 0.0, 100.0) : 0;
        p.speed = Num(j, "speed"); p.eta = Num(j, "eta", -1);
        p.processing = false; return true;
    } catch (...) { return false; }
}
DownloadResult Engine::VerifyOutput(const fs::path& file, const std::string& format,
    std::atomic<Stop>& stop, const LineSink& log) const {
    DownloadResult result; result.file = file.wstring();
    if (stop != Stop::None) { result.stopped = true; return result; }
    if (!fs::is_regular_file(file) || fs::file_size(file) == 0) {
        result.error = L"The output file is missing or empty. Open the log and retry."; return result;
    }
    std::string output;
    auto probe = RunProcess(tools_ / L"ffprobe.exe", {L"-v", L"error", L"-show_entries",
        L"stream=codec_type,codec_name:format=format_name", L"-of", L"json", L"-i", file.wstring()}, stop,
        [&](const std::string& line) {
            if (output.size() + line.size() > 256 * 1024) throw std::runtime_error("Media verification output exceeded its limit.");
            output += line + "\n";
        }, 30000);
    result.stopped = probe.stopped;
    if (probe.stopped) return result;
    if (!output.empty()) log("ffprobe: " + output);
    if (probe.timedOut) { result.error = L"Media verification timed out. The file was kept at " + file.wstring() + L". Check the tools or disk, then retry."; return result; }
    bool compatible = false;
    if (probe.exitCode == 0) {
        try {
            auto metadata = Json::parse(output);
            bool h264 = false, aac = false, mp3 = false, video = false;
            for (const auto& stream : metadata.at("streams")) {
                auto codec = stream.value("codec_name", ""); auto type = stream.value("codec_type", "");
                video |= type == "video"; h264 |= type == "video" && codec == "h264";
                aac |= type == "audio" && codec == "aac"; mp3 |= type == "audio" && codec == "mp3";
            }
            auto container = metadata.at("format").value("format_name", "");
            compatible = format == "mp3" ? mp3 && !video && container.find("mp3") != std::string::npos :
                h264 && aac && container.find("mp4") != std::string::npos;
        } catch (...) {}
    }
    result.success = compatible;
    if (!compatible) result.error = L"The output could not be verified as " + std::wstring(format == "mp3" ? L"MP3 audio" : L"H.264/AAC MP4") +
        L". It was kept at " + file.wstring() + L". Open the log; move an unreadable file aside before retrying so existing media is not overwritten.";
    return result;
}
DownloadResult Engine::Download(const Job& job, std::atomic<Stop>& stop,
    const std::function<void(Progress)>& progress, const LineSink& log) const {
    Check(true); fs::create_directories(job.destination);
    DownloadResult result;
    auto r = RunProcess(tools_ / L"yt-dlp.exe", DownloadArguments(job, tools_), stop, [&](const std::string& line) {
        Progress p;
        if (ParseProgress(line, p)) progress(p);
        else if (line.rfind("CFFILE:", 0) == 0) {
            try { result.file = Wide(Json::parse(line.substr(7)).get<std::string>()); } catch (...) { log("Could not read the output filename."); }
        } else {
            log(line);
            if (line.rfind("ERROR:", 0) == 0) result.error = Wide(line);
            if (line.rfind("[Merger]", 0) == 0 || line.rfind("[ExtractAudio]", 0) == 0 || line.rfind("[VideoRemuxer]", 0) == 0)
                progress({100, 0, -1, true});
        }
    });
    result.stopped = r.stopped;
    result.success = !r.stopped && r.exitCode == 0 && !result.file.empty() && fs::is_regular_file(result.file) && fs::file_size(result.file) > 0;
    if (result.success) {
        progress({100, 0, -1, true}); log("Verifying the output container and codecs with ffprobe.");
        return VerifyOutput(result.file, job.format, stop, log);
    }
    if (!result.success && !r.stopped && result.error.empty())
        result.error = L"The engine did not produce a verified output file (exit " + std::to_wstring(r.exitCode) + L"). Open the log for details, check the tools, and retry.";
    return result;
}
ProcessResult Engine::Update(std::atomic<Stop>& stop, const LineSink& log) const {
    if (!fs::is_regular_file(tools_ / L"yt-dlp.exe")) throw std::runtime_error("Install yt-dlp.exe with scripts\\Setup-Tools.ps1 first.");
    return RunProcess(tools_ / L"yt-dlp.exe", {L"--ignore-config", L"--no-plugin-dirs", L"--no-colors", L"--encoding", L"utf-8", L"--update"}, stop, log);
}
}
