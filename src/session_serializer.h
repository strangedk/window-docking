#pragma once

#include "json.h"
#include <filesystem>
#include <fstream>
#include <iterator>

class SessionSerializer {
public:
    bool Load(const std::filesystem::path& path, std::vector<WindowRecord>& records, std::string& error) const {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            error = "Unable to open session file.";
            return false;
        }
        std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        records.clear();
        return Json::Parse(source, records, error);
    }

    bool Save(const std::filesystem::path& path, const std::vector<WindowRecord>& records) const {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }

        SYSTEMTIME time{};
        GetSystemTime(&time);
        char stamp[32]{};
        sprintf_s(stamp, "%04u-%02u-%02uT%02u:%02u:%02uZ", time.wYear, time.wMonth,
                  time.wDay, time.wHour, time.wMinute, time.wSecond);
        file << Json::Serialize(records, stamp);
        return static_cast<bool>(file);
    }
};
