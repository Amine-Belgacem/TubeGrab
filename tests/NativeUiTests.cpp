#include "App.h"
#include <iostream>
#include <wincodec.h>
#include <wrl/client.h>

namespace cf {
using Microsoft::WRL::ComPtr;
class NativeUiTests {
    static inline int checks_ = 0;
    static void Check(bool value, const std::string& message) {
        ++checks_; if (!value) throw std::runtime_error("Native UI: " + message);
    }
    static RECT Bounds(App& app, int id) {
        RECT r{}; GetWindowRect(app.controls_.at(id), &r);
        MapWindowPoints(nullptr, app.window_, reinterpret_cast<POINT*>(&r), 2); return r;
    }
    static bool Contains(RECT outer, RECT inner) {
        return inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right && inner.bottom <= outer.bottom;
    }
    static bool Visible(HWND window) { return (GetWindowLongPtrW(window, GWL_STYLE) & WS_VISIBLE) != 0; }
    static void Resize(App& app, int width, int height, UINT dpi) {
        app.dpi_ = dpi; app.Font(); app.ResetImages();
        SupplyThumbnail(app);
        RECT r{0, 0, app.Scale(width), app.Scale(height)};
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, TRUE, WS_EX_CONTROLPARENT, GetDpiForWindow(app.window_));
        SetWindowPos(app.window_, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
        app.Layout();
    }
    static void SupplyThumbnail(App& app) {
        if (app.videos_.empty()) return;
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 96; info.bmiHeader.biHeight = -54; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        void* pixels = nullptr; auto bitmap = std::make_shared<Bitmap>();
        bitmap->handle = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        Check(bitmap->handle != nullptr, "create a decoded-thumbnail fixture");
        std::fill_n(static_cast<DWORD*>(pixels), 96 * 54, 0xff4285b4);
        App::Event event{App::EventType::Thumb}; event.id = app.videos_[0].id; event.bitmap = bitmap;
        app.ShowThumbnail(event);
    }
    static void CheckPlaylistSpacing(App& app) {
        RECT row{}, icon{}, label{};
        ListView_GetItemRect(app.Control(App::Videos), 0, &row, LVIR_BOUNDS);
        ListView_GetItemRect(app.Control(App::Videos), 0, &icon, LVIR_ICON);
        ListView_GetItemRect(app.Control(App::Videos), 0, &label, LVIR_LABEL);
        Check(row.bottom - row.top >= app.Scale(61), "playlist rows include vertical breathing room");
        Check(label.left - (icon.left + app.Scale(12) + app.Scale(80)) >= app.Scale(12), "thumbnail and title have a visible gap");
        auto found = app.thumbnailSlots_.find(app.videos_[0].id);
        Check(found != app.thumbnailSlots_.end(), "decoded thumbnail enters the padded image list");
        HICON image = ImageList_GetIcon(app.images_, found->second, ILD_NORMAL);
        ICONINFO details{}; Check(image && GetIconInfo(image, &details), "inspect padded thumbnail pixels");
        BITMAP bitmap{}; GetObjectW(details.hbmColor, sizeof(bitmap), &bitmap);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = bitmap.bmWidth;
        info.bmiHeader.biHeight = -bitmap.bmHeight; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        std::vector<DWORD> pixels(static_cast<size_t>(bitmap.bmWidth) * bitmap.bmHeight);
        HDC dc = GetDC(app.window_); int rows = GetDIBits(dc, details.hbmColor, 0, bitmap.bmHeight, pixels.data(), &info, DIB_RGB_COLORS); ReleaseDC(app.window_, dc);
        Check(rows == bitmap.bmHeight, "read scaled thumbnail pixels");
        Check((pixels[0] >> 24) == 0 && (pixels.back() >> 24) == 0, "thumbnail padding is transparent instead of a solid block");
        Check((pixels[(bitmap.bmHeight / 2) * bitmap.bmWidth + bitmap.bmWidth / 2] >> 24) == 255, "thumbnail content remains opaque and visible");
        DeleteObject(details.hbmColor); DeleteObject(details.hbmMask); DestroyIcon(image);
        RECT bar = Bounds(app, App::CurrentProgress);
        Check(bar.bottom - bar.top == app.Scale(8), "bottom progress uses a slim track");
        Check((GetWindowLongPtrW(app.Control(App::CurrentProgress), GWL_EXSTYLE) & WS_EX_CLIENTEDGE) == 0, "progress has no recessed client border");
    }
    static void CheckText(App& app, int id, bool checkbox = false) {
        auto control = app.controls_.at(id); HDC dc = GetDC(control);
        auto old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0)));
        auto text = WindowText(control); text.erase(std::remove(text.begin(), text.end(), L'&'), text.end());
        SIZE size{}; GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
        TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
        auto bounds = Bounds(app, id);
        int padding = app.Scale(checkbox ? 26 : 12);
        Check(size.cx + padding <= bounds.right - bounds.left, "caption fits its native control: " + Utf8(text));
        Check(metrics.tmHeight + app.Scale(2) <= bounds.bottom - bounds.top, "text fits vertically: " + Utf8(text));
        SelectObject(dc, old); ReleaseDC(control, dc);
    }
    static void CheckLayout(App& app) {
        RECT client{}; GetClientRect(app.window_, &client);
        std::unordered_map<HWND, int> zOrder;
        int position = 0;
        for (HWND child = GetWindow(app.window_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) zOrder[child] = position++;
        for (auto [id, control] : app.controls_) if (Visible(control)) {
            auto r = Bounds(app, id);
            Check(r.right > r.left && r.bottom > r.top, "control has positive bounds");
            Check(Contains(client, r), "visible control stays within the client: " + std::to_string(id));
            if (id != App::CurrentProgress) {
                LOGFONTW font{};
                Check(GetObjectW(reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0)), sizeof(font), &font) != 0, "main control has an explicit UI font");
                Check(font.lfHeight == -MulDiv(10, app.dpi_, 72) && _wcsicmp(font.lfFaceName, L"Segoe UI") == 0 &&
                    font.lfWeight == (id == App::Heading || id == App::CurrentTitle ? FW_SEMIBOLD : FW_NORMAL), "main control uses the common text style");
            }
        }
        const std::vector<std::pair<int, std::vector<int>>> groups{
            {App::SourceGroup, {App::UrlLabel, App::Url, App::Paste, App::Analyze}},
            {App::PlaylistGroup, {App::Heading, App::Summary, App::SelectAll, App::SelectNone, App::Videos, App::OutputSummary, App::Options, App::Add}},
            {App::DownloadsGroup, {App::Tabs, App::Queue, App::HistoryList, App::Retry, App::ClearFinished, App::Forget, App::OpenFile, App::Pause, App::Resume, App::Cancel, App::CancelAll}},
            {App::CurrentGroup, {App::CurrentTitle, App::CurrentProgress, App::CurrentPercent, App::CurrentMetrics}}
        };
        for (const auto& group : groups) {
            auto frame = Bounds(app, group.first);
            for (int id : group.second) if (Visible(app.controls_.at(id))) {
                auto r = Bounds(app, id); Check(Contains(frame, r), "control remains inside its frame: " + std::to_string(id));
                Check(r.top >= frame.top + app.Scale(20), "control clears its frame caption");
                Check(zOrder.at(app.controls_.at(id)) < zOrder.at(app.controls_.at(group.first)),
                    "group frame cannot obscure or clip its control: " + std::to_string(id));
            }
            for (size_t a = 0; a < group.second.size(); ++a) for (size_t b = a + 1; b < group.second.size(); ++b) {
                auto first = group.second[a], second = group.second[b];
                if (!Visible(app.controls_.at(first)) || !Visible(app.controls_.at(second))) continue;
                RECT intersection{}, firstBounds = Bounds(app, first), secondBounds = Bounds(app, second);
                Check(!IntersectRect(&intersection, &firstBounds, &secondBounds), "controls do not overlap: " + std::to_string(first) + "/" + std::to_string(second));
            }
        }
        auto source = Bounds(app, App::SourceGroup), playlist = Bounds(app, App::PlaylistGroup);
        auto downloads = Bounds(app, App::DownloadsGroup), current = Bounds(app, App::CurrentGroup), status = Bounds(app, App::Status);
        Check(source.bottom < playlist.top && playlist.bottom < current.top && current.bottom < status.top, "sections remain separated vertically");
        Check(playlist.top == downloads.top && playlist.bottom == downloads.bottom && playlist.right < downloads.left, "large list panes align side by side");
        for (auto id : {App::Videos, App::Queue}) {
            auto list = Bounds(app, id);
            Check(list.bottom - list.top >= app.Scale(client.bottom >= app.Scale(700) ? 310 : 150), "both lists retain useful height at minimum window size");
            Check((GetWindowLongPtrW(app.Control(id), GWL_STYLE) & WS_HSCROLL) == 0, "list columns fit without horizontal scrolling");
        }
        Check(app.Control(App::Format) == nullptr && app.Control(App::Destination) == nullptr, "options do not occupy the main lists");
        for (auto id : {App::Paste, App::Analyze, App::SelectAll, App::SelectNone, App::Options, App::Add, App::OpenFile, App::Cancel, App::CancelAll, App::Retry, App::ClearFinished, App::Pause, App::Resume})
            if (Visible(app.Control(id))) CheckText(app, id);
    }
    static void CheckOptionsStyle(App& app, HWND dialog) {
        auto bounds = [dialog](HWND child) {
            RECT r{}; GetWindowRect(child, &r); MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&r), 2); return r;
        };
        RECT client{}; GetClientRect(dialog, &client);
        std::unordered_map<HWND, int> order; int position = 0;
        for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) order[child] = position++;
        LOGFONTW mainFont{};
        GetObjectW(reinterpret_cast<HFONT>(SendMessageW(app.Control(App::Url), WM_GETFONT, 0, 0)), sizeof(mainFont), &mainFont);
        const std::vector<std::pair<int, std::vector<int>>> groups{
            {IDC_OPT_MEDIA_GROUP, {IDC_OPT_FORMAT_LABEL, IDC_OPT_FORMAT, IDC_OPT_QUALITY_LABEL, IDC_OPT_QUALITY, IDC_OPT_COMPATIBILITY}},
            {IDC_OPT_DEST_GROUP, {IDC_OPT_DEST_LABEL, IDC_OPT_DESTINATION, IDC_OPT_BROWSE, IDC_OPT_FOLDER, IDC_OPT_SKIP}}
        };
        for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            LOGFONTW font{}; GetObjectW(reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0)), sizeof(font), &font);
            Check(font.lfHeight == mainFont.lfHeight && font.lfWeight == mainFont.lfWeight && font.lfQuality == mainFont.lfQuality &&
                _wcsicmp(font.lfFaceName, mainFont.lfFaceName) == 0, "options font exactly matches main-window input font");
            Check(Contains(client, bounds(child)), "options control is inside its dialog");
            auto id = GetDlgCtrlID(child);
            if (id != IDC_OPT_FORMAT && id != IDC_OPT_QUALITY && id != IDC_OPT_DESTINATION) {
                HDC dc = GetDC(child); auto old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0)));
                auto text = WindowText(child); text.erase(std::remove(text.begin(), text.end(), L'&'), text.end());
                SIZE size{}; GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
                auto r = bounds(child); int padding = app.Scale(id == IDC_OPT_FOLDER || id == IDC_OPT_SKIP ? 26 : 12);
                Check(size.cx + padding <= r.right - r.left && size.cy <= r.bottom - r.top, "options caption fits the shared font: " + Utf8(text));
                SelectObject(dc, old); ReleaseDC(child, dc);
            }
        }
        for (const auto& group : groups) {
            auto frame = bounds(GetDlgItem(dialog, group.first));
            for (int id : group.second) {
                auto child = GetDlgItem(dialog, id); auto r = bounds(child);
                Check(Contains(frame, r) && r.top >= frame.top + app.Scale(20), "options field clears its frame and caption");
                Check(order.at(child) < order.at(GetDlgItem(dialog, group.first)), "options frame stays behind its controls");
            }
            for (size_t a = 0; a < group.second.size(); ++a) for (size_t b = a + 1; b < group.second.size(); ++b) {
                RECT intersection{}, first = bounds(GetDlgItem(dialog, group.second[a])), second = bounds(GetDlgItem(dialog, group.second[b]));
                Check(!IntersectRect(&intersection, &first, &second), "options controls do not overlap");
            }
        }
        auto destination = bounds(GetDlgItem(dialog, IDC_OPT_DESTINATION));
        for (int id : {IDC_OPT_FORMAT, IDC_OPT_QUALITY, IDC_OPT_DESTINATION, IDC_OPT_BROWSE}) {
            auto r = bounds(GetDlgItem(dialog, id));
            Check(r.bottom - r.top == app.Scale(28), "text fields, dropdowns and Browse have the same height (id=" + std::to_string(id) +
                ", dpi=" + std::to_string(app.dpi_) + ", actual=" + std::to_string(r.bottom - r.top) + ", expected=" + std::to_string(app.Scale(28)) + ")");
        }
        auto format = bounds(GetDlgItem(dialog, IDC_OPT_FORMAT)), quality = bounds(GetDlgItem(dialog, IDC_OPT_QUALITY));
        auto browse = bounds(GetDlgItem(dialog, IDC_OPT_BROWSE)), save = bounds(GetDlgItem(dialog, IDOK)), cancel = bounds(GetDlgItem(dialog, IDCANCEL));
        Check(format.top == quality.top && format.bottom == quality.bottom, "format and quality align on the same row");
        Check(destination.top == browse.top && destination.bottom == browse.bottom, "destination and Browse align on the same row");
        Check(save.top == cancel.top && save.bottom == cancel.bottom && save.bottom - save.top == app.Scale(30), "dialog action buttons use the main action height and baseline");
        auto input = GetDlgItem(dialog, IDC_OPT_DESTINATION);
        Check(SendMessageW(input, EM_GETMARGINS, 0, 0) == SendMessageW(app.Control(App::Url), EM_GETMARGINS, 0, 0), "URL and destination text margins match");
        Check((GetWindowLongPtrW(input, GWL_STYLE) & (WS_BORDER | ES_MULTILINE | ES_AUTOHSCROLL)) ==
            (GetWindowLongPtrW(app.Control(App::Url), GWL_STYLE) & (WS_BORDER | ES_MULTILINE | ES_AUTOHSCROLL)), "edit fields have identical native text/border styles");
        Check((GetWindowLongPtrW(input, GWL_EXSTYLE) & (WS_EX_CLIENTEDGE | WS_EX_STATICEDGE)) ==
            (GetWindowLongPtrW(app.Control(App::Url), GWL_EXSTYLE) & (WS_EX_CLIENTEDGE | WS_EX_STATICEDGE)), "edit fields have identical native edge treatment");
        for (int id : {IDC_OPT_FORMAT, IDC_OPT_QUALITY}) {
            RECT combo{}, closed{}; auto control = GetDlgItem(dialog, id);
            SendMessageW(control, CB_GETDROPPEDCONTROLRECT, 0, reinterpret_cast<LPARAM>(&combo)); GetWindowRect(control, &closed);
            int itemHeight = static_cast<int>(SendMessageW(control, CB_GETITEMHEIGHT, 0, 0));
            int count = static_cast<int>(SendMessageW(control, CB_GETCOUNT, 0, 0));
            Check(combo.bottom - combo.top >= closed.bottom - closed.top + count * itemHeight,
                "native options dropdown shows every choice (id=" + std::to_string(id) + ", dpi=" + std::to_string(app.dpi_) +
                ", popup=" + std::to_string(combo.bottom - combo.top) + ", closed=" + std::to_string(closed.bottom - closed.top) +
                ", row=" + std::to_string(itemHeight) + ", count=" + std::to_string(count) + ")");
        }
    }
    static void CheckOptions(App& app) {
        auto original = Encode(app.state_);
        App::OptionsDraft draft{app.state_.settings};
        HWND dialog = CreateDialogParamW(app.instance_, MAKEINTRESOURCEW(IDD_DOWNLOAD_OPTIONS), app.window_, App::OptionsProc, reinterpret_cast<LPARAM>(&draft));
        Check(dialog != nullptr, "native download options resource loads");
        Check(WindowText(GetDlgItem(dialog, IDC_OPT_BROWSE)) == L"&Browse…", "resource captions preserve UTF-8 punctuation");
        struct Cleanup { HWND w; ~Cleanup() { if (w) DestroyWindow(w); } } cleanup{dialog};
        Check(WindowText(GetDlgItem(dialog, IDC_OPT_DESTINATION)) == app.state_.settings.destination, "options initialize from saved destination");
        for (UINT dpi : {96u, 120u, 144u, 192u}) {
            Resize(app, 1360, 860, dpi); App::StyleOptions(dialog, dpi);
            SaveRender(app, app.data_.parent_path() / (L"download-options-" + std::to_wstring(dpi * 100 / 96) + L".png"), dialog);
            CheckOptionsStyle(app, dialog);
        }
        SendDlgItemMessageW(dialog, IDC_OPT_FORMAT, CB_SETCURSEL, 1, 0);
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDC_OPT_FORMAT, CBN_SELCHANGE), 0);
        Check(!IsWindowEnabled(GetDlgItem(dialog, IDC_OPT_QUALITY)) && WindowText(GetDlgItem(dialog, IDC_OPT_COMPATIBILITY)).find(L"Audio only") == 0, "MP3 disables quality and explains output");
        SetDlgItemTextW(dialog, IDC_OPT_DESTINATION, L"relative-folder");
        bool rejected = false; try { App::ReadOptions(dialog, draft.settings); } catch (...) { rejected = true; }
        Check(rejected, "invalid destination is rejected before changing settings");
        SetDlgItemTextW(dialog, IDC_OPT_DESTINATION, L"  D:\\Videos\\Music  ");
        SendDlgItemMessageW(dialog, IDC_OPT_QUALITY, CB_SETCURSEL, 3, 0);
        CheckDlgButton(dialog, IDC_OPT_SKIP, BST_UNCHECKED);
        auto edited = App::ReadOptions(dialog, draft.settings);
        Check(edited.destination == L"D:\\Videos\\Music" && edited.format == "mp3" && edited.quality == 0 && !edited.skipDownloaded, "options read into an independent validated draft");
        Check(Encode(app.state_) == original, "editing or dismissing options never changes the active queue or saved settings");
        SaveRender(app, app.data_.parent_path() / L"download-options.png", dialog);
        DestroyWindow(dialog); cleanup.w = nullptr;
        Check(draft.font == nullptr, "closing the options dialog releases its shared-style font");
        DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int cycle = 0; cycle < 8; ++cycle) {
            App::OptionsDraft reopened{app.state_.settings};
            HWND next = CreateDialogParamW(app.instance_, MAKEINTRESOURCEW(IDD_DOWNLOAD_OPTIONS), app.window_, App::OptionsProc, reinterpret_cast<LPARAM>(&reopened));
            Check(next != nullptr, "options can reopen after font cleanup");
            App::StyleOptions(next, app.dpi_); DestroyWindow(next);
        }
        Check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= before + 2, "opening options repeatedly does not leak GDI fonts");
    }
    static void SaveRender(App& app, const fs::path& path, HWND root = nullptr) {
        if (!root) root = app.window_;
        RECT client{}; GetClientRect(root, &client);
        HDC screen = GetDC(root), dc = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, client.right, client.bottom); auto old = SelectObject(dc, bitmap);
        FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
        // Render our application's real native controls without showing a test window,
        // taking over the desktop, reading user state, or enabling network workers.
        std::vector<HWND> ordered;
        for (HWND child = GetWindow(root, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
            if (Visible(child)) ordered.push_back(child);
        // Use the real back-to-front sibling order, including the group boxes.
        // Reordering frames for rendering would hide live-window clipping bugs.
        for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
            HWND child = *it;
            RECT rect{}; GetWindowRect(child, &rect); MapWindowPoints(nullptr, root, reinterpret_cast<POINT*>(&rect), 2);
            int saved = SaveDC(dc); SetViewportOrgEx(dc, rect.left, rect.top, nullptr);
            IntersectClipRect(dc, 0, 0, rect.right - rect.left, rect.bottom - rect.top);
            SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
            RestoreDC(dc, saved);
        }
        GdiFlush();
        ComPtr<IWICImagingFactory> factory; ComPtr<IWICBitmap> source; ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder; ComPtr<IWICBitmapFrameEncode> frame;
        Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))), "WIC render factory");
        Check(SUCCEEDED(factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapIgnoreAlpha, &source)), "native render bitmap");
        Check(SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)), "render output stream");
        Check(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) && SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)), "PNG encoder");
        Check(SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)), "PNG frame");
        Check(SUCCEEDED(frame->SetSize(client.right, client.bottom)), "PNG dimensions");
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGR;
        Check(SUCCEEDED(frame->SetPixelFormat(&format)) && SUCCEEDED(frame->WriteSource(source.Get(), nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()), "save native-control render");
        SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(root, screen);
    }
    static void CheckLargePlaylistAndQueue(App& app) {
        Resize(app, 1360, 860, 96);
        app.state_.queue.clear(); app.state_.history.clear(); app.active_ = -1; app.paused_ = true;
        app.videos_.clear(); app.videoIds_.clear(); app.videoStatuses_.clear();
        ListView_DeleteAllItems(app.Control(App::Videos)); app.analysisBusy_ = true;
        constexpr int count = 5000, downloads = 100;
        size_t peakMailbox = 0; ULONGLONG longestDrain = 0;
        std::atomic<bool> sent{false};
        std::thread producer([&] {
            for (int i = 0; i < count; ++i) {
                App::Event event{App::EventType::Video};
                event.video.id = "stress" + std::to_string(10000 + i);
                event.video.title = L"Playlist item 日本語 " + std::to_wstring(i);
                event.video.duration = 60 + i; app.Post(std::move(event));
            }
            App::Event done{App::EventType::AnalysisDone}; done.ok = true; done.text = L"Stress fixture complete"; app.Post(std::move(done)); sent = true;
        });
        struct JoinProducer {
            App& app; std::thread& thread;
            ~JoinProducer() { if (thread.joinable()) { app.acceptingEvents_ = false; app.mailboxSpace_.notify_all(); thread.join(); } }
        } join{app, producer};
        auto begin = GetTickCount64();
        while (!sent || app.analysisBusy_) {
            { std::lock_guard<std::mutex> lock(app.mailboxMutex_); peakMailbox = std::max(peakMailbox, app.mailbox_.size()); }
            auto batch = GetTickCount64(); app.Drain(); longestDrain = std::max(longestDrain, GetTickCount64() - batch);
            if (GetTickCount64() - begin >= 60000) throw std::runtime_error("Large-playlist analysis exceeded 60 seconds.");
            MSG message{}; for (int i = 0; i < 4 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++i) DispatchMessageW(&message);
            Sleep(1);
        }
        producer.join();
        auto analysisMs = GetTickCount64() - begin;
        Check(app.videos_.size() == count && ListView_GetItemCount(app.Control(App::Videos)) == count,
            "5000 streamed entries reach the native list without loss");
        Check(peakMailbox <= 2048 && longestDrain < 2000, "playlist mailbox stays bounded and batches yield to Windows");
        app.Command(App::SelectAll); Check(app.selectedVideoCount_ == count, "Select all handles a large playlist");
        app.Command(App::SelectNone); Check(app.selectedVideoCount_ == 0, "Deselect all handles a large playlist");
        // Run the real App queue dispatcher against deterministic engine/probe executables.
        auto tools = app.data_ / L"fixture-tools"; fs::create_directories(tools);
        auto self = ExeDirectory() / L"TubeGrab.Tests.exe";
        fs::copy_file(self, tools / L"yt-dlp.exe", fs::copy_options::overwrite_existing);
        fs::copy_file(self, tools / L"ffprobe.exe", fs::copy_options::overwrite_existing);
        AtomicWrite(tools / L"ffmpeg.exe", "fixture"); AtomicWrite(tools / L"deno.exe", "fixture");
        app.state_.settings.tools = tools.wstring();
        app.state_.settings.destination = (app.data_ / L"stress-media").wstring();
        app.state_.settings.format = "mp4"; app.state_.settings.skipDownloaded = true;
        for (int i = 0; i < count; i += count / downloads) ListView_SetCheckState(app.Control(App::Videos), i, TRUE);
        app.RefreshSummary(); Check(app.selectedVideoCount_ == downloads, "individual selections form the intended subset");
        begin = GetTickCount64(); app.AddSelected();
        while (app.active_ >= 0 || std::any_of(app.state_.queue.begin(), app.state_.queue.end(), [](const Job& job) { return job.state == JobState::Pending; })) {
            app.Drain();
            if (GetTickCount64() - begin >= 120000) throw std::runtime_error("100-job fixture queue exceeded 120 seconds.");
            MSG message{}; for (int i = 0; i < 4 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++i) DispatchMessageW(&message);
            Sleep(2);
        }
        auto queueMs = GetTickCount64() - begin;
        Check(app.state_.queue.size() == downloads && app.state_.history.size() == downloads &&
            std::all_of(app.state_.queue.begin(), app.state_.queue.end(), [](const Job& job) { return job.state == JobState::Completed && fs::exists(job.file); }),
            "100 selected fixture downloads complete through the real dispatcher and enter history");
        app.AddSelected(); Check(app.state_.queue.size() == downloads && app.state_.history.size() == downloads,
            "queue and history prevent duplicate downloads after a large batch");
        std::wstring notice; auto restored = LoadState(app.statePath_, notice);
        Check(restored.queue.size() == downloads && restored.history.size() == downloads, "large-batch completion and history survive a fresh state load");
        AtomicWrite(app.data_.parent_path() / L"stress-result.json", Json{{"playlist_entries", count}, {"fixture_downloads", downloads},
            {"analysis_ms", analysisMs}, {"queue_ms", queueMs}, {"peak_mailbox", peakMailbox}, {"longest_drain_ms", longestDrain}}.dump(2));
        std::cout << "PASS: 5000-entry native playlist + 100-job fixture queue; analysis " << analysisMs << " ms, queue " << queueMs << " ms, peak mailbox " << peakMailbox << ", longest batch " << longestDrain << " ms.\n";
    }
