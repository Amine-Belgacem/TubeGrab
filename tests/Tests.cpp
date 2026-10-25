#include "Engine.h"
#include <shellapi.h>
#include <iostream>
#include <fstream>
#include <chrono>

using namespace cf;
void RunNativeUiTests();
static int checks = 0;
static void Check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
static bool Has(int argc, wchar_t** argv, const wchar_t* option) {
    for (int i = 1; i < argc; ++i) if (std::wstring(argv[i]) == option) return true; return false;
}
static std::wstring Arg(int argc, wchar_t** argv, const wchar_t* option) {
    for (int i = 1; i + 1 < argc; ++i) if (std::wstring(argv[i]) == option) return argv[i + 1]; return {};
}
static int Fixture(int argc, wchar_t** argv) {
    if (Has(argc, argv, L"--fixture-writing-child")) {
        fs::path folder(Arg(argc, argv, L"--fixture-data"));
        std::ofstream partial(folder / L"crash.mp4.part", std::ios::binary | std::ios::app);
        partial << std::string(4096, 'x'); partial.flush();
        AtomicWrite(folder / L"ready.json", Json{{"pid", GetCurrentProcessId()}}.dump());
        for (;;) { partial << std::string(4096, 'x'); partial.flush(); Sleep(50); }
    }
    if (Has(argc, argv, L"--fixture-crash-owner")) {
        fs::path folder(Arg(argc, argv, L"--fixture-data"));
        State state; Job job; job.video.id = "abcdefghijk"; job.video.title = L"Interrupted fixture";
        job.state = JobState::Downloading; job.destination = folder.wstring(); job.outputTemplate = OutputTemplate(job.video);
        state.queue.push_back(job); state.history.push_back({"historyitem", "mp4", L"Keep history", L"saved.mp4", L"2026-10-16", 0});
        SaveState(folder / L"state.json", state);
        wchar_t exe[32768]; GetModuleFileNameW(nullptr, exe, 32768); std::atomic<Stop> signal{Stop::None};
        RunProcess(exe, {L"--fixture-writing-child", L"--fixture-data", folder.wstring()}, signal, [](const std::string&) {});
        return 0;
    }
    if (Has(argc, argv, L"-show_entries")) {
        auto file = Arg(argc, argv, L"-i");
        if (file.find(L"corrupt-file") != std::wstring::npos) { std::cout << "Invalid data found" << std::endl; return 1; }
        bool audio = fs::path(file).extension() == L".mp3";
        std::cout << Json{{"streams", audio ? Json::array({{{"codec_type", "audio"}, {"codec_name", file.find(L"wrong-codec") != std::wstring::npos ? "aac" : "mp3"}}}) :
            Json::array({{{"codec_type", "video"}, {"codec_name", file.find(L"wrong-codec") != std::wstring::npos ? "vp9" : "h264"}}, {{"codec_type", "audio"}, {"codec_name", "aac"}}})},
            {"format", {{"format_name", audio ? "mp3" : "mov,mp4,m4a,3gp,3g2,mj2"}}}}.dump() << std::endl;
        return 0;
    }
    if (Has(argc, argv, L"--fixture-noisy")) { for (;;) { std::cout << "flood" << std::endl; Sleep(1); } }
    if (Has(argc, argv, L"--fixture-exited-parent")) {
        wchar_t exe[32768]; GetModuleFileNameW(nullptr, exe, 32768); auto cmd = QuoteArg(exe) + L" --fixture-noisy";
        STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{}; si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE); si.hStdError = GetStdHandle(STD_ERROR_HANDLE); si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        if (!CreateProcessW(exe, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return 9;
        std::cout << "PID:" << pi.dwProcessId << std::endl; CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return 0;
    }
    if (Has(argc, argv, L"--fixture-args")) {
        Json args = Json::array(); for (int i = 2; i < argc; ++i) args.push_back(Utf8(argv[i])); std::cout << args.dump() << std::endl; return 0;
    }
    if (Has(argc, argv, L"--fixture-exit")) { std::cout << "ERROR: fixture failure" << std::endl; return 7; }
    if (Has(argc, argv, L"--fixture-child")) { Sleep(30000); return 0; }
    if (Has(argc, argv, L"--fixture-tree")) {
        wchar_t exe[32768]; GetModuleFileNameW(nullptr, exe, 32768);
        auto cmd = QuoteArg(exe) + L" --fixture-child";
        STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
        if (!CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return 9;
        std::cout << pi.dwProcessId << std::endl; CloseHandle(pi.hThread); CloseHandle(pi.hProcess); Sleep(30000); return 0;
    }
    if (Has(argc, argv, L"--dump-json")) {
        std::cout << R"({"id":"abcdefghijk","title":"Fixture video","duration":123,"playlist_title":"Fixture playlist","playlist_id":"PL_fixture"})" << std::endl; return 0;
    }
    if (Has(argc, argv, L"--no-playlist")) {
        auto destination = Arg(argc, argv, L"--paths"); auto pattern = Arg(argc, argv, L"--output");
        auto at = pattern.find(L"%(ext)s"); pattern.replace(at, 7, Has(argc, argv, L"--extract-audio") ? L"mp3" : L"mp4");
        auto file = fs::path(destination) / pattern;
        if (pattern.find(L"missing-file") == std::wstring::npos) {
            std::ofstream output(file, std::ios::binary);
            if (pattern.find(L"empty-file") == std::wstring::npos) output << "fixture media placeholder";
        }
        std::cout << "CFPROGRESS:{\"downloaded_bytes\":50,\"total_bytes\":100,\"speed\":10,\"eta\":5}" << std::endl;
        std::cout << "CFPOST:finished" << std::endl;
        std::cout << "CFFILE:" << Json(Utf8(file.wstring())).dump() << std::endl; return 0;
    }
    if (Has(argc, argv, L"--update")) { std::cout << "Current version: fixture" << std::endl; return 0; }
    return -1;
}
int wmain(int argc, wchar_t** argv) {
    int fixture = Fixture(argc, argv); if (fixture >= 0) return fixture;
    if (Has(argc, argv, L"--verify-media")) {
        try {
            std::atomic<Stop> signal{Stop::None};
            auto result = Engine(Arg(argc, argv, L"--tools")).VerifyOutput(Arg(argc, argv, L"--file"), Utf8(Arg(argc, argv, L"--format")), signal, [](const std::string& s) { std::cerr << s << "\n"; });
            std::cout << Json{{"success", result.success}, {"error", Utf8(result.error)}, {"file", Utf8(result.file)}}.dump() << std::endl;
            return result.success ? 0 : 1;
        } catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 2; }
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    fs::path temp;
    try {
        Check(QuoteArg(L"") == L"\"\"", "empty argument quoting");
        std::vector<std::wstring> values{L"hello", L"a b", L"quote\"end", L"C:\\space here\\", L"& echo %PATH%", L"العربية 日本語", L"\\\""};
        for (const auto& value : values) {
            auto cmd = L"tool.exe " + QuoteArg(value); int count = 0; auto args = CommandLineToArgvW(cmd.c_str(), &count);
            Check(args && count == 2 && args[1] == value, "argument round trip"); LocalFree(args);
        }
        Check(IsYoutubeUrl(L"https://www.youtube.com/watch?v=abcdefghijk&list=PL_123"), "valid playlist URL");
        Check(IsYoutubeUrl(L"https://youtu.be/abcdefghijk"), "valid short URL");
        for (auto invalid : {L"--exec calc", L"http://youtube.com", L"https://youtube.com.evil.test/v", L"https://youtube.com@evil.test", L"file:///C:/test", L"https://youtube.com:8080/x", L"https://youtube.com/\n--exec"}) Check(!IsYoutubeUrl(invalid), "reject unsafe URL");
        Check(SafeName(L"CON") == L"_CON", "reserved filename");
        Check(SafeName(L"abc. ") == L"abc", "trailing filename dots");
        Check(SafeName(L"../a:b") == L".._a_b", "filename traversal sanitization");
        Check(Duration(3661) == L"1:01:01" && Duration(-1) == L"—", "duration formatting");
        Check(Duration(1e300) == L"—", "extreme metadata durations cannot overflow integer conversion");
        Video video = ParseVideo(Json::parse(R"({"id":"abcdefghijk","title":"100% sample","duration":null,"availability":null})"));
        Check(video.duration == -1 && video.available, "nullable flat metadata");
        Check(OutputTemplate(video).find(L"100%%") != std::wstring::npos, "output template escapes percent");
        auto unavailable = ParseVideo(Json{{"id", "abcdefghijk"}, {"title", "[Private video]"}});
        Check(!unavailable.available, "private entry excluded");
        Progress progress;
        Check(ParseProgress("CFPROGRESS:{\"downloaded_bytes\":50,\"total_bytes\":null,\"total_bytes_estimate\":100,\"eta\":null}", progress) && progress.percent == 50 && progress.eta == -1, "estimated progress and null ETA");
        Check(!ParseProgress("CFPROGRESS:garbage", progress), "malformed progress rejected");
        Check(ParseProgress("CFPOST:started", progress) && progress.processing, "postprocessor progress");
        Job job; job.video = video; job.outputTemplate = OutputTemplate(video); job.destination = L"C:\\Downloads"; job.token = L"test";
        for (int height : {480, 720, 1080, 0}) {
            job.quality = height; auto args = DownloadArguments(job, L"C:\\tools");
            auto it = std::find(args.begin(), args.end(), L"-f"); Check(it != args.end(), "format selector exists");
            Check((*(it + 1)).find(L"avc1") != std::wstring::npos, "MP4 H264 compatibility");
            Check(!height || (*(it + 1)).find(L"height<=" + std::to_wstring(height)) != std::wstring::npos, "quality cap");
        }
        job.format = "mp3"; auto args = DownloadArguments(job, L"C:\\tools");
        Check(std::find(args.begin(), args.end(), L"--extract-audio") != args.end(), "MP3 extraction");
        State state; state.settings.destination = L"C:\\日本語"; state.queue.push_back(job);
        state.queue[0].state = JobState::Downloading;
        state.history.push_back({video.id, "mp3", video.title, L"C:\\saved.mp3", L"2026-10-14", 0});
        auto decoded = Decode(Encode(state));
        Check(decoded.queue[0].state == JobState::Paused && decoded.settings.destination == state.settings.destination && decoded.history[0].id == video.id, "state round trip and crash recovery");
        for (auto status : {JobState::Pending, JobState::Processing, JobState::Paused, JobState::Failed, JobState::Cancelled, JobState::Completed}) {
            state.queue[0].state = status; auto restored = Decode(Encode(state));
            auto expected = status == JobState::Pending || status == JobState::Processing ? JobState::Paused : status;
            Check(restored.queue[0].state == expected, "recovery preserves terminal states and pauses unfinished work");
        }
        bool rejected = false; try { auto future = Encode(state); future["version"] = 99; Decode(future); } catch (...) { rejected = true; }
        Check(rejected, "unknown state schema rejected");
        rejected = false; try { ParseVideo(Json{{"id", "../not-video"}}); } catch (...) { rejected = true; }
        Check(rejected, "malformed video ID rejected");
        wchar_t tempFolder[32768]; GetTempPathW(32768, tempFolder);
        temp = fs::path(tempFolder) / (L"ClearFetch-tests-" + std::to_wstring(GetCurrentProcessId())); fs::create_directories(temp);
        auto statePath = temp / L"state.json"; SaveState(statePath, state); SaveState(statePath, state);
        std::ofstream(statePath) << "corrupt"; std::wstring notice;
        auto recovered = LoadState(statePath, notice);
        Check(!notice.empty() && recovered.queue.size() == 1 && fs::exists(temp / L"state.json.damaged"), "backup recovery preserves damaged state");
        fs::remove(statePath); notice.clear();
        Check(LoadState(statePath, notice).history.size() == 1 && !notice.empty() && fs::exists(statePath), "missing primary restores the valid backup instead of resetting history");
        std::ofstream(statePath) << "unreadable primary"; std::ofstream(temp / L"state.json.bak") << "unreadable backup";
        rejected = false; try { LoadState(statePath, notice); } catch (...) { rejected = true; }
        Check(rejected && ReadFile(statePath) == "unreadable primary" && ReadFile(temp / L"state.json.bak") == "unreadable backup", "unreadable profile is preserved without overwriting evidence");
        SaveState(statePath, state); SaveState(statePath, state);
        for (const auto& blocked : {statePath, temp / L"state.json.bak"}) {
            auto committed = ReadFile(statePath);
            Check(SetFileAttributesW(blocked.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE, "create real read-only persistence fixture");
            rejected = false; try { SaveState(statePath, state); } catch (...) { rejected = true; }
            Check(rejected && ReadFile(statePath) == committed, "read-only state or backup fails without replacing committed data");
            SetFileAttributesW(blocked.c_str(), FILE_ATTRIBUTE_NORMAL); SaveState(statePath, state);
        }
        {
            auto pendingPath = statePath; pendingPath += L".tmp";
            Handle held(CreateFileW(pendingPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
            Check(held.h != INVALID_HANDLE_VALUE, "lock staged state file with a real Windows handle");
            auto committed = ReadFile(statePath); rejected = false;
            try { SaveState(statePath, state); } catch (...) { rejected = true; }
            Check(rejected && ReadFile(statePath) == committed, "exclusive staged-file lock cannot damage the committed state");
        }
        SaveState(statePath, state);
        wchar_t self[32768]; GetModuleFileNameW(nullptr, self, 32768);
        std::atomic<Stop> stop{Stop::None}; std::string out;
        std::vector<std::wstring> echo{L"--fixture-args"}; echo.insert(echo.end(), values.begin(), values.end());
        auto process = RunProcess(self, echo, stop, [&](const std::string& s) { out = s; });
        auto actual = Json::parse(out); Check(process.exitCode == 0 && actual.size() == values.size(), "real child process arguments");
        for (size_t i = 0; i < values.size(); ++i) Check(actual[i] == Utf8(values[i]), "real process preserves Unicode and metacharacters");
        auto failed = RunProcess(self, {L"--fixture-exit"}, stop, [](const std::string&) {}); Check(failed.exitCode == 7, "nonzero engine exit propagated");
        auto timeoutStarted = GetTickCount64();
        auto timedOut = RunProcess(self, {L"--fixture-child"}, stop, [](const std::string&) {}, 150);
        Check(timedOut.timedOut && !timedOut.stopped && GetTickCount64() - timeoutStarted < 3000, "silent subprocess timeout is bounded and distinct from user cancellation");
        bool descendantSeen = false; Handle descendant;
        auto rootStarted = GetTickCount64();
        auto rootEnded = RunProcess(self, {L"--fixture-exited-parent"}, stop, [&](const std::string& s) {
            if (s.rfind("PID:", 0) == 0) { descendantSeen = true; descendant.reset(OpenProcess(SYNCHRONIZE, FALSE, std::stoul(s.substr(4)))); }
        }, 3000);
        Check(rootEnded.exitCode == 0 && !rootEnded.timedOut && descendantSeen && GetTickCount64() - rootStarted < 2500, "exited root cannot hang on a noisy descendant's inherited pipe");
        Check(!descendant.h || WaitForSingleObject(descendant, 2000) == WAIT_OBJECT_0, "orphaned descendants are cleaned up after root exit");
        stop = Stop::Cancel;
        Check(RunProcess(temp / L"does-not-exist.exe", {}, stop, [](const std::string&) {}).stopped, "pre-cancelled work never launches");
        stop = Stop::None; rejected = false;
        try { RunProcess(temp / L"does-not-exist.exe", {}, stop, [](const std::string&) {}); } catch (...) { rejected = true; }
        Check(rejected, "missing process executable reports an error");
        Handle child;
        auto started = GetTickCount64();
        auto killed = RunProcess(self, {L"--fixture-tree"}, stop, [&](const std::string& s) { child.reset(OpenProcess(SYNCHRONIZE, FALSE, std::stoul(s))); stop = Stop::Cancel; });
        Check(killed.stopped && GetTickCount64() - started < 5000, "cancellation is responsive");
        Check(child.h && WaitForSingleObject(child, 2000) == WAIT_OBJECT_0, "cancellation kills descendants");
        stop = Stop::None;
        {
            auto crashFolder = temp / L"abrupt-owner-exit"; fs::create_directories(crashFolder);
            auto command = QuoteArg(self) + L" --fixture-crash-owner --fixture-data " + QuoteArg(crashFolder.wstring());
            STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
            Check(CreateProcessW(self, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != FALSE,
                "launch independent crash-test owner without an outer runner job");
            Handle owner(pi.hProcess), ownerThread(pi.hThread);
            struct StopOwner { HANDLE process; ~StopOwner() { TerminateProcess(process, ERROR_CANCELLED); WaitForSingleObject(process, 5000); } } cleanup{owner.h};
            const auto deadline = GetTickCount64() + 10000;
            while (!fs::exists(crashFolder / L"ready.json") && GetTickCount64() < deadline && WaitForSingleObject(owner, 10) == WAIT_TIMEOUT) {}
            Check(fs::exists(crashFolder / L"ready.json"), "crash worker committed its first partial bytes");
            auto workerPid = Json::parse(ReadFile(crashFolder / L"ready.json")).at("pid").get<DWORD>();
            Handle worker(OpenProcess(SYNCHRONIZE, FALSE, workerPid));
            Check(worker.h != nullptr, "open the isolated crash worker before killing its owner");
            Check(TerminateProcess(owner, ERROR_CANCELLED) != FALSE && WaitForSingleObject(owner, 5000) == WAIT_OBJECT_0,
                "abrupt owner termination skips normal application cleanup");
            Check(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0, "owner crash closes its job and kills the writing engine");
            auto partial = crashFolder / L"crash.mp4.part"; auto bytes = fs::file_size(partial); Sleep(100);
            Check(bytes > 0 && fs::file_size(partial) == bytes, "crash preserves partial data without an orphan continuing to write");
            std::wstring crashNotice; auto restored = LoadState(crashFolder / L"state.json", crashNotice);
            Check(restored.queue.size() == 1 && restored.queue[0].state == JobState::Paused && restored.history.size() == 1,
                "a fresh state load after actual owner death pauses unfinished work and keeps history");
            SaveState(crashFolder / L"state.json", restored);
            Check(LoadState(crashFolder / L"state.json", crashNotice).queue[0].state == JobState::Paused,
                "recovered state can be committed again after an owner crash");
        }
        fs::copy_file(self, temp / L"yt-dlp.exe", fs::copy_options::overwrite_existing);
        fs::copy_file(self, temp / L"ffprobe.exe", fs::copy_options::overwrite_existing);
        for (auto tool : {L"ffmpeg.exe", L"deno.exe"}) std::ofstream(temp / tool) << "fixture";
        Engine engine(temp); int found = 0;
        auto analyzed = engine.Analyze(L"https://youtu.be/abcdefghijk", stop, [&](Video v) { ++found; Check(v.playlist == L"Fixture playlist", "analysis playlist metadata"); }, [](const std::string&) {});
        Check(analyzed.exitCode == 0 && found == 1, "analysis integration");
        job.video.title = L"Fixture video"; job.outputTemplate = OutputTemplate(job.video); job.destination = (temp / L"媒体 folder").wstring();
        bool progressSeen = false, processingSeen = false;
        auto downloaded = engine.Download(job, stop, [&](Progress p) { progressSeen |= p.percent == 50; processingSeen |= p.processing; }, [](const std::string&) {});
        Check(downloaded.success && fs::exists(downloaded.file) && progressSeen && processingSeen, "engine download and final-file integration");
        for (auto title : {L"missing-file", L"empty-file"}) {
            job.video.title = title; job.outputTemplate = OutputTemplate(job.video);
            auto invalid = engine.Download(job, stop, [](Progress) {}, [](const std::string&) {});
            Check(!invalid.success && !invalid.error.empty(), "successful exit cannot complete a missing or empty output file");
        }
        for (auto format : {"mp4", "mp3"}) for (auto title : {L"corrupt-file", L"wrong-codec"}) {
            job.format = format; job.video.title = title; job.outputTemplate = OutputTemplate(job.video);
            auto invalid = engine.Download(job, stop, [](Progress) {}, [](const std::string&) {});
            Check(!invalid.success && !invalid.error.empty() && fs::exists(invalid.file), "invalid containers/codecs are rejected while the output is preserved");
        }
        auto blockedDestination = temp / L"not-a-directory"; std::ofstream(blockedDestination) << "keep";
        job.destination = blockedDestination.wstring(); rejected = false;
        try { engine.Download(job, stop, [](Progress) {}, [](const std::string&) {}); } catch (...) { rejected = true; }
        Check(rejected && ReadFile(blockedDestination) == "keep", "unwritable destination fails without overwriting an existing file");
        Check(engine.Update(stop, [](const std::string&) {}).exitCode == 0, "update integration");
        // This is an owned, uniquely named test directory under GetTempPath.
        fs::remove_all(temp); RunNativeUiTests(); CoUninitialize();
        std::cout << "PASS: " << checks << " checks (including real process-tree cancellation and fixture engine integration).\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << "\n";
        if (!temp.empty()) std::cerr << "Test artifacts retained at " << Utf8(temp.wstring()) << "\n";
        CoUninitialize(); return 1;
    }
}
