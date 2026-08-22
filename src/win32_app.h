#pragma once

#include "session_serializer.h"
#include "window_service.h"
#include <windows.h>
#include <filesystem>
#include <string>
#include <unordered_set>

class Win32App {
public:
    int Run(HINSTANCE instance, int commandShow) {
        instance_ = instance;
        LoadSession();
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = Procedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = className_;
        if (!RegisterClassW(&windowClass)) {
            return 0;
        }
        HWND window = CreateWindowExW(0, className_, title_, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                      CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, nullptr, nullptr,
                                      instance, this);
        if (!window) {
            return 0;
        }
        ShowWindow(window, commandShow);
        UpdateWindow(window);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }
private:
    enum class Status { Info, Success, Error };
    static constexpr wchar_t className_[] = L"WindowLayoutManagerWindow";
    static constexpr wchar_t title_[] = L"Window Layout Manager";
    static constexpr int listId_ = 1001;
    static constexpr int refreshId_ = 1002;
    static constexpr int saveId_ = 1003;
    static constexpr int excludeId_ = 1004;
    static constexpr int restoreId_ = 1005;
    static constexpr int restoreAllId_ = 1006;
    static constexpr int loadId_ = 1007;
    static constexpr int statusId_ = 1008;

    static Win32App* Instance(HWND window) {
        return reinterpret_cast<Win32App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }

