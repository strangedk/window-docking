#pragma once

#include <string>

struct WindowRecord {
    std::wstring title;
    std::wstring className;
    std::wstring exePath;
    std::wstring state;
    int x{};
    int y{};
    int width{};
    int height{};
    int monitorIndex{};
};
