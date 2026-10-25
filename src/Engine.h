#pragma once
#include "Model.h"
#include "ChildProcess.h"
namespace cf {
struct Progress { double percent = 0, speed = 0, eta = -1; bool processing = false; };
struct DownloadResult { bool success = false, stopped = false; std::wstring file, error; };
std::vector<std::wstring> DownloadArguments(const Job& job, const fs::path& tools);
bool ParseProgress(const std::string& line, Progress& progress);
class Engine {
public:
    explicit Engine(fs::path tools) : tools_(std::move(tools)) {}
    void Check(bool media) const;
    ProcessResult Analyze(const std::wstring& url, std::atomic<Stop>& stop,
        const std::function<void(Video)>& video, const LineSink& log) const;
    DownloadResult Download(const Job& job, std::atomic<Stop>& stop,
        const std::function<void(Progress)>& progress, const LineSink& log) const;
    DownloadResult VerifyOutput(const fs::path& file, const std::string& format,
        std::atomic<Stop>& stop, const LineSink& log) const;
    ProcessResult Update(std::atomic<Stop>& stop, const LineSink& log) const;
private:
    fs::path tools_;
};
}
