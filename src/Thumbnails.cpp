#include "Thumbnails.h"
#include <winhttp.h>
#include <wincodec.h>
#include <wrl/client.h>
namespace cf {
using Microsoft::WRL::ComPtr;
struct Http { HINTERNET h; ~Http() { if (h) WinHttpCloseHandle(h); } operator HINTERNET() const { return h; } };
static std::shared_ptr<Bitmap> Fetch(const std::string& id, const std::atomic<bool>& stop) {
    if (!IsVideoId(id)) return {};
    Http session{WinHttpOpen(L"TubeGrab/2.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0)};
    if (!session.h) return {};
    WinHttpSetTimeouts(session, 2000, 2000, 2000, 2000);
    Http connection{WinHttpConnect(session, L"i.ytimg.com", INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.h) return {};
    auto path = L"/vi/" + Wide(id) + L"/mqdefault.jpg";
    Http request{WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.h || stop) return {};
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    if (!WinHttpSendRequest(request, nullptr, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr)) return {};
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr) || status != 200) return {};
    std::vector<BYTE> bytes;
    auto deadline = GetTickCount64() + 6000;
    while (!stop && GetTickCount64() < deadline) {
        BYTE buffer[8192]; DWORD read = 0;
        if (!WinHttpReadData(request, buffer, sizeof(buffer), &read)) return {};
        if (!read) break;
        if (bytes.size() + read > 512 * 1024) return {};
        bytes.insert(bytes.end(), buffer, buffer + read);
    }
    if (stop || bytes.empty()) return {};
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return {};
    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromMemory(bytes.data(), static_cast<DWORD>(bytes.size())))) return {};
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder))) return {};
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return {};
    UINT width = 0, height = 0; frame->GetSize(&width, &height);
    if (!width || !height || width > 4096 || height > 4096) return {};
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(factory->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(frame.Get(), 96, 54, WICBitmapInterpolationModeFant))) return {};
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) || FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = 96; info.bmiHeader.biHeight = -54;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr; auto bitmap = std::make_shared<Bitmap>();
    bitmap->handle = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap->handle || FAILED(converter->CopyPixels(nullptr, 96 * 4, 96 * 54 * 4, static_cast<BYTE*>(bits)))) return {};
    return bitmap;
}
Thumbnails::Thumbnails(Sink sink) : sink_(std::move(sink)), thread_([this] { Work(); }) {}
Thumbnails::~Thumbnails() { Stop(); if (thread_.joinable()) thread_.join(); }
void Thumbnails::Stop() { stop_ = true; wake_.notify_all(); }
void Thumbnails::Request(const std::vector<std::string>& ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Only retain nearby rows: a long scroll cannot create a huge HTTP backlog.
    pending_.clear(); requested_.clear();
    for (const auto& id : ids) if (id != working_ && requested_.insert(id).second) pending_.push_back(id);
    wake_.notify_one();
}
void Thumbnails::Work() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        std::string id;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stop_ || !pending_.empty(); });
            if (stop_) break;
            id = pending_.front(); pending_.pop_front(); working_ = id;
        }
        try { sink_(id, Fetch(id, stop_)); } catch (...) { sink_(id, {}); }
        { std::lock_guard<std::mutex> lock(mutex_); working_.clear(); }
    }
    CoUninitialize();
}
}
