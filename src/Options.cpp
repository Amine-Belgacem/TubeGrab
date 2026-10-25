#include "App.h"

namespace cf {
namespace { constexpr UINT RestyleOptions = WM_APP + 17; }
void App::StyleOptions(HWND dialog, UINT dpi, bool center) {
    auto draft = reinterpret_cast<OptionsDraft*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (!draft) return;
    HFONT font = MakeUiFont(dpi);
    if (!font) throw std::runtime_error("Cannot create the download options font.");
    auto old = draft->font; draft->font = font; draft->dpi = dpi;
    SendMessageW(dialog, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (old) DeleteObject(old);
    StyleTextField(GetDlgItem(dialog, IDC_OPT_DESTINATION), dpi);
    auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
    RECT bounds{0, 0, scale(600), scale(390)};
    if (!AdjustWindowRectExForDpi(&bounds, static_cast<DWORD>(GetWindowLongPtrW(dialog, GWL_STYLE)), FALSE,
        static_cast<DWORD>(GetWindowLongPtrW(dialog, GWL_EXSTYLE)), dpi)) throw std::runtime_error("Cannot size download options.");
    int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    RECT current{}; GetWindowRect(dialog, &current);
    int x = current.left, y = current.top;
    if (center) {
        RECT parent{}; GetWindowRect(GetParent(dialog), &parent);
        x = parent.left + (parent.right - parent.left - width) / 2;
        y = parent.top + (parent.bottom - parent.top - height) / 2;
        MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &monitor)) {
            x = static_cast<int>(std::clamp<LONG>(x, monitor.rcWork.left, std::max(monitor.rcWork.left, monitor.rcWork.right - width)));
            y = static_cast<int>(std::clamp<LONG>(y, monitor.rcWork.top, std::max(monitor.rcWork.top, monitor.rcWork.bottom - height)));
        }
    }
    if (!SetWindowPos(dialog, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE))
        throw std::runtime_error("Cannot resize download options.");
    HDWP deferred = BeginDeferWindowPos(15);
    if (!deferred) throw std::runtime_error("Cannot begin download options layout.");
    auto move = [&](int id, int left, int top, int w, int h) {
        deferred = DeferWindowPos(deferred, GetDlgItem(dialog, id), nullptr, scale(left), scale(top), scale(w), scale(h), SWP_NOZORDER | SWP_NOACTIVATE);
        if (!deferred) throw std::runtime_error("Cannot arrange download options.");
    };
    move(IDC_OPT_MEDIA_GROUP, 16, 12, 568, 132);
    move(IDC_OPT_FORMAT_LABEL, 32, 40, 252, LabelHeight);
    move(IDC_OPT_FORMAT, 32, 64, 252, 220);
    move(IDC_OPT_QUALITY_LABEL, 300, 40, 268, LabelHeight);
    move(IDC_OPT_QUALITY, 300, 64, 268, 220);
    move(IDC_OPT_COMPATIBILITY, 32, 108, 536, LabelHeight);
    move(IDC_OPT_DEST_GROUP, 16, 156, 568, 164);
    move(IDC_OPT_DEST_LABEL, 32, 184, 536, LabelHeight);
    move(IDC_OPT_DESTINATION, 32, 208, 440, FieldHeight);
    move(IDC_OPT_BROWSE, 484, 208, 84, FieldHeight);
    move(IDC_OPT_FOLDER, 32, 248, 536, 24);
    move(IDC_OPT_SKIP, 32, 280, 536, 24);
    move(IDOK, 396, 344, 84, ActionHeight);
    move(IDCANCEL, 492, 344, 92, ActionHeight);
    if (!EndDeferWindowPos(deferred)) throw std::runtime_error("Cannot finish download options layout.");
    for (int id : {IDC_OPT_FORMAT, IDC_OPT_QUALITY}) SizeComboField(GetDlgItem(dialog, id), dpi);
    // Group boxes stay behind the controls they enclose, including during
    // native WM_PRINT rendering and after Windows rescales the dialog.
    for (int id : {IDC_OPT_MEDIA_GROUP, IDC_OPT_DEST_GROUP})
        SetWindowPos(GetDlgItem(dialog, id), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    RedrawWindow(dialog, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
Settings App::ReadOptions(HWND dialog, Settings settings) {
    auto destination = WindowText(GetDlgItem(dialog, IDC_OPT_DESTINATION));
    auto first = destination.find_first_not_of(L" \t\r\n"), last = destination.find_last_not_of(L" \t\r\n");
    destination = first == std::wstring::npos ? L"" : destination.substr(first, last - first + 1);
    if (destination.empty() || !fs::path(destination).is_absolute())
        throw std::runtime_error("Choose a full destination path, such as C:\\Users\\YourName\\Downloads.");
    settings.destination = std::move(destination);
    settings.format = SendDlgItemMessageW(dialog, IDC_OPT_FORMAT, CB_GETCURSEL, 0, 0) == 1 ? "mp3" : "mp4";
    constexpr int qualities[]{480, 720, 1080, 0};
    auto selected = static_cast<int>(SendDlgItemMessageW(dialog, IDC_OPT_QUALITY, CB_GETCURSEL, 0, 0));
    settings.quality = qualities[std::clamp(selected, 0, 3)];
    settings.playlistFolder = IsDlgButtonChecked(dialog, IDC_OPT_FOLDER) == BST_CHECKED;
    settings.skipDownloaded = IsDlgButtonChecked(dialog, IDC_OPT_SKIP) == BST_CHECKED;
    return settings;
}
INT_PTR CALLBACK App::OptionsProc(HWND dialog, UINT message, WPARAM w, LPARAM l) {
    auto draft = reinterpret_cast<OptionsDraft*>(GetWindowLongPtrW(dialog, DWLP_USER));
    try {
        auto updateFormat = [&] {
            bool video = SendDlgItemMessageW(dialog, IDC_OPT_FORMAT, CB_GETCURSEL, 0, 0) == 0;
            EnableWindow(GetDlgItem(dialog, IDC_OPT_QUALITY), video);
            EnableWindow(GetDlgItem(dialog, IDC_OPT_QUALITY_LABEL), video);
            SetDlgItemTextW(dialog, IDC_OPT_COMPATIBILITY, video ? L"H.264 video + AAC audio, ready for ShowDeck." : L"Audio only. MP3 uses the best available audio source.");
        };
        if (message == WM_INITDIALOG) {
            draft = reinterpret_cast<OptionsDraft*>(l);
            SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(draft));
            for (auto value : {L"MP4 video", L"MP3 audio"}) SendDlgItemMessageW(dialog, IDC_OPT_FORMAT, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
            for (auto value : {L"480p", L"720p", L"1080p", L"Best available"}) SendDlgItemMessageW(dialog, IDC_OPT_QUALITY, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
            const auto& s = draft->settings;
            SendDlgItemMessageW(dialog, IDC_OPT_FORMAT, CB_SETCURSEL, s.format == "mp3" ? 1 : 0, 0);
            SendDlgItemMessageW(dialog, IDC_OPT_QUALITY, CB_SETCURSEL, s.quality == 480 ? 0 : s.quality == 720 ? 1 : s.quality == 1080 ? 2 : 3, 0);
            SetDlgItemTextW(dialog, IDC_OPT_DESTINATION, s.destination.c_str());
            SendDlgItemMessageW(dialog, IDC_OPT_DESTINATION, EM_SETLIMITTEXT, 30000, 0);
            CheckDlgButton(dialog, IDC_OPT_FOLDER, s.playlistFolder ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dialog, IDC_OPT_SKIP, s.skipDownloaded ? BST_CHECKED : BST_UNCHECKED);
            StyleOptions(dialog, GetDpiForWindow(dialog), true);
            updateFormat();
            return TRUE;
        }
        if (message == WM_DPICHANGED && draft) {
            // Let the native dialog manager handle the monitor transition first,
            // then reapply the shared font and exact field geometry.
            PostMessageW(dialog, RestyleOptions, HIWORD(w), 0); return FALSE;
        }
        if (message == RestyleOptions && draft) { StyleOptions(dialog, static_cast<UINT>(w)); return TRUE; }
        if (message == WM_CTLCOLORDLG) return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
        if (message == WM_CTLCOLORSTATIC) {
            auto child = reinterpret_cast<HWND>(l);
            SetBkColor(reinterpret_cast<HDC>(w), GetSysColor(COLOR_BTNFACE));
            SetTextColor(reinterpret_cast<HDC>(w), GetSysColor(!IsWindowEnabled(child) || child == GetDlgItem(dialog, IDC_OPT_COMPATIBILITY) ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
        }
        if (message == WM_NCDESTROY && draft) {
            if (draft->font) DeleteObject(draft->font);
            draft->font = nullptr; SetWindowLongPtrW(dialog, DWLP_USER, 0); return FALSE;
        }
        if (message == WM_COMMAND && draft) {
            switch (LOWORD(w)) {
            case IDC_OPT_FORMAT: if (HIWORD(w) == CBN_SELCHANGE) updateFormat(); return TRUE;
            case IDC_OPT_BROWSE: {
                auto path = PickFolder(dialog, L"Choose download destination", WindowText(GetDlgItem(dialog, IDC_OPT_DESTINATION)));
                if (!path.empty()) SetDlgItemTextW(dialog, IDC_OPT_DESTINATION, path.c_str());
                return TRUE;
            }
            case IDOK: draft->settings = ReadOptions(dialog, draft->settings); EndDialog(dialog, IDOK); return TRUE;
            case IDCANCEL: EndDialog(dialog, IDCANCEL); return TRUE;
            }
        }
        if (message == WM_CLOSE) { EndDialog(dialog, IDCANCEL); return TRUE; }
    } catch (const std::exception& e) {
        MessageBoxW(dialog, Wide(e.what()).c_str(), L"Download options", MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(dialog, IDC_OPT_DESTINATION));
        return TRUE;
    }
    return FALSE;
}
void App::ShowOptions() {
    OptionsDraft draft{state_.settings};
    auto result = DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_DOWNLOAD_OPTIONS), window_, OptionsProc, reinterpret_cast<LPARAM>(&draft));
    if (result == -1) throw std::runtime_error("Could not open download options: " + Utf8(WinError(GetLastError())));
    if (result != IDOK) return;
    auto previous = state_.settings;
    state_.settings = std::move(draft.settings);
    if (!Persist()) { state_.settings = std::move(previous); Error(L"Download options could not be saved. Restore access to the local data folder and try again."); }
    RefreshVideoStatuses(); RefreshSummary();
}
}