    std::filesystem::path SessionPath() const {
        wchar_t path[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
        return length ? std::filesystem::path(path, path + length).parent_path() / L"session.json"
                      : std::filesystem::path{};
    }

    void SetStatus(const std::wstring& text, Status status) {
        statusColor_ = status == Status::Success
                           ? RGB(0, 128, 0)
                           : status == Status::Error ? RGB(192, 0, 0) : RGB(0, 102, 204);
        SetWindowTextW(status_, text.c_str());
        InvalidateRect(status_, nullptr, TRUE);
    }

    void LoadSession() {
        std::string error;
        const auto path = SessionPath();
        if (!std::filesystem::exists(path) || !serializer_.Load(path, records_, error)) {
            records_.clear();
            initialStatus_ = L"Information: session.json was not found.";
            if (!error.empty()) {
                initialStatus_ = L"Error: failed to load session.json: " + Json::FromUtf8(error);
            }
            return;
        }
        initialStatus_ = L"Session loaded successfully: " +
                         std::to_wstring(records_.size()) + L" windows.";
    }

    static std::wstring RecordKey(const WindowRecord& record) {
        return record.title + L"\x1F" + record.className + L"\x1F" + record.exePath;
    }

    void Populate() {
        SendMessageW(list_, LB_RESETCONTENT, 0, 0);
        visible_.clear();
        for (size_t index = 0; index < records_.size(); ++index) {
            if (excluded_.count(RecordKey(records_[index]))) {
                continue;
            }
            SendMessageW(list_, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(records_[index].title.c_str()));
            visible_.push_back(index);
        }
    }

    void LoadFromFile() {
        std::string error;
        records_.clear();
        const auto path = SessionPath();
        if (!std::filesystem::exists(path) || !serializer_.Load(path, records_, error)) {
            SetStatus(L"Error: failed to load session.json: " + Json::FromUtf8(error),
                      Status::Error);
            records_.clear();
        } else {
            SetStatus(L"Session loaded successfully: " +
                          std::to_wstring(records_.size()) + L" windows.",
                      Status::Success);
        }
        excluded_.clear();
        Populate();
    }

    void Refresh() {
        records_ = windows_.Collect(GetCurrentProcessId());
        Populate();
        SetStatus(L"Information: refreshed visible windows.", Status::Info);
    }

    void Exclude() {
        const int item = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
        if (item < 0 || static_cast<size_t>(item) >= visible_.size()) {
            SetStatus(L"Information: select a window first.", Status::Info);
            return;
        }
        excluded_.insert(RecordKey(records_[visible_[static_cast<size_t>(item)]]));
        Populate();
        SetStatus(L"Window excluded successfully.", Status::Success);
    }

    void Restore(bool all) {
        std::unordered_set<HWND> used;
        size_t count = 0;
        if (all) {
            for (const size_t index : visible_) {
                count += windows_.Restore(records_[index], &used);
            }
        } else {
            const int item = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
            if (item < 0 || static_cast<size_t>(item) >= visible_.size()) {
                SetStatus(L"Information: select a window first.", Status::Info);
                return;
            }
            count = windows_.Restore(records_[visible_[static_cast<size_t>(item)]]);
        }
        SetStatus(all ? L"Restored windows: " + std::to_wstring(count) + L" of " +
                           std::to_wstring(visible_.size()) + L"."
                     : L"Window restored successfully.",
                  count ? Status::Success : Status::Error);
    }

    void Save() {
        std::vector<WindowRecord> saved;
        for (const size_t index : visible_) {
            saved.push_back(records_[index]);
        }
        if (!serializer_.Save(SessionPath(), saved)) {
            SetStatus(L"Error: unable to save session.json.", Status::Error);
            return;
        }
        SetStatus(L"Session saved successfully: " + std::to_wstring(saved.size()) + L" windows.",
                  Status::Success);
    }
    void Resize(HWND window) {
        RECT rect{};
        GetClientRect(window, &rect);
        constexpr int margin = 12;
        constexpr int statusHeight = 28;
        constexpr int buttonHeight = 56;
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        const int buttonWidth = (std::max)(80, (width - 2 * margin - 5 * margin) / 6);
        const int start = (std::max)(margin, (width - (6 * buttonWidth + 5 * margin)) / 2);
        MoveWindow(list_, margin, margin, width - 2 * margin,
                   height - 3 * margin - statusHeight - buttonHeight, TRUE);
        MoveWindow(status_, margin, height - 2 * margin - statusHeight - buttonHeight,
                   width - 2 * margin, statusHeight, TRUE);
        HWND buttons[] = {GetDlgItem(window, refreshId_), GetDlgItem(window, excludeId_),
                          GetDlgItem(window, restoreId_), GetDlgItem(window, restoreAllId_),
                          GetDlgItem(window, loadId_), GetDlgItem(window, saveId_)};
        for (int index = 0; index < 6; ++index) {
            MoveWindow(buttons[index], start + index * (buttonWidth + margin),
                       height - margin - buttonHeight, buttonWidth, buttonHeight, TRUE);
        }
    }

    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        Win32App* app = Instance(window);
        if (message == WM_NCCREATE) {
            app = static_cast<Win32App*>(
                reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->CreateControls(window);
        }
        if (!app) {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        if (message == WM_SIZE) {
            app->Resize(window);
        } else if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED) {
            const int id = LOWORD(wParam);
            if (id == refreshId_) app->Refresh();
            else if (id == loadId_) app->LoadFromFile();
            else if (id == excludeId_) app->Exclude();
            else if (id == restoreId_) app->Restore(false);
            else if (id == restoreAllId_) app->Restore(true);
            else if (id == saveId_) app->Save();
        } else if (message == WM_COMMAND &&
                   (HIWORD(wParam) == LBN_SELCHANGE || HIWORD(wParam) == LBN_DBLCLK) &&
                   LOWORD(wParam) == listId_) {
            app->Restore(false);
        } else if (message == WM_KEYDOWN && wParam == VK_DELETE && GetFocus() == app->list_) {
            app->Exclude();
        } else if (message == WM_CTLCOLORSTATIC &&
                   reinterpret_cast<HWND>(lParam) == app->status_) {
            SetTextColor(reinterpret_cast<HDC>(wParam), app->statusColor_);
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        } else if (message == WM_DESTROY) {
            PostQuitMessage(0);
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void CreateControls(HWND window) {
        list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 0, 0, window,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(listId_)),
                                instance_, nullptr);
        status_ = CreateWindowExW(0, L"STATIC", L"",
                                  WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                                  0, 0, 0, 0, window,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(statusId_)),
                                  instance_, nullptr);
        const wchar_t* labels[] = {L"Refresh threads", L"Exclude selected", L"Restore selected",
                                    L"Restore all", L"Load session", L"Save session"};
        const int ids[] = {refreshId_, excludeId_, restoreId_, restoreAllId_, loadId_, saveId_};
        for (int index = 0; index < 6; ++index) {
            CreateWindowExW(0, L"BUTTON", labels[index],
                            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_MULTILINE |
                                BS_CENTER | BS_VCENTER,
                            0, 0, 0, 0, window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ids[index])),
                            instance_, nullptr);
        }
        Populate();
        const bool isError = initialStatus_.find(L"Error:") == 0;
        const bool isInfo = initialStatus_.find(L"Information:") == 0;
        SetStatus(initialStatus_, isError ? Status::Error : isInfo ? Status::Info : Status::Success);
    }

    HINSTANCE instance_{};
    HWND list_{};
    HWND status_{};
    COLORREF statusColor_ = RGB(0, 102, 204);
    std::wstring initialStatus_;
    std::vector<WindowRecord> records_;
    std::vector<size_t> visible_;
    std::unordered_set<std::wstring> excluded_;
    WindowService windows_;
    SessionSerializer serializer_;
};
