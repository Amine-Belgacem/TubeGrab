#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <string>
#include <vector>
#include <filesystem>
#include <stdexcept>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <algorithm>
#include <memory>
#include <cmath>
#include "json.hpp"

namespace cf {
namespace fs = std::filesystem;
using Json = nlohmann::json;
struct Handle {
    HANDLE h = nullptr;
    Handle() = default;
    explicit Handle(HANDLE value) : h(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : h(other.h) { other.h = nullptr; }
    void reset(HANDLE value = nullptr) { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); h = value; }
    operator HANDLE() const { return h; }
};
std::wstring Wide(const std::string& text);
std::string Utf8(const std::wstring& text);
std::wstring WinError(DWORD code = GetLastError());
std::wstring QuoteArg(const std::wstring& value);
std::wstring SafeName(std::wstring value, size_t limit = 100);
std::wstring Duration(double seconds);
std::wstring Now();
std::wstring WindowText(HWND window);
fs::path ExeDirectory();
fs::path DataDirectory();
fs::path DefaultDownloads();
bool IsYoutubeUrl(const std::wstring& value);
bool IsVideoId(const std::string& value);
std::wstring PickFolder(HWND owner, const std::wstring& title, const fs::path& initial);
void AtomicWrite(const fs::path& path, const std::string& data);
std::string ReadFile(const fs::path& path);
}
