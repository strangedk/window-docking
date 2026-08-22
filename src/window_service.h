#pragma once

#include "window_record.h"
#include <dwmapi.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <unordered_set>
#include <vector>

class WindowService {
public:
    std::vector<WindowRecord> Collect(DWORD ownProcessId) const {
        std::vector<WindowRecord> records;
        std::vector<HMONITOR> monitors;
        EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM data) {
                reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&monitors));
        struct Context {
            std::vector<WindowRecord>* records;
            DWORD own;
            const std::vector<HMONITOR>* monitors;
        } context{&records, ownProcessId, &monitors};
        EnumWindows([](HWND window, LPARAM data) {
            auto& context = *reinterpret_cast<Context*>(data);
            if (!IsWindowVisible(window) || IsCloaked(window) ||
                GetWindowTextLengthW(window) == 0) {
                return TRUE;
            }
            DWORD processId = 0;
            GetWindowThreadProcessId(window, &processId);
            if (processId == context.own) {
                return TRUE;
            }
            RECT rect{};
            if (!GetWindowRect(window, &rect) || rect.right <= rect.left ||
                rect.bottom <= rect.top) {
                return TRUE;
            }
            wchar_t className[256]{};
            GetClassNameW(window, className, 256);
            if (std::wstring(className) == L"Progman") {
                return TRUE;
            }
            WindowRecord record;
            record.title = Text(window);
            record.className = className;
            record.exePath = ImagePath(processId);
            record.state = IsIconic(window) ? L"minimized"
                                            : IsZoomed(window) ? L"maximized" : L"normal";
            record.x = rect.left;
            record.y = rect.top;
            record.width = rect.right - rect.left;
            record.height = rect.bottom - rect.top;
            record.monitorIndex = MonitorIndex(window, *context.monitors);
            context.records->push_back(std::move(record));
            return TRUE;
        }, reinterpret_cast<LPARAM>(&context));
        return records;
    }

    bool Restore(const WindowRecord& record, std::unordered_set<HWND>* used = nullptr) const {
        HWND window = Find(record, used);
        if (!window) {
            std::wstring command = L"\"" + record.exePath + L"\"";
            if (IsExplorer(record)) {
                command += L" /separate";
            }
            STARTUPINFOW startup{sizeof(startup)};
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(record.exePath.c_str(), command.data(), nullptr, nullptr, FALSE,
                                0, nullptr, nullptr, &startup, &process)) {
                return false;
            }
            CloseHandle(process.hThread);
            WaitForInputIdle(process.hProcess, 5000);
            CloseHandle(process.hProcess);
            for (int attempt = 0; attempt < 15 && !window; ++attempt) {
                Sleep(200);
                window = Find(record, used, process.dwProcessId);
            }
        }
        if (!window) {
            return false;
        }
        if (used) {
            used->insert(window);
        }
        const int showCommand = record.state == L"maximized"
                                    ? SW_MAXIMIZE
                                    : record.state == L"minimized" ? SW_MINIMIZE : SW_RESTORE;
        ShowWindow(window, showCommand);
        SetWindowPos(window, nullptr, record.x, record.y, record.width, record.height,
                     SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
        Sleep(250);
        SetWindowPos(window, nullptr, record.x, record.y, record.width, record.height,
                     SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
        SetForegroundWindow(window);
        return true;
    }
private:
    static bool IsCloaked(HWND window) {
        DWORD value = 0;
        return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &value, sizeof(value))) &&
               value != 0;
    }

    static std::wstring Text(HWND window) {
        const int length = GetWindowTextLengthW(window);
        std::vector<wchar_t> text(static_cast<size_t>(length) + 1);
        GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
        return text.data();
    }

    static std::wstring ImagePath(DWORD processId) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (!process) {
            return {};
        }
        std::vector<wchar_t> path(32768);
        DWORD size = static_cast<DWORD>(path.size());
        const bool succeeded = QueryFullProcessImageNameW(process, 0, path.data(), &size) != FALSE;
        CloseHandle(process);
        return succeeded ? std::wstring(path.data(), size) : std::wstring{};
    }

    static int MonitorIndex(HWND window, const std::vector<HMONITOR>& monitors) {
        const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        for (size_t index = 0; index < monitors.size(); ++index) {
            if (monitors[index] == monitor) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    static bool Same(std::wstring left, std::wstring right) {
        std::transform(left.begin(), left.end(), left.begin(), towlower);
        std::transform(right.begin(), right.end(), right.begin(), towlower);
        return left == right;
    }

    static bool IsExplorer(const WindowRecord& record) {
        return Same(std::filesystem::path(record.exePath).filename().wstring(), L"explorer.exe");
    }
    static HWND Find(const WindowRecord& r, const std::unordered_set<HWND>* used, DWORD pid = 0) {
        struct Context { const WindowRecord* record; const std::unordered_set<HWND>* used; DWORD pid; HWND result{}; } context{&r, used, pid};
        EnumWindows([](HWND window, LPARAM data) { auto& c = *reinterpret_cast<Context*>(data); if (!IsWindowVisible(window) || (c.used && c.used->count(window)) != 0) return TRUE; DWORD current = 0; GetWindowThreadProcessId(window, &current); if (c.pid && current != c.pid) return TRUE; if (!c.pid && !Same(ImagePath(current), c.record->exePath)) return TRUE; if (IsExplorer(*c.record) && (Text(window) != c.record->title)) return TRUE; c.result = window; return FALSE; }, reinterpret_cast<LPARAM>(&context)); return context.result;
    }
};