public:
    static int Run() {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES};
        InitCommonControlsEx(&common);
        auto results = ExeDirectory() / L".." / L".." / L".." / L"tests" / L"results";
        fs::create_directories(results); results = fs::weakly_canonical(results);
        State state; state.settings.destination = L"D:\\Videos"; state.settings.tools = (ExeDirectory() / L"tools").wstring();
        const wchar_t* titles[]{L"01 — Getting started with C++", L"02 — Variables and data types", L"03 — Operators and expressions", L"04 — Conditions and loops", L"05 — Functions", L"06 — Classes and objects"};
        double durations[]{760, 1105, 910, 1472, 1268, 1620};
        std::vector<Video> videos;
        for (int i = 0; i < 6; ++i) { Video v; v.id = "fixture00" + std::to_string(10 + i); v.title = titles[i]; v.playlist = L"C++ Essentials"; v.duration = durations[i]; videos.push_back(v); }
        for (int i : {0, 1, 3, 5}) { Job q; q.video = videos[i]; q.token = std::to_wstring(i); q.destination = L"D:\\Videos"; q.state = i == 0 ? JobState::Completed : i == 1 ? JobState::Downloading : JobState::Pending; q.percent = i == 0 ? 100 : i == 1 ? 62 : 0; q.speed = i == 1 ? 4.8 * 1048576 : 0; q.eta = i == 1 ? 42 : -1; state.queue.push_back(q); }
        state.history.push_back({videos[0].id, "mp4", videos[0].title, L"D:\\Videos\\Getting started.mp4", L"2026-10-14 12:30", 1080});
        App app(std::move(state), results / L"isolated-profile"); fs::create_directories(app.data_);
        app.instance_ = GetModuleHandleW(nullptr); app.dpi_ = 96;
        WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = App::WindowProc; wc.hInstance = app.instance_;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); wc.lpszClassName = L"TubeGrab.NativeUiTests";
        Check(RegisterClassExW(&wc) != 0, "register isolated native test window");
        app.window_ = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"TubeGrab native UI tests", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            0, 0, 1100, 850, nullptr, nullptr, app.instance_, &app);
        Check(app.window_ != nullptr, "create real Win32 controls");
        struct Cleanup { HWND w; ~Cleanup() { DestroyWindow(w); } } cleanup{app.window_};
        for (const auto& v : videos) { App::Event e{App::EventType::Video}; e.video = v; app.Post(e); }
        app.Drain(); app.active_ = 1; app.paused_ = false; app.lastToken_ = L"1";
        Check((GetWindowLongPtrW(app.Control(App::Videos), GWL_STYLE) & WS_HSCROLL) == 0,
            "streaming playlist insertion refits columns when the vertical scrollbar appears");
        for (int i : {2, 4}) ListView_SetCheckState(app.Control(App::Videos), i, FALSE);
        app.RefreshSummary(); app.RefreshButtons();
        Check(WindowText(app.Control(App::Add)).find(L"(4)") != std::wstring::npos, "download button reflects checked rows");
        Check(SendMessageW(app.Control(App::CurrentProgress), PBM_GETPOS, 0, 0) == 620, "active strip reflects actual queue progress");
        Check(WindowText(app.Control(App::CurrentMetrics)).find(L"00:42") != std::wstring::npos, "active strip shows ETA");
        Check(app.VideoStatus(videos[1]) == L"Downloading" && app.VideoStatus(videos[0]) == L"Downloaded", "playlist reflects download and history states");
        for (UINT dpi : {96u, 120u, 144u, 192u}) {
            for (auto size : {SIZE{900, 560}, SIZE{1120, 700}, SIZE{1360, 860}, SIZE{1680, 980}}) { Resize(app, size.cx, size.cy, dpi); app.RefreshButtons(); CheckLayout(app); CheckPlaylistSpacing(app); }
            Resize(app, 1360, 860, dpi); SaveRender(app, results / (L"layout-" + std::to_wstring(dpi * 100 / 96) + L".png"));
            TabCtrl_SetCurSel(app.Control(App::Tabs), 1); app.RefreshButtons(); CheckLayout(app);
            Check(!Visible(app.Control(App::Queue)) && Visible(app.Control(App::HistoryList)), "history replaces queue without hiding active transfer");
            Check(Visible(app.Control(App::CurrentProgress)) && Visible(app.Control(App::Pause)), "queue controls remain accessible in history");
            TabCtrl_SetCurSel(app.Control(App::Tabs), 0); app.RefreshButtons();
        }
        app.Command(App::SelectNone); Check(WindowText(app.Control(App::Add)).find(L"(0)") != std::wstring::npos && !IsWindowEnabled(app.Control(App::Add)), "deselect all updates count and disables download");
        app.videos_[2].available = false; app.Command(App::SelectAll);
        Check(!ListView_GetCheckState(app.Control(App::Videos), 2) && WindowText(app.Control(App::Add)).find(L"(5)") != std::wstring::npos, "select all excludes unavailable videos");
        CheckOptions(app);
        int percent = app.state_.settings.playlistPercent;
        SendMessageW(app.Control(App::Splitter), WM_KEYDOWN, VK_RIGHT, 0);
        Check(app.state_.settings.playlistPercent == percent + 2, "splitter supports keyboard resizing");
        SendMessageW(app.Control(App::Splitter), WM_KEYDOWN, VK_HOME, 0);
        Check(app.state_.settings.playlistPercent == 48, "splitter resets to the default balance");
        app.MoveSplitter(-1000); CheckLayout(app);
        app.MoveSplitter(100000); CheckLayout(app);
        app.state_.settings.playlistPercent = 48; app.Layout();
        app.active_ = -1; app.state_.queue[1].state = JobState::Paused; app.paused_ = true; app.RefreshButtons();
        Check(Visible(app.Control(App::Resume)) && !Visible(app.Control(App::Pause)) && IsWindowEnabled(app.Control(App::Resume)), "paused queue exposes resume in the same control position");
        ListView_SetItemState(app.Control(App::Queue), 2, LVIS_SELECTED, LVIS_SELECTED); app.CancelSelected();
        Check(app.state_.queue[2].state == JobState::Cancelled, "cancel selected affects the intended queue row");
        Check(fs::exists(app.statePath_), "UI action saves only to the isolated profile");
        auto originalHistory = Encode(app.state_)["history"];
        Job paused = app.state_.queue[3]; paused.state = JobState::Paused; paused.token = L"paused";
        Job failed = paused; failed.state = JobState::Failed; failed.token = L"failed";
        app.state_.queue.push_back(paused); app.state_.queue.push_back(failed);
        app.active_ = 1; app.state_.queue[1].state = JobState::Downloading; app.paused_ = false; app.downloadStop_ = Stop::None;
        TabCtrl_SetCurSel(app.Control(App::Tabs), 1); app.RefreshQueue(); app.RefreshButtons();
        Check(Visible(app.Control(App::CancelAll)) && IsWindowEnabled(app.Control(App::CancelAll)), "Cancel all remains available from History without row selection");
        app.Command(App::CancelAll);
        Check(app.downloadStop_ == Stop::Cancel && app.paused_, "Cancel all stops the active engine and prevents dispatch");
        Check(app.state_.queue[1].state == JobState::Cancelled && app.state_.queue[3].state == JobState::Cancelled && app.state_.queue[4].state == JobState::Cancelled,
            "Cancel all includes active, pending and paused downloads");
        Check(app.state_.queue[0].state == JobState::Completed && app.state_.queue[5].state == JobState::Failed && Encode(app.state_)["history"] == originalHistory,
            "Cancel all preserves completed downloads, history and existing failure diagnostics");
        std::wstring recoveryNotice;
        Check(LoadState(app.statePath_, recoveryNotice).queue[1].state == JobState::Cancelled, "active cancellation is saved before the engine acknowledges it");
        App::Event late{App::EventType::Progress}; late.progress.percent = 90; app.Post(late); app.Drain();
        Check(app.state_.queue[1].state == JobState::Cancelled, "late progress cannot revive a cancelled download");
        app.PauseQueue(); Check(app.downloadStop_ == Stop::Cancel, "pause cannot undo an in-flight cancellation");
        Check(!IsWindowEnabled(app.Control(App::CancelAll)), "Cancel all disables after all unfinished downloads are cancelled");
        app.active_ = -1; app.downloadStop_ = Stop::None; TabCtrl_SetCurSel(app.Control(App::Tabs), 0);
        app.state_.settings.skipDownloaded = false;
        Check(!Decode(Encode(app.state_)).settings.skipDownloaded, "skip-downloaded preference persists");
        auto legacy = Encode(app.state_); legacy["settings"].erase("skipDownloaded");
        Check(Decode(legacy).settings.skipDownloaded, "older profiles retain duplicate protection");
        Check(Decode(Encode(app.state_)).settings.playlistPercent == app.state_.settings.playlistPercent, "splitter position survives restart");
        auto savedPath = app.statePath_; auto originalFile = ReadFile(savedPath);
        app.statePath_ = app.data_ / L"blocked"; fs::create_directories(app.statePath_);
        app.active_ = 1; app.state_.queue[1].state = JobState::Downloading;
        app.state_.queue[3].state = JobState::Pending; app.downloadStop_ = Stop::None;
        Check(!app.Persist() && app.paused_ && app.storageFailed_ && app.downloadStop_ == Stop::Pause && app.state_.queue[3].state == JobState::Paused,
            "save failure pauses the active engine and undispatched jobs");
        app.PauseQueue();
        Check(WindowText(app.Control(App::Status)).find(L"restore folder access") != std::wstring::npos,
            "Pause retains the actionable storage failure instead of replacing it with a normal paused message");
        Check(ReadFile(savedPath) == originalFile, "save failure preserves the previous committed profile");
        app.active_ = -1; app.statePath_ = savedPath; app.downloadStop_ = Stop::None;
        Check(app.Persist(), "saving can recover after folder access is restored");
        for (int i = 0; i < 10000; ++i) { App::Event e{App::EventType::Progress}; e.progress.percent = i % 100; app.Post(e); }
        Check(app.mailbox_.size() == 1 && app.mailbox_.front().progress.percent == 99, "progress storms are coalesced without losing the newest sample");
        app.Drain();
        CheckLargePlaylistAndQueue(app);
        std::cout << "PASS: " << checks_ << " native UI checks across 100%, 125%, 150%, 200% scaling and four window sizes.\n";
        std::cout << "Native-control renders: " << Utf8(results.wstring()) << "\n";
        return checks_;
    }
};
}
void RunNativeUiTests() { cf::NativeUiTests::Run(); }
