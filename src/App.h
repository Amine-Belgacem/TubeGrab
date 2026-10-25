#pragma once
#include "Engine.h"
#include "Thumbnails.h"
#include <commctrl.h>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <condition_variable>
#include "../resources/resource.h"
namespace cf {
class App {
public:
    App();
    ~App();
    int Run(HINSTANCE instance, int show);
private:
    friend class NativeUiTests;
    App(State state, fs::path data);
    enum Id { Url = 100, Paste, Analyze, StopAnalysis, Videos, SelectAll, SelectNone, Quality, Format,
        Destination, Browse, PlaylistFolder, Add, Tabs, Queue, HistoryList, Pause, Resume, Cancel, Retry,
        OpenFile, Forget, Tools, Update, Log, Status, Summary, QualityLabel, FormatLabel, DestLabel,
        UrlLabel, QueueLabel, Heading, Hint, ClearFinished,
        SourceGroup, PlaylistGroup, OptionsGroup, DownloadsGroup, CurrentGroup,
        SkipDownloaded, Compatibility, CurrentThumbnail, CurrentTitle, CurrentProgress,
        CurrentPercent, CurrentMetrics, Options, OutputSummary, Splitter, CancelAll, Exit = 500, About };
    enum class EventType { Video, AnalysisDone, Progress, Done, Log, UpdateDone, Thumb };
    struct Event {
        EventType type;
        Video video;
        Progress progress;
        DownloadResult result;
        std::wstring text;
        std::string id;
        std::shared_ptr<Bitmap> bitmap;
        bool ok = false;
    };
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l);
    LRESULT HandleMessage(UINT message, WPARAM w, LPARAM l);
    HWND Control(Id id) const { auto i = controls_.find(id); return i == controls_.end() ? nullptr : i->second; }
    HWND Make(Id id, const wchar_t* type, const wchar_t* text, DWORD style, DWORD ex = 0);
    void CreateControls();
    void CreateMenuBar();
    struct OptionsDraft { Settings settings; HFONT font = nullptr; UINT dpi = 96; };
    static INT_PTR CALLBACK OptionsProc(HWND dialog, UINT message, WPARAM w, LPARAM l);
    static Settings ReadOptions(HWND dialog, Settings settings);
    static void StyleOptions(HWND dialog, UINT dpi, bool center = false);
    static HFONT MakeUiFont(UINT dpi, int weight = FW_NORMAL);
    static void StyleTextField(HWND field, UINT dpi);
    static void SizeComboField(HWND combo, UINT dpi);
    void ShowOptions();
    static LRESULT CALLBACK SplitterProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK ProgressProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void MoveSplitter(int clientX);
    void Layout();
    void FitListColumns();
    void Font();
    void ResetImages();
    int Scale(int n) const { return MulDiv(n, dpi_, 96); }
    void Command(int id);
    void AnalyzeUrl();
    void AddSelected();
    void StartNext();
    void PauseQueue();
    void ResumeQueue();
    void CancelSelected();
    void CancelAllDownloads();
    void RetrySelected();
    void RefreshQueue();
    void RefreshHistory();
    void RefreshButtons();
    void RefreshSummary();
    void RefreshVideoStatuses();
    void RefreshDownloadStrip();
    std::wstring VideoStatus(const Video& video) const;
    int CurrentJob() const;
    void PopupQueueMenu(POINT point);
    bool Persist();
    void StatusText(const std::wstring& text);
    void Error(const std::wstring& text);
    void Post(Event event);
    void Drain();
    void AppendLog(const std::wstring& text);
    void UpdateEngine();
    void RequestThumbnails();
    void ShowThumbnail(const Event& event);
    void Close();
    void FinishClose();
    void SelectionDetails();
    void OpenSelected();
    void ForgetSelected();
    LRESULT CustomDraw(NMLVCUSTOMDRAW* draw);
    std::vector<int> Selected(HWND list) const;
    void Cell(HWND list, int row, int column, const std::wstring& value);
    void Column(HWND list, int index, const wchar_t* title, int width);
    void AddRow(HWND list, int index, const std::wstring& title, int image = -1);
    HWND window_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HFONT font_ = nullptr, titleFont_ = nullptr;
    HIMAGELIST images_ = nullptr;
    HIMAGELIST queueImages_ = nullptr;
    HMENU menu_ = nullptr;
    UINT dpi_ = 96;
    std::unordered_map<int, HWND> controls_;
    fs::path data_, statePath_;
    State state_;
    std::wstring notice_;
    std::vector<Video> videos_;
    std::unordered_set<std::string> videoIds_;
    std::unordered_map<std::string, std::wstring> videoStatuses_;
    std::thread analysisThread_, downloadThread_;
    std::atomic<Stop> analysisStop_{Stop::None}, downloadStop_{Stop::None};
    bool analysisBusy_ = false, updating_ = false, paused_ = true, closing_ = false, storageFailed_ = false;
    int active_ = -1;
    std::mutex mailboxMutex_;
    std::condition_variable mailboxSpace_;
    std::atomic<bool> acceptingEvents_{true};
    std::deque<Event> mailbox_;
    std::unique_ptr<Thumbnails> thumbnails_;
    std::unordered_map<std::string, int> thumbnailSlots_;
    std::unordered_set<std::string> thumbnailRequested_;
    std::vector<std::string> slotOwners_;
    size_t nextSlot_ = 1;
    std::wstring log_;
    bool rebuilding_ = false;
    bool summaryDirty_ = true;
    size_t selectedVideoCount_ = 0;
    bool backgroundEnabled_ = true;
    std::wstring lastToken_;
    bool splitterDragging_ = false;
    static constexpr int ThumbWidth = 80, ThumbHeight = 45, ThumbPaddingX = 12, ThumbPaddingY = 8;
    static constexpr int FieldHeight = 28, ActionHeight = 30, LabelHeight = 22;
};
}
