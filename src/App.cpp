#include "App.h"
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <sstream>
#include <iomanip>
#include <set>

namespace cf {
constexpr UINT WM_EVENTS = WM_APP + 1;
App::App() : data_(DataDirectory()), statePath_(data_ / L"state.json"), state_(LoadState(statePath_, notice_)) {
    for (const auto& q : state_.queue) if (q.state == JobState::Paused) notice_ = L"An interrupted queue was recovered. Click Resume queue to continue saved partial downloads.";
}
App::App(State state, fs::path data) : data_(std::move(data)), statePath_(data_ / L"state.json"), state_(std::move(state)), backgroundEnabled_(false) {}
App::~App() {
    acceptingEvents_ = false; mailboxSpace_.notify_all();
    analysisStop_ = Stop::Shutdown; downloadStop_ = Stop::Shutdown;
    if (analysisThread_.joinable()) analysisThread_.join();
    if (downloadThread_.joinable()) downloadThread_.join();
    thumbnails_.reset();
    if (images_) ImageList_Destroy(images_);
    if (queueImages_) ImageList_Destroy(queueImages_);
    if (font_) DeleteObject(font_);
    if (titleFont_) DeleteObject(titleFont_);
}
int App::Run(HINSTANCE instance, int show) {
    instance_ = instance;
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = WindowProc; wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_TUBEGRAB));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_TUBEGRAB), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); wc.lpszClassName = L"TubeGrabWindow";
    if (!RegisterClassExW(&wc)) throw std::runtime_error("Cannot register the application window.");
    dpi_ = GetDpiForSystem();
    RECT bounds{0, 0, Scale(1360), Scale(860)};
    AdjustWindowRectExForDpi(&bounds, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, TRUE, WS_EX_CONTROLPARENT, dpi_);
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"TubeGrab", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, std::min(bounds.right - bounds.left, work.right - work.left),
        std::min(bounds.bottom - bounds.top, work.bottom - work.top), nullptr, nullptr, instance, this);
    if (!window_) throw std::runtime_error(notice_.empty() ? "Cannot create the application window." : Utf8(notice_));
    ShowWindow(window_, show); UpdateWindow(window_);
    if (!notice_.empty()) StatusText(notice_);
    SetFocus(Control(Url));
    MSG message{};
    BOOL received = 0;
    while ((received = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN && message.hwnd == Control(Url)) { Command(Analyze); continue; }
        if (!IsDialogMessageW(window_, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (received == -1) throw std::runtime_error("The Windows message loop failed.");
    return static_cast<int>(message.wParam);
}
LRESULT CALLBACK App::WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        self->window_ = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, w, l);
    try { return self->HandleMessage(message, w, l); }
    catch (const std::exception& e) {
        if (message == WM_CREATE) { self->notice_ = Wide(e.what()); return -1; }
        self->Error(Wide(e.what())); return 0;
    }
}
HWND App::Make(Id id, const wchar_t* type, const wchar_t* text, DWORD style, DWORD ex) {
    HWND c = CreateWindowExW(ex, type, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, 0, 0, 10, 10, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    if (!c) throw std::runtime_error("Cannot create a Windows control.");
    controls_[id] = c; return c;
}
void App::Column(HWND list, int index, const wchar_t* title, int width) {
    LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.pszText = const_cast<wchar_t*>(title); column.cx = Scale(width);
    ListView_InsertColumn(list, index, &column);
}
void App::ResetImages() {
    auto old = images_;
    const int width = Scale(ThumbWidth + ThumbPaddingX * 2), height = Scale(ThumbHeight + ThumbPaddingY * 2);
    images_ = ImageList_Create(width, height, ILC_COLOR32, 1, 16);
    if (!images_) { images_ = old; throw std::runtime_error("Cannot allocate thumbnail image list."); }
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; void* pixels = nullptr;
    HBITMAP placeholder = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (placeholder) {
        auto bits = static_cast<DWORD*>(pixels); std::fill_n(bits, width * height, 0);
        for (int y = Scale(ThumbPaddingY); y < Scale(ThumbPaddingY) + Scale(ThumbHeight); ++y)
            std::fill_n(bits + y * width + Scale(ThumbPaddingX), Scale(ThumbWidth), 0xffeeeeee);
        ImageList_Add(images_, placeholder, nullptr); DeleteObject(placeholder);
    }
    ListView_SetImageList(Control(Videos), images_, LVSIL_SMALL);
    if (old) ImageList_Destroy(old);
    slotOwners_.assign(1, ""); thumbnailSlots_.clear(); thumbnailRequested_.clear(); nextSlot_ = 1;
    for (int i = 0; i < static_cast<int>(videos_.size()); ++i) {
        LVITEMW item{}; item.mask = LVIF_IMAGE; item.iItem = i; item.iImage = 0; ListView_SetItem(Control(Videos), &item);
    }
}
void App::Cell(HWND list, int row, int column, const std::wstring& value) { ListView_SetItemText(list, row, column, const_cast<wchar_t*>(value.c_str())); }
void App::AddRow(HWND list, int index, const std::wstring& title, int image) {
    LVITEMW item{}; item.mask = LVIF_TEXT | LVIF_IMAGE; item.iItem = index; item.pszText = const_cast<wchar_t*>(title.c_str()); item.iImage = image;
    ListView_InsertItem(list, &item);
}
std::vector<int> App::Selected(HWND list) const {
    std::vector<int> result;
    for (int i = -1; (i = ListView_GetNextItem(list, i, LVNI_SELECTED)) >= 0;) result.push_back(i);
    return result;
}
void App::RefreshQueue() {
    auto list = Control(Queue); auto selection = Selected(list); int top = ListView_GetTopIndex(list);
    rebuilding_ = true; SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    for (int i = 0; i < static_cast<int>(state_.queue.size()); ++i) {
        const auto& q = state_.queue[i]; AddRow(list, i, q.video.title);
        Cell(list, i, 1, q.format == "mp3" ? L"MP3 audio" : (q.quality ? std::to_wstring(q.quality) + L"p MP4" : L"Best MP4"));
        Cell(list, i, 2, std::to_wstring(static_cast<int>(q.percent)) + L"%");
        std::wostringstream speed; if (q.speed > 0) speed << std::fixed << std::setprecision(1) << q.speed / 1048576 << L" MB/s";
        Cell(list, i, 3, speed.str()); Cell(list, i, 4, q.eta >= 0 ? Duration(q.eta) : L"—");
        Cell(list, i, 5, q.state == JobState::Paused ? L"Paused" : q.state == JobState::Failed ? L"Failed" : StateName(q.state));
    }
    for (int i : selection) if (i < static_cast<int>(state_.queue.size())) ListView_SetItemState(list, i, LVIS_SELECTED, LVIS_SELECTED);
    if (top >= 0 && top < static_cast<int>(state_.queue.size())) ListView_EnsureVisible(list, top, FALSE);
    SendMessageW(list, WM_SETREDRAW, TRUE, 0); InvalidateRect(list, nullptr, TRUE); rebuilding_ = false;
    RefreshVideoStatuses();
    FitListColumns();
}
void App::RefreshHistory() {
    auto list = Control(HistoryList); SendMessageW(list, WM_SETREDRAW, FALSE, 0); ListView_DeleteAllItems(list);
    for (int i = 0; i < static_cast<int>(state_.history.size()); ++i) {
        const auto& h = state_.history[i]; AddRow(list, i, h.title); Cell(list, i, 1, h.format == "mp3" ? L"MP3" : L"MP4");
        Cell(list, i, 2, h.date); Cell(list, i, 3, h.file);
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0); InvalidateRect(list, nullptr, TRUE);
    FitListColumns();
}
bool App::Persist() {
    try { SaveState(statePath_, state_); storageFailed_ = false; return true; }
    catch (const std::exception& e) {
        storageFailed_ = true; paused_ = true;
        if (active_ >= 0 && downloadStop_ == Stop::None) downloadStop_ = Stop::Pause;
        for (auto& job : state_.queue) if (job.state == JobState::Pending) job.state = JobState::Paused;
        StatusText(L"Queue paused: " + Wide(e.what()) + L" Free disk space or restore folder access, then Resume."); return false;
    }
}
void App::Error(const std::wstring& text) { StatusText(text); MessageBoxW(window_, text.c_str(), L"TubeGrab", MB_OK | MB_ICONWARNING); }
void App::Post(Event event) {
    std::unique_lock<std::mutex> lock(mailboxMutex_);
    if (!acceptingEvents_) return;
    // Progress can be replaced by its newest sample. Metadata is never dropped;
    // back pressure bounds memory when a large playlist arrives faster than painting.
    if (event.type == EventType::Progress && !mailbox_.empty() && mailbox_.back().type == EventType::Progress) {
        mailbox_.back() = std::move(event); return;
    }
    if (event.type == EventType::Log && mailbox_.size() >= 2048) return;
    mailboxSpace_.wait(lock, [this] { return mailbox_.size() < 2048 || !acceptingEvents_; });
    if (!acceptingEvents_) return;
    bool empty = mailbox_.empty(); mailbox_.push_back(std::move(event));
    if (empty) PostMessageW(window_, WM_EVENTS, 0, 0);
}
void App::AppendLog(const std::wstring& text) {
    log_ += Now() + L"  " + text + L"\r\n";
    if (log_.size() > 512 * 1024) log_.erase(0, log_.size() - 384 * 1024);
}
void App::AnalyzeUrl() {
    if (analysisBusy_ || updating_) return;
    auto url = WindowText(Control(Url));
    while (!url.empty() && iswspace(url.front())) url.erase(url.begin());
    while (!url.empty() && iswspace(url.back())) url.pop_back();
    if (!IsYoutubeUrl(url)) { Error(L"Paste an HTTPS YouTube video or playlist URL (youtube.com or youtu.be)."); return; }
    Engine engine(state_.settings.tools); engine.Check(false);
    if (analysisThread_.joinable()) analysisThread_.join();
    videos_.clear(); videoIds_.clear(); ListView_DeleteAllItems(Control(Videos));
    analysisStop_ = Stop::None; analysisBusy_ = true;
    RefreshSummary();
    SetWindowTextW(Control(Summary), L"Analyzing… videos appear as they are found."); StatusText(L"Reading video information. You can stop analysis and keep the results already found."); RefreshButtons();
    try { analysisThread_ = std::thread([this, engine, url] {
        Event done{EventType::AnalysisDone};
        try {
            auto r = engine.Analyze(url, analysisStop_, [this](Video v) { Event e{EventType::Video}; e.video = std::move(v); Post(std::move(e)); },
                [this](const std::string& line) { Event e{EventType::Log}; e.text = Wide(line); Post(std::move(e)); });
            done.ok = r.exitCode == 0 && !r.stopped;
            done.text = r.stopped ? L"Analysis stopped. You can download the entries already found." : r.exitCode ? L"Analysis could not finish. Available results were retained; open the log for details." : L"Analysis complete. Choose videos, format and destination, then Download selected.";
        } catch (const std::exception& e) { done.text = Wide(e.what()); }
        Post(std::move(done));
    }); } catch (...) { analysisBusy_ = false; RefreshSummary(); throw; }
}
void App::AddSelected() {
    if (analysisBusy_ || updating_) return;
    Engine(state_.settings.tools).Check(true);
    fs::path destination(state_.settings.destination);
    if (!destination.is_absolute()) { Error(L"Choose a full destination path, such as C:\\Users\\YourName\\Downloads."); return; }
    std::set<std::string> known;
    if (state_.settings.skipDownloaded) for (const auto& h : state_.history) known.insert(h.key());
    for (const auto& q : state_.queue) if (state_.settings.skipDownloaded || q.state != JobState::Completed) known.insert(q.key());
    size_t added = 0, skipped = 0;
    for (int i = 0; i < static_cast<int>(videos_.size()); ++i) {
        if (!ListView_GetCheckState(Control(Videos), i) || !videos_[i].available) continue;
        const auto& video = videos_[i]; std::string key = video.id + ":" + state_.settings.format;
        if (!known.insert(key).second) { ++skipped; continue; }
        Job q; q.video = video; q.format = state_.settings.format; q.quality = state_.settings.quality;
        auto folder = destination;
        if (state_.settings.playlistFolder && !video.playlist.empty()) folder /= SafeName(video.playlist, 70) + (video.playlistId.empty() ? L"" : L" [" + SafeName(video.playlistId, 40) + L"]");
        q.destination = folder.wstring(); q.outputTemplate = OutputTemplate(video);
        GUID guid{}; CoCreateGuid(&guid); wchar_t token[40]; StringFromGUID2(guid, token, 40); q.token = token;
        state_.queue.push_back(std::move(q)); ++added;
    }
    RefreshQueue(); TabCtrl_SetCurSel(Control(Tabs), 0);
    if (!Persist()) { RefreshButtons(); return; }
    StatusText(std::to_wstring(added) + L" added to queue. " + std::to_wstring(skipped) + L" already queued or downloaded. Use Retry for failed items; uncheck Skip downloaded videos to download again.");
    if (added && active_ < 0) paused_ = false;
    RefreshButtons(); StartNext();
}
void App::StartNext() {
    if (paused_ || closing_ || updating_ || active_ >= 0 || storageFailed_) return;
    auto found = std::find_if(state_.queue.begin(), state_.queue.end(), [](const Job& q) { return q.state == JobState::Pending; });
    if (found == state_.queue.end()) {
        // Adding/retrying new work must not strand older paused rows after that work finishes.
        if (std::any_of(state_.queue.begin(), state_.queue.end(), [](const Job& q) { return q.state == JobState::Paused; })) paused_ = true;
        RefreshButtons(); return;
    }
    if (downloadThread_.joinable()) downloadThread_.join();
    active_ = static_cast<int>(found - state_.queue.begin());
    found->state = JobState::Downloading; found->detail.clear(); found->speed = 0; found->eta = -1;
    if (!Persist()) { found->state = JobState::Paused; active_ = -1; RefreshQueue(); RefreshButtons(); return; }
    downloadStop_ = Stop::None; Job job = *found; Engine engine(state_.settings.tools);
    lastToken_ = job.token;
    RefreshQueue(); RefreshButtons();
    try { downloadThread_ = std::thread([this, engine, job] {
        Event done{EventType::Done};
        try { done.result = engine.Download(job, downloadStop_,
            [this](Progress progress) { Event e{EventType::Progress}; e.progress = progress; Post(std::move(e)); },
            [this](const std::string& line) { Event e{EventType::Log}; e.text = Wide(line); Post(std::move(e)); }); }
        catch (const std::exception& e) { done.result.error = Wide(e.what()); }
        Post(std::move(done));
    }); } catch (...) {
        state_.queue[active_].state = JobState::Paused; active_ = -1; paused_ = true;
        Persist(); RefreshQueue(); RefreshButtons(); throw;
    }
}
void App::PauseQueue() {
    paused_ = true; if (active_ >= 0 && downloadStop_ != Stop::Cancel) downloadStop_ = Stop::Pause;
    for (auto& q : state_.queue) if (q.state == JobState::Pending) q.state = JobState::Paused;
    bool saved = Persist(); RefreshQueue(); RefreshButtons();
    if (saved) StatusText(L"Queue paused. Partial files are kept; Resume continues where the source supports it. Media processing may restart.");
}
void App::ResumeQueue() {
    if (active_ >= 0 || updating_) return;
    Engine(state_.settings.tools).Check(true);
    for (auto& q : state_.queue) if (q.state == JobState::Paused) q.state = JobState::Pending;
    paused_ = false; if (!Persist()) return; RefreshQueue(); RefreshButtons(); StartNext();
}
void App::CancelSelected() {
    auto chosen = TabCtrl_GetCurSel(Control(Tabs)) == 1 ? std::vector<int>{} : Selected(Control(Queue));
    if (chosen.empty() && active_ >= 0) chosen.push_back(active_);
    for (int i : chosen) {
        auto& q = state_.queue.at(i);
        if (i == active_) { downloadStop_ = Stop::Cancel; q.state = JobState::Cancelled; q.speed = 0; q.eta = -1; }
        else if (q.state != JobState::Completed) { q.state = JobState::Cancelled; q.speed = 0; q.eta = -1; }
    }
    Persist(); RefreshQueue(); RefreshButtons();
}
void App::CancelAllDownloads() {
    paused_ = true;
    size_t cancelled = 0;
    for (int i = 0; i < static_cast<int>(state_.queue.size()); ++i) {
        auto& q = state_.queue[i];
        if (i == active_) { downloadStop_ = Stop::Cancel; q.state = JobState::Cancelled; q.speed = 0; q.eta = -1; ++cancelled; }
        else if (q.state == JobState::Pending || q.state == JobState::Paused || q.state == JobState::Downloading || q.state == JobState::Processing) {
            q.state = JobState::Cancelled; q.speed = 0; q.eta = -1; ++cancelled;
        }
    }
    bool saved = Persist(); RefreshQueue(); RefreshButtons();
    if (saved) StatusText(std::to_wstring(cancelled) + L" downloads cancelled. Completed files and history were kept. Select cancelled rows and Retry to continue saved partial files.");
}
void App::RetrySelected() {
    if (updating_) return;
    for (int i : Selected(Control(Queue))) {
        auto& q = state_.queue.at(i);
        if (q.state == JobState::Failed || q.state == JobState::Cancelled) { q.state = JobState::Pending; q.detail.clear(); q.percent = 0; }
    }
    if (!Persist()) return; paused_ = false; RefreshQueue(); RefreshButtons(); StartNext();
}
void App::UpdateEngine() {
    if (analysisBusy_ || active_ >= 0 || updating_) return;
    if (analysisThread_.joinable()) analysisThread_.join();
    updating_ = true; analysisStop_ = Stop::None; RefreshButtons(); StatusText(L"Updating yt-dlp from its official release channel…");
    Engine engine(state_.settings.tools);
    try { analysisThread_ = std::thread([this, engine] {
        Event done{EventType::UpdateDone};
        try {
            auto r = engine.Update(analysisStop_, [this](const std::string& line) { Event e{EventType::Log}; e.text = Wide(line); Post(std::move(e)); });
            done.ok = r.exitCode == 0 && !r.stopped;
            done.text = done.ok ? L"yt-dlp is up to date. See the log for its version." : L"Update did not finish. Check that the tools folder is writable and open the log for details.";
        } catch (const std::exception& e) { done.text = Wide(e.what()); }
        Post(std::move(done));
    }); } catch (...) { updating_ = false; RefreshButtons(); throw; }
}
void App::Drain() {
    std::deque<Event> events;
    {
        std::lock_guard<std::mutex> lock(mailboxMutex_);
        size_t count = std::min<size_t>(mailbox_.size(), 128);
        while (count--) { events.push_back(std::move(mailbox_.front())); mailbox_.pop_front(); }
        if (!mailbox_.empty()) PostMessageW(window_, WM_EVENTS, 0, 0);
    }
    mailboxSpace_.notify_all();
    bool queueChanged = false, videosChanged = false;
    for (const auto& e : events) {
        switch (e.type) {
        case EventType::Video: {
            if (!videoIds_.insert(e.video.id).second) break;
            int i = static_cast<int>(videos_.size()); videos_.push_back(e.video);
            auto slot = thumbnailSlots_.find(e.video.id);
            AddRow(Control(Videos), i, e.video.title, slot == thumbnailSlots_.end() ? 0 : slot->second);
            Cell(Control(Videos), i, 1, Duration(e.video.duration)); Cell(Control(Videos), i, 2, VideoStatus(e.video));
            ListView_SetCheckState(Control(Videos), i, e.video.available); videosChanged = true; break;
        }
        case EventType::AnalysisDone:
            if (analysisThread_.joinable()) analysisThread_.join();
            analysisBusy_ = false; StatusText(e.text); AppendLog(e.text); videosChanged = true;
            if (videos_.empty()) SetWindowTextW(Control(Summary), L"No downloadable videos found. Check the URL and log.");
            break;
        case EventType::Progress:
            if (active_ >= 0 && downloadStop_ != Stop::Cancel) {
                auto& q = state_.queue[active_]; q.percent = e.progress.percent; q.speed = e.progress.speed; q.eta = e.progress.eta;
                auto previousState = q.state;
                q.state = e.progress.processing ? JobState::Processing : JobState::Downloading;
                if (q.state != previousState) RefreshVideoStatuses();
                // Only update the active row; keep selection and scroll position stable.
                Cell(Control(Queue), active_, 2, std::to_wstring(static_cast<int>(q.percent)) + L"%");
                std::wostringstream speed; if (q.speed > 0) speed << std::fixed << std::setprecision(1) << q.speed / 1048576 << L" MB/s";
                Cell(Control(Queue), active_, 3, speed.str()); Cell(Control(Queue), active_, 4, Duration(q.eta)); Cell(Control(Queue), active_, 5, StateName(q.state));
                ListView_RedrawItems(Control(Queue), active_, active_);
            } break;
        case EventType::Done:
            if (downloadThread_.joinable()) downloadThread_.join();
            if (active_ >= 0) {
                auto& q = state_.queue[active_]; q.speed = 0; q.eta = -1;
                if (e.result.success) {
                    q.state = JobState::Completed; q.percent = 100; q.file = e.result.file;
                    auto key = q.key();
                    state_.history.erase(std::remove_if(state_.history.begin(), state_.history.end(), [&](const History& h) { return h.key() == key; }), state_.history.end());
                    state_.history.insert(state_.history.begin(), {q.video.id, q.format, q.video.title, q.file, Now(), q.quality});
                    StatusText(L"Saved: " + q.file); RefreshHistory();
                } else if (e.result.stopped || downloadStop_ != Stop::None) q.state = downloadStop_ == Stop::Cancel ? JobState::Cancelled : JobState::Paused;
                else { q.state = JobState::Failed; q.detail = e.result.error; StatusText(q.detail); AppendLog(q.detail); }
                active_ = -1; Persist(); queueChanged = true;
            } break;
        case EventType::Log: AppendLog(e.text); break;
        case EventType::UpdateDone:
            if (analysisThread_.joinable()) analysisThread_.join(); updating_ = false; StatusText(e.text); AppendLog(e.text); break;
        case EventType::Thumb: ShowThumbnail(e); break;
        }
    }
    if (queueChanged) RefreshQueue();
    if (videosChanged) { RefreshSummary(); FitListColumns(); }
    RefreshButtons();
    if (closing_) FinishClose(); else StartNext();
}
void App::RequestThumbnails() {
    if (!thumbnails_ || closing_) return;
    int first = std::max(0, ListView_GetTopIndex(Control(Videos)));
    int last = std::min(static_cast<int>(videos_.size()), first + ListView_GetCountPerPage(Control(Videos)) + 4);
    std::vector<std::string> ids;
    int current = CurrentJob();
    if (current >= 0 && !thumbnailSlots_.count(state_.queue[current].video.id) && !thumbnailRequested_.count(state_.queue[current].video.id)) ids.push_back(state_.queue[current].video.id);
    for (int i = first; i < last; ++i) if (!thumbnailSlots_.count(videos_[i].id) && !thumbnailRequested_.count(videos_[i].id)) ids.push_back(videos_[i].id);
    thumbnails_->Request(ids);
}
void App::ShowThumbnail(const Event& e) {
    thumbnailRequested_.insert(e.id);
    if (!e.bitmap || !e.bitmap->handle || thumbnailSlots_.count(e.id)) return;
    // WIC decodes once at 96 DPI; scale the bounded bitmap on insertion to match this monitor.
    auto scaled = std::make_shared<Bitmap>();
    HDC screen = GetDC(window_), source = CreateCompatibleDC(screen), target = CreateCompatibleDC(screen);
    BITMAPINFO scaledInfo{}; scaledInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    const int width = Scale(ThumbWidth + ThumbPaddingX * 2), height = Scale(ThumbHeight + ThumbPaddingY * 2);
    const int insetX = Scale(ThumbPaddingX), insetY = Scale(ThumbPaddingY);
    scaledInfo.bmiHeader.biWidth = width; scaledInfo.bmiHeader.biHeight = -height;
    scaledInfo.bmiHeader.biPlanes = 1; scaledInfo.bmiHeader.biBitCount = 32; scaledInfo.bmiHeader.biCompression = BI_RGB;
    void* scaledBits = nullptr;
    scaled->handle = CreateDIBSection(screen, &scaledInfo, DIB_RGB_COLORS, &scaledBits, nullptr, 0);
    if (!source || !target || !scaled->handle) { if (source) DeleteDC(source); if (target) DeleteDC(target); ReleaseDC(window_, screen); return; }
    std::fill_n(static_cast<DWORD*>(scaledBits), width * height, 0);
    auto sourceOld = SelectObject(source, e.bitmap->handle), targetOld = SelectObject(target, scaled->handle);
    SetStretchBltMode(target, HALFTONE); SetBrushOrgEx(target, 0, 0, nullptr);
    StretchBlt(target, insetX, insetY, Scale(ThumbWidth), Scale(ThumbHeight), source, 0, 0, 96, 54, SRCCOPY);
    GdiFlush();
    for (int y = insetY; y < insetY + Scale(ThumbHeight); ++y)
        for (int x = insetX; x < insetX + Scale(ThumbWidth); ++x) static_cast<DWORD*>(scaledBits)[y * width + x] |= 0xff000000;
    SelectObject(source, sourceOld); SelectObject(target, targetOld); DeleteDC(source); DeleteDC(target); ReleaseDC(window_, screen);
    int slot;
    if (slotOwners_.size() < 257) { slot = ImageList_Add(images_, scaled->handle, nullptr); if (slot >= 0) slotOwners_.push_back(e.id); }
    else {
        slot = static_cast<int>(nextSlot_); nextSlot_ = nextSlot_ % 256 + 1;
        auto old = slotOwners_[slot]; thumbnailSlots_.erase(old); thumbnailRequested_.erase(old);
        for (int i = 0; i < static_cast<int>(videos_.size()); ++i) if (videos_[i].id == old) {
            LVITEMW item{}; item.mask = LVIF_IMAGE; item.iItem = i; item.iImage = 0; ListView_SetItem(Control(Videos), &item);
        }
        ImageList_Replace(images_, slot, scaled->handle, nullptr); slotOwners_[slot] = e.id;
    }
    if (slot < 0) return;
    thumbnailSlots_[e.id] = slot;
    for (int i = 0; i < static_cast<int>(videos_.size()); ++i) if (videos_[i].id == e.id) {
        LVITEMW item{}; item.mask = LVIF_IMAGE; item.iItem = i; item.iImage = slot; ListView_SetItem(Control(Videos), &item);
    }
}
void App::SelectionDetails() {
    if (rebuilding_) return;
    auto selected = Selected(Control(Queue));
    if (selected.size() == 1) {
        const auto& q = state_.queue.at(selected[0]);
        auto format = q.format == "mp3" ? std::wstring(L"MP3 audio") : std::wstring(L"MP4 ") + (q.quality ? std::to_wstring(q.quality) + L"p" : L"Best");
        StatusText(q.detail.empty() ? StateName(q.state) + L"  ·  " + format + L"  ·  " + q.destination : q.detail);
    }
}
void App::OpenSelected() {
    bool history = TabCtrl_GetCurSel(Control(Tabs)) == 1;
    auto selection = Selected(Control(history ? HistoryList : Queue));
    if (selection.empty()) return;
    auto path = history ? state_.history.at(selection[0]).file : state_.queue.at(selection[0]).file;
    if (path.empty() || !fs::is_regular_file(path)) { Error(L"The completed file is not available at its saved location. It may have been moved or deleted."); return; }
    PIDLIST_ABSOLUTE item = nullptr;
    if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &item, 0, nullptr))) { SHOpenFolderAndSelectItems(item, 0, nullptr, 0); CoTaskMemFree(item); }
}
void App::ForgetSelected() {
    auto selection = Selected(Control(HistoryList)); if (selection.empty()) return;
    if (MessageBoxW(window_, L"Remove these history records so they can be downloaded again? Your media files will be kept.", L"Forget download history", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    std::set<std::string> forgotten;
    for (auto it = selection.rbegin(); it != selection.rend(); ++it) { forgotten.insert(state_.history.at(*it).key()); state_.history.erase(state_.history.begin() + *it); }
    // Active indexes must remain stable. Completed queue entries can be cleared when idle.
    if (active_ < 0) state_.queue.erase(std::remove_if(state_.queue.begin(), state_.queue.end(), [&](const Job& q) { return q.state == JobState::Completed && forgotten.count(q.key()); }), state_.queue.end());
    Persist(); RefreshHistory(); RefreshQueue(); RefreshButtons(); StatusText(L"History records removed. Your media files were kept.");
}
void App::Command(int id) {
    if (closing_) return;
    switch (id) {
    case Paste: SendMessageW(Control(Url), EM_SETSEL, 0, -1); SendMessageW(Control(Url), WM_PASTE, 0, 0); SetFocus(Control(Url)); break;
    case Analyze: if (analysisBusy_) analysisStop_ = Stop::Cancel; else AnalyzeUrl(); break;
    case StopAnalysis: analysisStop_ = Stop::Cancel; break;
    case SelectAll: case SelectNone:
        for (int i = 0; i < static_cast<int>(videos_.size()); ++i) ListView_SetCheckState(Control(Videos), i, id == SelectAll && videos_[i].available);
        RefreshSummary(); break;
    case Options: case Browse: ShowOptions(); break;
    case Add: AddSelected(); break;
    case Pause: PauseQueue(); break;
    case Resume: ResumeQueue(); break;
    case Cancel: CancelSelected(); break;
    case CancelAll: CancelAllDownloads(); break;
    case Retry: RetrySelected(); break;
    case Tools: {
        if (analysisBusy_ || active_ >= 0 || updating_) break;
        auto p = PickFolder(window_, L"Choose folder containing yt-dlp, FFmpeg, ffprobe and Deno", state_.settings.tools);
        if (!p.empty()) { state_.settings.tools = p; Persist(); Engine(p).Check(true); StatusText(L"Tools folder: " + p); } break;
    }
    case Update: UpdateEngine(); break;
    case Log: {
        auto p = data_ / L"session.log"; AtomicWrite(p, Utf8(log_.empty() ? L"No engine output yet.\r\n" : log_));
        auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(window_, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32) Error(L"Could not open the log. It is saved at " + p.wstring()); break;
    }
    case OpenFile: OpenSelected(); break;
    case Forget: ForgetSelected(); break;
    case ClearFinished:
        if (active_ >= 0) { StatusText(L"Pause the queue before clearing finished rows."); break; }
        state_.queue.erase(std::remove_if(state_.queue.begin(), state_.queue.end(), [](const Job& q) { return q.state == JobState::Completed || q.state == JobState::Cancelled; }), state_.queue.end());
        Persist(); RefreshQueue(); RefreshButtons(); break;
    case Exit: Close(); break;
    case About: MessageBoxW(window_, L"TubeGrab 2.3\nNative Windows video and playlist downloader.\n\nDownloads and settings are saved locally.\nMP4 uses H.264 video and AAC audio; MP3 is audio only.\n\nEngines: yt-dlp, FFmpeg and Deno.", L"About TubeGrab", MB_OK | MB_ICONINFORMATION); break;
    }
}
void App::Close() {
    if (closing_) return;
    paused_ = true; closing_ = true;
    for (auto& q : state_.queue) if (q.state == JobState::Pending) q.state = JobState::Paused;
    analysisStop_ = Stop::Shutdown;
    if (downloadStop_ != Stop::Cancel) downloadStop_ = Stop::Shutdown;
    for (auto [id, c] : controls_) if (id != Status) EnableWindow(c, FALSE);
    StatusText(L"Stopping background work and saving the queue…"); FinishClose();
}
void App::FinishClose() {
    if (analysisBusy_ || updating_ || active_ >= 0) return;
    if (!Persist()) {
        closing_ = false;
        for (auto [id, c] : controls_) EnableWindow(c, TRUE);
        RefreshButtons(); Error(L"The queue could not be saved. Clear disk space or restore access to " + data_.wstring() + L", then close again. Your current queue is still in memory."); return;
    }
    try { if (!log_.empty()) AtomicWrite(data_ / L"session.log", Utf8(log_)); } catch (...) {}
    acceptingEvents_ = false; mailboxSpace_.notify_all();
    if (thumbnails_) thumbnails_->Stop();
    DestroyWindow(window_);
}
LRESULT App::HandleMessage(UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_CREATE: CreateControls(); return 0;
    case WM_SIZE: if (w != SIZE_MINIMIZED) Layout(); return 0;
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO*>(l); RECT bounds{0, 0, Scale(900), Scale(560)};
        AdjustWindowRectExForDpi(&bounds, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, TRUE, WS_EX_CONTROLPARENT, dpi_);
        info->ptMinTrackSize = {bounds.right - bounds.left, bounds.bottom - bounds.top}; return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(w); auto r = reinterpret_cast<RECT*>(l); Font(); ResetImages();
        SetWindowPos(window_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE); Layout(); return 0;
    }
    case WM_COMMAND:
        if (HIWORD(w) == BN_CLICKED || HIWORD(w) == CBN_SELCHANGE) Command(LOWORD(w)); return 0;
    case WM_QUERYENDSESSION: return Persist() ? TRUE : FALSE;
    case WM_ENDSESSION: if (w) Close(); return 0;
    case WM_CONTEXTMENU:
        if (reinterpret_cast<HWND>(w) == Control(Queue) || reinterpret_cast<HWND>(w) == Control(HistoryList)) {
            PopupQueueMenu({static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))}); return 0;
        } break;
    case WM_NOTIFY: {
        auto n = reinterpret_cast<NMHDR*>(l);
        if (n->idFrom == Queue && n->code == NM_CUSTOMDRAW) return CustomDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(l));
        if (n->idFrom == Tabs && n->code == TCN_SELCHANGE) RefreshButtons();
        if (n->idFrom == Queue && n->code == LVN_ITEMCHANGED) { SelectionDetails(); if (!rebuilding_) RefreshButtons(); }
        if (n->idFrom == HistoryList && n->code == LVN_ITEMCHANGED && !rebuilding_) RefreshButtons();
        if (n->idFrom == Videos && n->code == LVN_ITEMCHANGED) summaryDirty_ = true;
        if ((n->idFrom == Queue || n->idFrom == HistoryList) && n->code == NM_DBLCLK) {
            auto selected = Selected(Control(Queue));
            if (n->idFrom == Queue && selected.size() == 1 && !state_.queue[selected[0]].detail.empty()) Error(state_.queue[selected[0]].detail);
            else OpenSelected();
        }
        if (n->idFrom == Videos && n->code == LVN_ITEMCHANGING) {
            auto change = reinterpret_cast<NMLISTVIEW*>(l);
            if (change->iItem >= 0 && change->iItem < static_cast<int>(videos_.size()) && !videos_[change->iItem].available &&
                (change->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2)) return TRUE;
        }
        return 0;
    }
    case WM_TIMER: {
        bool events = false;
        { std::lock_guard<std::mutex> lock(mailboxMutex_); events = !mailbox_.empty(); }
        // Also drain on the timer if Windows temporarily rejected a posted
        // notification because the message queue was full.
        if (events) Drain();
        if (summaryDirty_) RefreshSummary(); RequestThumbnails(); if (closing_) FinishClose(); return 0;
    }
    case WM_EVENTS: Drain(); return 0;
    case WM_CLOSE: Close(); return 0;
    case WM_DESTROY: KillTimer(window_, 1); if (backgroundEnabled_) PostQuitMessage(0); return 0;
    case WM_CTLCOLORSTATIC:
        SetBkColor(reinterpret_cast<HDC>(w), GetSysColor(COLOR_BTNFACE));
        SetTextColor(reinterpret_cast<HDC>(w), GetSysColor(reinterpret_cast<HWND>(l) == Control(Compatibility) || reinterpret_cast<HWND>(l) == Control(Summary) ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }
    return DefWindowProcW(window_, message, w, l);
}
}
