#pragma once
#include "Common.h"
namespace cf {
// A stop request terminates only this process tree. Partial files remain for --continue.
enum class Stop { None, Pause, Cancel, Shutdown };
struct ProcessResult { DWORD exitCode = 1; bool stopped = false; bool timedOut = false; };
using LineSink = std::function<void(const std::string&)>;
ProcessResult RunProcess(const fs::path& executable, const std::vector<std::wstring>& arguments,
    std::atomic<Stop>& stop, const LineSink& output, DWORD timeoutMs = 0);
}
