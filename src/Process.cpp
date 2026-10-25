#include "ChildProcess.h"
namespace cf {
ProcessResult RunProcess(const fs::path& executable, const std::vector<std::wstring>& arguments,
    std::atomic<Stop>& stop, const LineSink& output, DWORD timeoutMs) {
    if (stop != Stop::None) return {ERROR_CANCELLED, true};
    std::wstring command = QuoteArg(executable.wstring());
    for (const auto& argument : arguments) command += L" " + QuoteArg(argument);
    if (command.size() >= 32767) throw std::runtime_error("The engine command is too long.");
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE r = nullptr, w = nullptr;
    if (!CreatePipe(&r, &w, &sa, 0)) throw std::runtime_error("Cannot create engine output pipe.");
    Handle reader(r), writer(w);
    if (!SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0)) throw std::runtime_error("Cannot isolate the engine output handle.");
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr));
    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (!job.h || input.h == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot initialize engine process.");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) throw std::runtime_error("Cannot configure engine process isolation.");
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<unsigned char> storage(size);
    auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attrs, 1, 0, &size)) throw std::runtime_error("Cannot initialize process attributes.");
    struct Cleanup { LPPROC_THREAD_ATTRIBUTE_LIST p; ~Cleanup() { DeleteProcThreadAttributeList(p); } } cleanup{attrs};
    HANDLE inherited[] = {writer.h, input.h};
    if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)) throw std::runtime_error("Cannot configure engine handles.");
    STARTUPINFOEXW si{}; si.StartupInfo.cb = sizeof(si); si.lpAttributeList = attrs;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = writer; si.StartupInfo.hStdError = writer; si.StartupInfo.hStdInput = input;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
        executable.parent_path().c_str(), &si.StartupInfo, &pi))
        throw std::runtime_error("Cannot start " + Utf8(executable.filename().wstring()) + ": " + Utf8(WinError()));
    Handle process(pi.hProcess), thread(pi.hThread);
    if (!AssignProcessToJobObject(job, process)) {
        TerminateProcess(process, 1); throw std::runtime_error("Windows could not isolate the engine process tree.");
    }
    if (ResumeThread(thread) == static_cast<DWORD>(-1)) throw std::runtime_error("Cannot start engine thread.");
    writer.reset(); input.reset();
    std::string line;
    bool killed = false, timedOut = false, rootExited = false;
    const auto started = GetTickCount64();
    auto check = [&] {
        if (!rootExited && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
            rootExited = true;
            // A descendant must not keep a completed engine's inherited pipe
            // busy forever. The root's exit code still determines success.
            if (!TerminateJobObject(job, ERROR_CANCELLED)) throw std::runtime_error("Cannot clean up the engine process tree.");
        }
        if (stop != Stop::None && !killed) {
            if (!TerminateJobObject(job, ERROR_CANCELLED)) throw std::runtime_error("Cannot stop the engine process tree.");
            killed = true;
        }
        if (timeoutMs && !rootExited && !killed && !timedOut && GetTickCount64() - started >= timeoutMs) {
            if (!TerminateJobObject(job, WAIT_TIMEOUT)) throw std::runtime_error("Cannot stop a timed-out engine process.");
            timedOut = true;
        }
    };
    for (;;) {
        check();
        DWORD available = 0;
        if (!PeekNamedPipe(reader, nullptr, 0, nullptr, &available, nullptr)) break;
        if (available) {
            char buffer[16384]; DWORD count = 0;
            if (!::ReadFile(reader, buffer, std::min<DWORD>(available, sizeof(buffer)), &count, nullptr)) break;
            for (DWORD i = 0; i < count; ++i) {
                char c = buffer[i];
                if (c == '\n' || c == '\r') { if (!line.empty()) { output(line); line.clear(); } }
                else { line += c; if (line.size() > 16 * 1024 * 1024) throw std::runtime_error("Engine output line exceeds 16 MB."); }
            }
        } else if (WaitForSingleObject(process, 20) == WAIT_OBJECT_0) {
            // Read any final bytes already in the pipe before returning.
            if (!PeekNamedPipe(reader, nullptr, 0, nullptr, &available, nullptr) || !available) break;
        }
    }
    if (!line.empty()) output(line);
    // A child may have closed its pipe early. Keep waiting responsively to cancellation.
    while (WaitForSingleObject(process, 50) == WAIT_TIMEOUT) {
        check();
    }
    DWORD code = 1;
    if (!GetExitCodeProcess(process, &code)) throw std::runtime_error("Cannot read the engine exit status.");
    return {code, killed, timedOut};
}
}
