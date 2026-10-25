#include "Common.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <winhttp.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cwctype>

namespace cf {
std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), n);
    return result;
}
std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), n, nullptr, nullptr);
    return result;
}
std::wstring WinError(DWORD code) {
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = buffer ? buffer : L"Unknown Windows error";
    if (buffer) LocalFree(buffer);
    return message;
}
std::wstring QuoteArg(const std::wstring& value) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        if (c == L'"') out += L'\\';
        out += c;
        slashes = 0;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}
std::wstring SafeName(std::wstring value, size_t limit) {
    for (auto& c : value) if (c < 32 || std::wstring(L"<>:\"/\\|?*").find(c) != std::wstring::npos) c = L'_';
    if (value.size() > limit) value.resize(limit);
    if (!value.empty() && value.back() >= 0xd800 && value.back() <= 0xdbff) value.pop_back();
    while (!value.empty() && (value.back() == L'.' || value.back() == L' ')) value.pop_back();
    if (value.empty()) value = L"Untitled";
    std::wstring stem = value.substr(0, value.find(L'.'));
    std::transform(stem.begin(), stem.end(), stem.begin(), towupper);
    if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" ||
        (stem.size() == 4 && (stem.substr(0, 3) == L"COM" || stem.substr(0, 3) == L"LPT") && stem[3] >= L'0' && stem[3] <= L'9')) value = L"_" + value;
    return value;
}
std::wstring Duration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 3155760000.0) return L"—";
    auto n = static_cast<long long>(seconds);
    std::wostringstream s;
    if (n >= 3600) s << n / 3600 << L":" << std::setw(2) << std::setfill(L'0') << (n / 60) % 60;
    else s << std::setw(2) << std::setfill(L'0') << n / 60;
    s << L":" << std::setw(2) << std::setfill(L'0') << n % 60;
    return s.str();
}
std::wstring Now() {
    SYSTEMTIME t{}; GetLocalTime(&t);
    wchar_t b[32]; swprintf_s(b, L"%04u-%02u-%02u %02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    return b;
}
std::wstring WindowText(HWND w) {
    std::wstring s(GetWindowTextLengthW(w) + 1, L'\0');
    s.resize(GetWindowTextW(w, s.data(), static_cast<int>(s.size()))); return s;
}
fs::path ExeDirectory() {
    std::wstring s(32768, L'\0'); s.resize(GetModuleFileNameW(nullptr, s.data(), static_cast<DWORD>(s.size())));
    return fs::path(s).parent_path();
}
static fs::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) throw std::runtime_error("Cannot locate your Windows user folders.");
    fs::path result(p); CoTaskMemFree(p); return result;
}
// Keep the original profile and lock location when upgrading from ClearFetch.
fs::path DataDirectory() { auto p = KnownFolder(FOLDERID_LocalAppData) / L"ClearFetch"; fs::create_directories(p); return p; }
fs::path DefaultDownloads() { return KnownFolder(FOLDERID_Downloads); }
bool IsYoutubeUrl(const std::wstring& value) {
    if (value.size() > 8192 || value.find_first_of(L"\r\n\t ") != std::wstring::npos) return false;
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts); parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(value.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS || parts.dwUserNameLength || parts.dwPasswordLength) return false;
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::transform(host.begin(), host.end(), host.begin(), towlower);
    return parts.nPort == 443 && (host == L"youtube.com" || host == L"www.youtube.com" || host == L"m.youtube.com" || host == L"music.youtube.com" || host == L"youtu.be");
}
bool IsVideoId(const std::string& s) {
    return s.size() == 11 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
}
std::wstring PickFolder(HWND owner, const std::wstring& title, const fs::path& initial) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
    DWORD options = 0; dialog->GetOptions(&options); dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(title.c_str());
    IShellItem* folder = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&folder)))) { dialog->SetFolder(folder); folder->Release(); }
    std::wstring result;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { result = path; CoTaskMemFree(path); }
            item->Release();
        }
    }
    dialog->Release(); return result;
}
void AtomicWrite(const fs::path& path, const std::string& data) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY))
        throw std::runtime_error("Local data is read-only. Restore write access to " + Utf8(path.wstring()) + ".");
    auto temp = path; temp += L".tmp";
    {
        Handle file(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.h == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot save local data: " + Utf8(WinError()));
        DWORD written = 0;
        if (!WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) || written != data.size() || !FlushFileBuffers(file))
            throw std::runtime_error("Cannot finish writing local data: " + Utf8(WinError()));
    }
    auto backup = path; backup += L".bak";
    if (fs::exists(path) && !CopyFileW(path.c_str(), backup.c_str(), FALSE)) throw std::runtime_error("Cannot back up local data: " + Utf8(WinError()));
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace local data: " + Utf8(WinError()));
}
std::string ReadFile(const fs::path& path) {
    if (fs::file_size(path) > 64 * 1024 * 1024) throw std::runtime_error("Local state file exceeds 64 MB.");
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot read local data.");
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
}
