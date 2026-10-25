#include "App.h"
#include <uxtheme.h>
#include <vsstyle.h>
#include <sstream>
#include <iomanip>

namespace cf {
static LRESULT CALLBACK FrameBackground(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR) {
    // A group box only paints its frame. With WS_CLIPCHILDREN on the main
    // window, explicitly erase the group's interior so it shares the native
    // dialog background with its labels and checkboxes.
    if (message == WM_ERASEBKGND) {
        RECT client{}; GetClientRect(window, &client);
        FillRect(reinterpret_cast<HDC>(w), &client, GetSysColorBrush(COLOR_BTNFACE));
        return TRUE;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, FrameBackground, id);
    return DefSubclassProc(window, message, w, l);
}
static std::wstring JobFormatLabel(const Job& job) {
    return job.format == "mp3" ? L"MP3 audio" : (job.quality ? std::to_wstring(job.quality) + L"p MP4" : L"Best MP4");
}
static std::wstring SpeedLabel(double speed) {
    if (speed <= 0 || !std::isfinite(speed)) return L"—";
    std::wostringstream s; s << std::fixed << std::setprecision(1) << speed / 1048576 << L" MB/s"; return s.str();
}
static std::wstring CompactState(JobState state) {
    switch (state) {
    case JobState::Pending: return L"Queued";
    case JobState::Downloading: return L"Downloading";
    case JobState::Processing: return L"Processing media";
    case JobState::Paused: return L"Paused";
    case JobState::Completed: return L"Completed";
    case JobState::Failed: return L"Failed";
    case JobState::Cancelled: return L"Cancelled";
    } return L"—";
}
LRESULT CALLBACK App::ProgressProc(HWND progress, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
    auto app = reinterpret_cast<App*>(data);
    switch (message) {
    case WM_ERASEBKGND: return TRUE;
    case WM_PAINT: case WM_PRINT: case WM_PRINTCLIENT: {
        // Keep the native progress control's messages and accessibility, with a
        // flat, rounded Win32/GDI treatment instead of the classic beveled frame.
        PAINTSTRUCT paint{};
        HDC dc = message == WM_PAINT ? BeginPaint(progress, &paint) : reinterpret_cast<HDC>(w);
        RECT client{}; GetClientRect(progress, &client);
        FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
        HIGHCONTRASTW contrast{sizeof(contrast)}; SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
        bool highContrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
        COLORREF track = highContrast ? GetSysColor(COLOR_WINDOWFRAME) : RGB(221, 226, 232);
        int state = static_cast<int>(SendMessageW(progress, PBM_GETSTATE, 0, 0));
        COLORREF color = GetSysColor(COLOR_HIGHLIGHT);
        if (!highContrast && state == PBST_PAUSED) color = RGB(192, 128, 28);
        if (!highContrast && state == PBST_ERROR) color = RGB(196, 48, 48);
        PBRANGE range{}; SendMessageW(progress, PBM_GETRANGE, FALSE, reinterpret_cast<LPARAM>(&range));
        int position = static_cast<int>(SendMessageW(progress, PBM_GETPOS, 0, 0));
        double fraction = range.iHigh > range.iLow ? std::clamp((position - static_cast<double>(range.iLow)) / (range.iHigh - static_cast<double>(range.iLow)), 0.0, 1.0) : 0;
        auto rounded = [&](RECT rect, COLORREF fill) {
            HRGN region = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1, rect.bottom + 1, app->Scale(8), app->Scale(8));
            HBRUSH brush = CreateSolidBrush(fill);
            if (region && brush) FillRgn(dc, region, brush);
            if (brush) DeleteObject(brush); if (region) DeleteObject(region);
        };
        rounded(client, track);
        if (fraction > 0) { RECT fill = client; fill.right = fill.left + std::max(1, static_cast<int>((client.right - client.left) * fraction)); rounded(fill, color); }
        if (message == WM_PAINT) EndPaint(progress, &paint); return 0;
    }
    case PBM_SETPOS: case PBM_SETRANGE32: case PBM_SETSTATE: {
        auto result = DefSubclassProc(progress, message, w, l); InvalidateRect(progress, nullptr, FALSE); return result;
    }
    case WM_NCDESTROY: RemoveWindowSubclass(progress, ProgressProc, id); break;
    }
    return DefSubclassProc(progress, message, w, l);
}
void App::MoveSplitter(int clientX) {
    RECT client{}; GetClientRect(window_, &client);
    int available = MulDiv(client.right, 96, dpi_) - 34;
    int left = std::clamp(MulDiv(clientX, 96, dpi_) - 12, 400, std::max(400, available - 420));
    state_.settings.playlistPercent = std::clamp(MulDiv(left, 100, std::max(available, 1)), 35, 65);
    Layout();
}
LRESULT CALLBACK App::SplitterProc(HWND splitter, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
    auto app = reinterpret_cast<App*>(data);
    switch (message) {
    case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); return TRUE;
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case WM_LBUTTONDOWN: SetFocus(splitter); SetCapture(splitter); app->splitterDragging_ = true; return 0;
    case WM_MOUSEMOVE:
        if (app->splitterDragging_) {
            POINT point{static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))};
            MapWindowPoints(splitter, app->window_, &point, 1); app->MoveSplitter(point.x);
        } return 0;
    case WM_LBUTTONUP:
        if (app->splitterDragging_) { app->splitterDragging_ = false; ReleaseCapture(); app->Persist(); }
        return 0;
    case WM_CAPTURECHANGED: app->splitterDragging_ = false; return 0;
    case WM_KEYDOWN:
        if (w == VK_LEFT || w == VK_RIGHT || w == VK_HOME) {
            app->state_.settings.playlistPercent = w == VK_HOME ? 48 : std::clamp(app->state_.settings.playlistPercent + (w == VK_LEFT ? -2 : 2), 35, 65);
            app->Layout(); app->Persist(); return 0;
        } break;
    case WM_SETFOCUS: case WM_KILLFOCUS: InvalidateRect(splitter, nullptr, FALSE); return 0;
    case WM_PAINT: case WM_PRINT: case WM_PRINTCLIENT: {
        PAINTSTRUCT paint{}; auto dc = message == WM_PAINT ? BeginPaint(splitter, &paint) : reinterpret_cast<HDC>(w);
        RECT client{}; GetClientRect(splitter, &client); FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
        RECT grip{client.right / 2, client.bottom / 2 - app->Scale(20), client.right / 2 + 1, client.bottom / 2 + app->Scale(20)};
        FillRect(dc, &grip, GetSysColorBrush(COLOR_3DSHADOW));
        if (GetFocus() == splitter) DrawFocusRect(dc, &client);
        if (message == WM_PAINT) EndPaint(splitter, &paint); return 0;
    }
    case WM_NCDESTROY: RemoveWindowSubclass(splitter, SplitterProc, id); break;
    }
    return DefSubclassProc(splitter, message, w, l);
}
void App::CreateMenuBar() {
    menu_ = CreateMenu();
    auto file = CreatePopupMenu(), downloads = CreatePopupMenu(), tools = CreatePopupMenu(), help = CreatePopupMenu();
    if (!menu_ || !file || !downloads || !tools || !help) throw std::runtime_error("Cannot create the application menus.");
    AppendMenuW(file, MF_STRING, Paste, L"&Paste URL");
    AppendMenuW(file, MF_STRING, Analyze, L"&Analyze URL\tEnter");
    AppendMenuW(file, MF_STRING, StopAnalysis, L"&Stop analysis");
    AppendMenuW(file, MF_STRING, Options, L"Download &options…");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr); AppendMenuW(file, MF_STRING, Exit, L"E&xit");
    AppendMenuW(downloads, MF_STRING, Pause, L"&Pause queue"); AppendMenuW(downloads, MF_STRING, Resume, L"&Resume queue");
    AppendMenuW(downloads, MF_STRING, Cancel, L"&Cancel selected"); AppendMenuW(downloads, MF_STRING, Retry, L"&Retry selected");
    AppendMenuW(downloads, MF_STRING, CancelAll, L"Cancel &all downloads");
    AppendMenuW(downloads, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(downloads, MF_STRING, OpenFile, L"&Show file"); AppendMenuW(downloads, MF_STRING, ClearFinished, L"C&lear finished");
    AppendMenuW(downloads, MF_STRING, Forget, L"&Forget selected history");
    AppendMenuW(tools, MF_STRING, Tools, L"Tools &folder…"); AppendMenuW(tools, MF_STRING, Update, L"&Update yt-dlp");
    AppendMenuW(tools, MF_SEPARATOR, 0, nullptr); AppendMenuW(tools, MF_STRING, Log, L"Open &log");
    AppendMenuW(help, MF_STRING, About, L"&About TubeGrab");
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(downloads), L"&Downloads");
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(tools), L"&Tools");
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");
    SetMenu(window_, menu_);
}
void App::CreateControls() {
    CreateMenuBar();
    // Frames are created first and never receive keyboard focus. Their sibling controls
    // retain their own native painting, accessibility, and navigation behavior.
    Make(SourceGroup, L"BUTTON", L"Source", BS_GROUPBOX);
    Make(PlaylistGroup, L"BUTTON", L"Playlist", BS_GROUPBOX);
    Make(DownloadsGroup, L"BUTTON", L"Downloads", BS_GROUPBOX);
    Make(CurrentGroup, L"BUTTON", L"Current transfer", BS_GROUPBOX);
    Make(UrlLabel, L"STATIC", L"YouTube &URL:", SS_LEFT);
    Make(Url, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
    StyleTextField(Control(Url), dpi_);
    SendMessageW(Control(Url), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"https://www.youtube.com/watch?v=… or a playlist URL"));
    SendMessageW(Control(Url), EM_SETLIMITTEXT, 8192, 0);
    Make(Paste, L"BUTTON", L"&Paste", WS_TABSTOP);
    Make(Analyze, L"BUTTON", L"&Analyze", WS_TABSTOP);
    Make(Heading, L"STATIC", L"No playlist analyzed", SS_LEFT | SS_ENDELLIPSIS);
    Make(Summary, L"STATIC", L"Paste a URL above to choose videos.", SS_LEFT | SS_ENDELLIPSIS);
    Make(SelectAll, L"BUTTON", L"Select all", WS_TABSTOP);
    Make(SelectNone, L"BUTTON", L"Deselect all", WS_TABSTOP);
    auto list = Make(Videos, WC_LISTVIEWW, L"Playlist videos", WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    ResetImages(); Column(list, 0, L"Video", 450); Column(list, 1, L"Duration", 72); Column(list, 2, L"Status", 114);
    Make(OutputSummary, L"STATIC", L"", SS_LEFT | SS_PATHELLIPSIS);
    Make(Options, L"BUTTON", L"Download &options…", WS_TABSTOP);
    Make(Splitter, L"STATIC", L"Resize playlist and downloads", WS_TABSTOP | SS_NOTIFY);
    if (!SetWindowSubclass(Control(Splitter), SplitterProc, 1, reinterpret_cast<DWORD_PTR>(this)))
        throw std::runtime_error("Cannot initialize the pane divider.");
    Make(Add, L"BUTTON", L"&Download selected (0)", WS_TABSTOP | BS_DEFPUSHBUTTON);
    Make(Tabs, WC_TABCONTROLW, L"", WS_TABSTOP | TCS_FIXEDWIDTH);
    TCITEMW tab{}; tab.mask = TCIF_TEXT; tab.pszText = const_cast<wchar_t*>(L"Queue (0)"); TabCtrl_InsertItem(Control(Tabs), 0, &tab);
    tab.pszText = const_cast<wchar_t*>(L"History (0)"); TabCtrl_InsertItem(Control(Tabs), 1, &tab);
    Make(Retry, L"BUTTON", L"Retry", WS_TABSTOP);
    Make(ClearFinished, L"BUTTON", L"Clear", WS_TABSTOP);
    Make(Forget, L"BUTTON", L"Forget selected", WS_TABSTOP);
    Make(OpenFile, L"BUTTON", L"Show file", WS_TABSTOP);
    auto queue = Make(Queue, WC_LISTVIEWW, L"Download queue", WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(queue, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    Column(queue, 0, L"Video", 390); Column(queue, 1, L"Format", 104); Column(queue, 2, L"Progress", 155);
    Column(queue, 3, L"Speed", 94); Column(queue, 4, L"ETA", 60); Column(queue, 5, L"Status", 140);
    auto history = Make(HistoryList, WC_LISTVIEWW, L"Download history", WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(history, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    Column(history, 0, L"Video", 350); Column(history, 1, L"Format", 85); Column(history, 2, L"Completed", 150); Column(history, 3, L"File", 390);
    Make(CurrentTitle, L"STATIC", L"Ready to download", SS_LEFT | SS_ENDELLIPSIS);
    Make(CurrentProgress, PROGRESS_CLASSW, L"", PBS_SMOOTH);
    if (!SetWindowSubclass(Control(CurrentProgress), ProgressProc, 1, reinterpret_cast<DWORD_PTR>(this)))
        throw std::runtime_error("Cannot initialize the progress control.");
    SendMessageW(Control(CurrentProgress), PBM_SETRANGE32, 0, 1000);
    Make(CurrentPercent, L"STATIC", L"0%", SS_RIGHT);
    Make(CurrentMetrics, L"STATIC", L"Choose videos above, then Download selected.", SS_LEFT | SS_ENDELLIPSIS);
    Make(Pause, L"BUTTON", L"Pause", WS_TABSTOP);
    Make(Resume, L"BUTTON", L"Resume", WS_TABSTOP);
    Make(Cancel, L"BUTTON", L"Cancel", WS_TABSTOP);
    Make(CancelAll, L"BUTTON", L"Cancel all", WS_TABSTOP);
    Make(Status, STATUSCLASSNAMEW, L"", SBARS_TOOLTIPS);
    // Child windows are inserted below existing siblings. A group box occupies
    // its entire rectangle for sibling clipping, so its frame must be behind
    // the controls inside it, not merely created before them.
    for (auto id : {SourceGroup, PlaylistGroup, DownloadsGroup, CurrentGroup}) {
        if (!SetWindowSubclass(Control(id), FrameBackground, 1, 0))
            throw std::runtime_error("Cannot initialize a native frame background.");
        if (!SetWindowPos(Control(id), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
            throw std::runtime_error("Cannot place a native frame behind its controls.");
    }
    for (auto id : {Videos, Queue, HistoryList}) SetWindowTheme(Control(id), L"Explorer", nullptr);
    Font(); Layout(); RefreshQueue(); RefreshHistory(); RefreshSummary(); RefreshButtons();
    StatusText(L"Paste a YouTube URL and choose Analyze.");
    if (backgroundEnabled_) {
        thumbnails_ = std::make_unique<Thumbnails>([this](std::string id, std::shared_ptr<Bitmap> bitmap) {
            Event e{EventType::Thumb}; e.id = std::move(id); e.bitmap = std::move(bitmap); Post(std::move(e));
        });
        if (!SetTimer(window_, 1, 500, nullptr)) throw std::runtime_error("Cannot start the application refresh timer.");
    }
}
HFONT App::MakeUiFont(UINT dpi, int weight) {
    return CreateFontW(-MulDiv(TG_UI_FONT_POINTS, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, TG_WIDEN(TG_UI_FONT_FAMILY));
}
void App::StyleTextField(HWND field, UINT dpi) {
    // Both resource-created and code-created edits use the same native border
    // and text margins. Resource EDITTEXT's implicit WS_BORDER is removed.
    SetWindowLongPtrW(field, GWL_STYLE, GetWindowLongPtrW(field, GWL_STYLE) & ~static_cast<LONG_PTR>(WS_BORDER));
    SetWindowLongPtrW(field, GWL_EXSTYLE, (GetWindowLongPtrW(field, GWL_EXSTYLE) & ~static_cast<LONG_PTR>(WS_EX_STATICEDGE)) | WS_EX_CLIENTEDGE);
    int margin = MulDiv(6, dpi, 96);
    SendMessageW(field, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(margin, margin));
    SetWindowPos(field, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}
void App::SizeComboField(HWND combo, UINT dpi) {
    const int target = MulDiv(FieldHeight, dpi, 96);
    SendMessageW(combo, CB_SETITEMHEIGHT, 0, target);
    // Native combo selection borders vary with the Windows theme. Measure the
    // actual closed control instead of assuming a fixed border thickness.
    for (int attempt = 0; attempt < 2; ++attempt) {
        RECT bounds{}; GetWindowRect(combo, &bounds);
        int difference = target - (bounds.bottom - bounds.top);
        if (!difference) break;
        int selected = static_cast<int>(SendMessageW(combo, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0));
        SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), std::max(1, selected + difference));
    }
    int count = std::max(1, static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0)));
    // Apply this last: a subsequent SetWindowPos would reset the closed height
    // from the font. The native minimum-visible setting sizes the popup alone.
    SendMessageW(combo, CB_SETMINVISIBLE, count, 0);
}
void App::Font() {
    auto normal = MakeUiFont(dpi_), heading = MakeUiFont(dpi_, FW_SEMIBOLD);
    if (!normal || !heading) {
        if (normal) DeleteObject(normal); if (heading) DeleteObject(heading);
        throw std::runtime_error("Cannot create the application font.");
    }
    auto old = font_, oldTitle = titleFont_; font_ = normal; titleFont_ = heading;
    for (auto [id, c] : controls_) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(id == Heading || id == CurrentTitle ? titleFont_ : font_), TRUE);
    if (old) DeleteObject(old); if (oldTitle) DeleteObject(oldTitle);
    StyleTextField(Control(Url), dpi_);
    // A one-pixel-wide native image list sets report-row height without changing
    // ListView keyboard navigation, selection, accessible text, or column behavior.
    auto oldImages = queueImages_;
    queueImages_ = ImageList_Create(1, Scale(32), ILC_COLOR32, 1, 1);
    ImageList_SetImageCount(queueImages_, 1);
    ListView_SetImageList(Control(Queue), queueImages_, LVSIL_SMALL);
    ListView_SetImageList(Control(HistoryList), queueImages_, LVSIL_SMALL);
    if (oldImages) ImageList_Destroy(oldImages);
}
void App::Layout() {
    if (controls_.empty()) return;
    SendMessageW(Control(Status), WM_SIZE, 0, 0);
    RECT client{}, status{}; GetClientRect(window_, &client); GetWindowRect(Control(Status), &status);
    int width = MulDiv(client.right, 96, dpi_);
    int height = MulDiv(client.bottom - (status.bottom - status.top), 96, dpi_);
    constexpr int margin = 12, gap = 10, sourceHeight = 64, currentHeight = 76;
    int full = width - margin * 2;
    int mainHeight = height - margin * 2 - gap * 2 - sourceHeight - currentHeight;
    int available = full - gap;
    int playlistWidth = std::clamp(available * state_.settings.playlistPercent / 100, 400, std::max(400, available - 420));
    int downloadsWidth = available - playlistWidth;
    int downloadsX = margin + playlistWidth + gap, right = width - margin;
    HDWP deferred = BeginDeferWindowPos(static_cast<int>(controls_.size()));
    if (!deferred) throw std::runtime_error("Cannot begin control layout.");
    auto move = [&](Id id, int x, int y, int w, int h) {
        deferred = DeferWindowPos(deferred, Control(id), nullptr, Scale(x), Scale(y), Scale(std::max(w, 1)), Scale(std::max(h, 1)), SWP_NOZORDER | SWP_NOACTIVATE);
        if (!deferred) throw std::runtime_error("Cannot arrange a Windows control.");
    };
    int y = margin;
    move(SourceGroup, margin, y, full, sourceHeight);
    move(UrlLabel, margin + 12, y + 27, 92, LabelHeight);
    move(Url, margin + 108, y + 23, full - 330, FieldHeight);
    move(Paste, right - 210, y + 23, 76, FieldHeight); move(Analyze, right - 124, y + 23, 112, FieldHeight);
    y += sourceHeight + gap;
    move(PlaylistGroup, margin, y, playlistWidth, mainHeight);
    move(Heading, margin + 12, y + 24, playlistWidth - 24, LabelHeight);
    move(Summary, margin + 12, y + 56, playlistWidth - 236, LabelHeight);
    move(SelectAll, margin + playlistWidth - 220, y + 52, 96, FieldHeight);
    move(SelectNone, margin + playlistWidth - 116, y + 52, 104, FieldHeight);
    move(Videos, margin + 12, y + 88, playlistWidth - 24, mainHeight - 178);
    move(OutputSummary, margin + 12, y + mainHeight - 77, playlistWidth - 24, LabelHeight);
    move(Options, margin + 12, y + mainHeight - 44, 154, ActionHeight);
    move(Add, margin + playlistWidth - 216, y + mainHeight - 44, 204, ActionHeight);
    move(Splitter, margin + playlistWidth, y + 10, gap, mainHeight - 10);
    move(DownloadsGroup, downloadsX, y, downloadsWidth, mainHeight);
    move(Tabs, downloadsX + 12, y + 24, 254, 31);
    move(OpenFile, right - 96, y + 25, 84, FieldHeight);
    move(Queue, downloadsX + 12, y + 64, downloadsWidth - 24, mainHeight - 120);
    move(HistoryList, downloadsX + 12, y + 64, downloadsWidth - 24, mainHeight - 120);
    int buttonY = y + mainHeight - 44;
    move(Pause, downloadsX + 12, buttonY, 68, ActionHeight); move(Resume, downloadsX + 12, buttonY, 68, ActionHeight);
    move(Cancel, downloadsX + 88, buttonY, 68, ActionHeight);
    move(CancelAll, downloadsX + 164, buttonY, 84, ActionHeight);
    move(Retry, right - 140, buttonY, 60, ActionHeight);
    move(ClearFinished, right - 72, buttonY, 60, ActionHeight);
    move(Forget, right - 148, buttonY, 136, ActionHeight);
    y += mainHeight + gap;
    move(CurrentGroup, margin, y, full, currentHeight);
    move(CurrentTitle, margin + 14, y + 21, full - 28, LabelHeight);
    int progressWidth = full * 42 / 100;
    move(CurrentProgress, margin + 14, y + 52, progressWidth, 8);
    move(CurrentPercent, margin + 22 + progressWidth, y + 46, 42, LabelHeight);
    move(CurrentMetrics, margin + 78 + progressWidth, y + 46, full - progressWidth - 92, LabelHeight);
    if (!EndDeferWindowPos(deferred)) throw std::runtime_error("Cannot finish control layout.");
    TabCtrl_SetItemSize(Control(Tabs), Scale(121), Scale(25));
    FitListColumns();
    int parts[] = {client.right - Scale(186), -1}; SendMessageW(Control(Status), SB_SETPARTS, 2, reinterpret_cast<LPARAM>(parts));
    InvalidateRect(window_, nullptr, TRUE);
}
void App::FitListColumns() {
    RECT videoClient{}, queueClient{}, historyClient{};
    GetClientRect(Control(Videos), &videoClient); GetClientRect(Control(Queue), &queueClient); GetClientRect(Control(HistoryList), &historyClient);
    // Measure the real client width, including the current native scrollbar.
    // This also remains correct when a monitor's scrollbar metrics differ.
    int videoWidth = videoClient.right - 2;
    ListView_SetColumnWidth(Control(Videos), 0, std::max(Scale(180), videoWidth - Scale(176)));
    ListView_SetColumnWidth(Control(Videos), 1, Scale(72)); ListView_SetColumnWidth(Control(Videos), 2, Scale(104));
    int queueWidth = queueClient.right - 2;
    int logicalWidth = MulDiv(queueWidth, 96, dpi_);
    // Keep titles readable on small/high-DPI displays. The active transfer strip
    // still shows speed, and job details retain format when those columns collapse.
    int widths[] = {0, logicalWidth < 580 ? 0 : 88, 100, logicalWidth < 470 ? 0 : 70, 56, 100}, used = 0;
    for (int column = 1; column < 6; ++column) { used += Scale(widths[column]); ListView_SetColumnWidth(Control(Queue), column, Scale(widths[column])); }
    ListView_SetColumnWidth(Control(Queue), 0, std::max(Scale(112), queueWidth - used));
    int historyWidth = historyClient.right - 2, historyTitle = historyWidth * 40 / 100;
    ListView_SetColumnWidth(Control(HistoryList), 0, historyTitle); ListView_SetColumnWidth(Control(HistoryList), 1, Scale(48));
    ListView_SetColumnWidth(Control(HistoryList), 2, Scale(124));
    ListView_SetColumnWidth(Control(HistoryList), 3, std::max(Scale(50), historyWidth - historyTitle - Scale(172)));

}
void App::RefreshButtons() {
    bool history = TabCtrl_GetCurSel(Control(Tabs)) == 1;
    ShowWindow(Control(Queue), history ? SW_HIDE : SW_SHOW); ShowWindow(Control(HistoryList), history ? SW_SHOW : SW_HIDE);
    for (auto id : {Retry, ClearFinished}) ShowWindow(Control(id), history ? SW_HIDE : SW_SHOW);
    ShowWindow(Control(Forget), history ? SW_SHOW : SW_HIDE);
    ShowWindow(Control(Pause), paused_ ? SW_HIDE : SW_SHOW); ShowWindow(Control(Resume), paused_ ? SW_SHOW : SW_HIDE);
    bool pending = std::any_of(state_.queue.begin(), state_.queue.end(), [](const Job& q) { return q.state == JobState::Pending; });
    bool recoverable = std::any_of(state_.queue.begin(), state_.queue.end(), [](const Job& q) { return q.state == JobState::Paused || q.state == JobState::Pending; });
    size_t selectedCount = selectedVideoCount_;
    auto chosen = history ? std::vector<int>{} : Selected(Control(Queue));
    bool retry = false, cancel = false, show = false;
    for (int i : chosen) {
        if (i >= static_cast<int>(state_.queue.size())) continue;
        auto s = state_.queue[i].state;
        retry |= s == JobState::Failed || s == JobState::Cancelled;
        cancel |= s == JobState::Pending || s == JobState::Downloading || s == JobState::Processing || s == JobState::Paused;
        show |= !state_.queue[i].file.empty();
    }
    if (chosen.empty()) cancel = active_ >= 0;
    bool historySelection = !Selected(Control(HistoryList)).empty();
    if (history) show = historySelection;
    auto enable = [&](Id id, bool value) {
        if (Control(id)) EnableWindow(Control(id), value && !closing_);
        EnableMenuItem(menu_, id, MF_BYCOMMAND | (value && !closing_ ? MF_ENABLED : MF_GRAYED));
    };
    enable(Analyze, !analysisBusy_ && !updating_); enable(StopAnalysis, analysisBusy_ && !updating_);
    // One button in the source frame becomes Stop while analysis is in progress.
    SetWindowTextW(Control(Analyze), analysisBusy_ ? L"Stop analysis" : L"&Analyze");
    EnableWindow(Control(Analyze), !closing_ && !updating_);
    enable(Add, selectedCount > 0 && !analysisBusy_ && !updating_);
    enable(SelectAll, !videos_.empty()); enable(SelectNone, selectedCount > 0);
    enable(Update, !analysisBusy_ && active_ < 0 && !updating_); enable(Tools, !analysisBusy_ && active_ < 0 && !updating_);
    enable(Pause, !paused_ && (active_ >= 0 || pending)); enable(Resume, paused_ && active_ < 0 && recoverable && !updating_);
    enable(Cancel, cancel); enable(Retry, retry && !updating_); enable(OpenFile, show);
    enable(CancelAll, recoverable || (active_ >= 0 && downloadStop_ != Stop::Cancel));
    enable(ClearFinished, active_ < 0 && std::any_of(state_.queue.begin(), state_.queue.end(), [](const Job& q) { return q.state == JobState::Completed || q.state == JobState::Cancelled; }));
    enable(Forget, history && historySelection);
    enable(Options, !updating_);
    auto queueCaption = L"Queue (" + std::to_wstring(state_.queue.size()) + L")";
    auto historyCaption = L"History (" + std::to_wstring(state_.history.size()) + L")";
    TCITEMW tab{}; tab.mask = TCIF_TEXT; tab.pszText = queueCaption.data(); TabCtrl_SetItem(Control(Tabs), 0, &tab);
    tab.pszText = historyCaption.data(); TabCtrl_SetItem(Control(Tabs), 1, &tab);
    std::wstring engineText = updating_ ? L"Updating yt-dlp…" : fs::is_regular_file(fs::path(state_.settings.tools) / L"yt-dlp.exe") ? L"yt-dlp installed" : L"Tools not installed";
    SendMessageW(Control(Status), SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(engineText.c_str()));
    SendMessageW(Control(Status), SB_SETTIPTEXTW, 1, reinterpret_cast<LPARAM>(state_.settings.tools.c_str()));
    RefreshDownloadStrip();
}
void App::RefreshSummary() {
    summaryDirty_ = false;
    size_t count = 0; for (int i = 0; i < static_cast<int>(videos_.size()); ++i) if (ListView_GetCheckState(Control(Videos), i)) ++count;
    selectedVideoCount_ = count;
    auto heading = videos_.empty() ? (analysisBusy_ ? L"Analyzing…" : L"No playlist analyzed") : videos_[0].playlist.empty() ? L"Video" : videos_[0].playlist;
    auto summary = videos_.empty() ? L"Paste a URL above to choose videos." : std::to_wstring(videos_.size()) + L" videos  ·  " + std::to_wstring(count) + L" selected";
    SetWindowTextW(Control(Heading), heading.c_str()); SetWindowTextW(Control(Summary), summary.c_str());
    auto button = L"&Download selected (" + std::to_wstring(count) + L")"; SetWindowTextW(Control(Add), button.c_str());
    auto output = state_.settings.format == "mp3" ? std::wstring(L"MP3 audio") : std::wstring(L"MP4  ·  ") + (state_.settings.quality ? std::to_wstring(state_.settings.quality) + L"p" : L"Best");
    output += L"  ·  " + state_.settings.destination;
    SetWindowTextW(Control(OutputSummary), output.c_str());
    RefreshButtons();
}
std::wstring App::VideoStatus(const Video& video) const {
    if (!video.available) return L"Unavailable / live";
    const auto& format = state_.settings.format;
    auto key = video.id + ":" + format;
    auto found = videoStatuses_.find(key);
    return found == videoStatuses_.end() ? L"Ready" : found->second;
}
void App::RefreshVideoStatuses() {
    videoStatuses_.clear();
    for (const auto& h : state_.history) videoStatuses_[h.key()] = L"Downloaded";
    // The newest queue entry wins after an explicit re-download. Index once so
    // a large playlist/queue cannot turn each refresh into a quadratic scan.
    for (const auto& q : state_.queue) videoStatuses_[q.key()] = q.state == JobState::Completed ? L"Downloaded" : CompactState(q.state);
    for (int i = 0; i < static_cast<int>(videos_.size()); ++i) Cell(Control(Videos), i, 2, VideoStatus(videos_[i]));
}
int App::CurrentJob() const {
    if (active_ >= 0 && active_ < static_cast<int>(state_.queue.size())) return active_;
    if (paused_) for (int i = 0; i < static_cast<int>(state_.queue.size()); ++i) if (state_.queue[i].state == JobState::Paused) return i;
    if (!lastToken_.empty()) for (int i = 0; i < static_cast<int>(state_.queue.size()); ++i) if (state_.queue[i].token == lastToken_) return i;
    return -1;
}
void App::RefreshDownloadStrip() {
    int index = CurrentJob();
    size_t queued = 0, paused = 0;
    for (const auto& q : state_.queue) { queued += q.state == JobState::Pending; paused += q.state == JobState::Paused; }
    std::wstring title = L"Ready to download", percent = L"0%", metrics = L"Choose videos above, then Download selected.";
    int position = 0;
    if (index >= 0) {
        const auto& q = state_.queue[index]; title = q.video.title;
        position = static_cast<int>(std::clamp(q.percent, 0.0, 100.0) * 10); percent = std::to_wstring(position / 10) + L"%";
        if (q.state == JobState::Downloading) metrics = SpeedLabel(q.speed) + L"  ·  " + (q.eta >= 0 ? Duration(q.eta) + L" remaining" : L"Estimating time…") + L"  ·  " + std::to_wstring(queued) + L" queued";
        else if (q.state == JobState::Processing) metrics = L"Processing media…  ·  " + JobFormatLabel(q) + L"  ·  " + std::to_wstring(queued) + L" queued";
        else if (q.state == JobState::Paused) metrics = L"Paused  ·  partial files saved  ·  " + std::to_wstring(paused) + L" paused";
        else if (q.state == JobState::Completed) metrics = L"Completed  ·  " + q.file;
        else if (q.state == JobState::Failed) metrics = q.detail.empty() ? L"Failed — select the row for details, then retry." : q.detail;
        else metrics = CompactState(q.state) + L"  ·  " + std::to_wstring(queued) + L" queued";
    }
    SetWindowTextW(Control(CurrentTitle), title.c_str()); SetWindowTextW(Control(CurrentPercent), percent.c_str());
    SetWindowTextW(Control(CurrentMetrics), metrics.c_str());
    SendMessageW(Control(CurrentProgress), PBM_SETPOS, position, 0);
    SendMessageW(Control(CurrentProgress), PBM_SETSTATE, index >= 0 && state_.queue[index].state == JobState::Failed ? PBST_ERROR : index >= 0 && state_.queue[index].state == JobState::Paused ? PBST_PAUSED : PBST_NORMAL, 0);
}
void App::StatusText(const std::wstring& text) {
    if (!Control(Status)) return;
    SendMessageW(Control(Status), SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(Control(Status), SB_SETTIPTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
}
LRESULT App::CustomDraw(NMLVCUSTOMDRAW* d) {
    if (d->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (d->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) return CDRF_NOTIFYSUBITEMDRAW;
    if (d->nmcd.dwDrawStage != (CDDS_ITEMPREPAINT | CDDS_SUBITEM) || d->iSubItem != 2) return CDRF_DODEFAULT;
    auto row = static_cast<size_t>(d->nmcd.dwItemSpec); if (row >= state_.queue.size()) return CDRF_DODEFAULT;
    RECT cell{}; ListView_GetSubItemRect(Control(Queue), static_cast<int>(row), 2, LVIR_BOUNDS, &cell);
    bool selected = (ListView_GetItemState(Control(Queue), static_cast<int>(row), LVIS_SELECTED) & LVIS_SELECTED) != 0;
    bool focused = GetFocus() == Control(Queue);
    FillRect(d->nmcd.hdc, &cell, GetSysColorBrush(selected ? focused ? COLOR_HIGHLIGHT : COLOR_3DFACE : COLOR_WINDOW));
    RECT bar = cell; bar.right -= Scale(44); InflateRect(&bar, -Scale(6), -Scale(6));
    auto theme = OpenThemeData(window_, L"PROGRESS");
    if (theme) {
        DrawThemeBackground(theme, d->nmcd.hdc, PP_BAR, 0, &bar, nullptr);
        RECT fill = bar; InflateRect(&fill, -1, -1);
        fill.right = fill.left + static_cast<LONG>((fill.right - fill.left) * std::clamp(state_.queue[row].percent, 0.0, 100.0) / 100);
        if (fill.right > fill.left) FillRect(d->nmcd.hdc, &fill, GetSysColorBrush(COLOR_HIGHLIGHT));
        CloseThemeData(theme);
    } else {
        FillRect(d->nmcd.hdc, &bar, GetSysColorBrush(COLOR_3DFACE));
        RECT fill = bar; fill.right = fill.left + static_cast<LONG>((fill.right - fill.left) * std::clamp(state_.queue[row].percent, 0.0, 100.0) / 100);
        FillRect(d->nmcd.hdc, &fill, GetSysColorBrush(COLOR_HIGHLIGHT));
    }
    RECT label = cell; label.left = bar.right + Scale(6); label.right -= Scale(4);
    auto text = std::to_wstring(static_cast<int>(state_.queue[row].percent)) + L"%";
    SetBkMode(d->nmcd.hdc, TRANSPARENT); SetTextColor(d->nmcd.hdc, GetSysColor(selected && focused ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    DrawTextW(d->nmcd.hdc, text.c_str(), -1, &label, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    return CDRF_SKIPDEFAULT;
}
void App::PopupQueueMenu(POINT point) {
    bool history = TabCtrl_GetCurSel(Control(Tabs)) == 1;
    if (point.x == -1 && point.y == -1) {
        auto list = Control(history ? HistoryList : Queue); RECT r{}; GetWindowRect(list, &r); point = {r.left + Scale(20), r.top + Scale(35)};
    }
    auto popup = CreatePopupMenu();
    if (history) AppendMenuW(popup, MF_STRING | (IsWindowEnabled(Control(Forget)) ? MF_ENABLED : MF_GRAYED), Forget, L"Forget selected history");
    else {
        AppendMenuW(popup, MF_STRING | (IsWindowEnabled(Control(Retry)) ? MF_ENABLED : MF_GRAYED), Retry, L"Retry selected");
        AppendMenuW(popup, MF_STRING | (IsWindowEnabled(Control(Cancel)) ? MF_ENABLED : MF_GRAYED), Cancel, L"Cancel selected");
    }
    AppendMenuW(popup, MF_STRING | (IsWindowEnabled(Control(OpenFile)) ? MF_ENABLED : MF_GRAYED), OpenFile, L"Show file");
    int chosen = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window_, nullptr);
    DestroyMenu(popup); if (chosen) Command(chosen);
}
}
