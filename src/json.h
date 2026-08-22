#pragma once

#include "window_record.h"
#include <windows.h>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

class Json {
public:
    static std::string ToUtf8(const std::wstring& value) {
        if (value.empty()) {
            return {};
        }
        const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0,
                                             nullptr, nullptr);
        std::string result(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(),
                            size, nullptr, nullptr);
        return result;
    }

    static std::wstring FromUtf8(const std::string& value) {
        if (value.empty()) {
            return {};
        }
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0);
        if (size == 0) {
            return {};
        }
        std::wstring result(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                            result.data(), size);
        return result;
    }

    static std::string Escape(const std::wstring& value) {
        std::string result;
        for (const unsigned char character : ToUtf8(value)) {
            if (character == '"') {
                result += "\\\"";
            } else if (character == '\\') {
                result += "\\\\";
            } else if (character == '\n') {
                result += "\\n";
            } else if (character == '\r') {
                result += "\\r";
            } else if (character == '\t') {
                result += "\\t";
            } else {
                result += static_cast<char>(character);
            }
        }
        return result;
    }

    static std::string Serialize(const std::vector<WindowRecord>& records, const std::string& time) {
        std::string output =
            "{\n  \"version\": 1,\n  \"savedAt\": \"" + time +
            "\",\n  \"windows\": [\n";
        for (size_t index = 0; index < records.size(); ++index) {
            const WindowRecord& record = records[index];
            output +=
                "    {\n      \"title\": \"" + Escape(record.title) +
                "\",\n      \"className\": \"" + Escape(record.className) +
                "\",\n      \"exePath\": \"" + Escape(record.exePath) +
                "\",\n      \"state\": \"" + Escape(record.state) +
                "\",\n      \"x\": " + std::to_string(record.x) +
                ",\n      \"y\": " + std::to_string(record.y) +
                ",\n      \"width\": " + std::to_string(record.width) +
                ",\n      \"height\": " + std::to_string(record.height) +
                ",\n      \"monitorIndex\": " + std::to_string(record.monitorIndex) +
                "\n    }" + (index + 1 == records.size() ? "\n" : ",\n");
        }
        return output + "  ]\n}\n";
    }

    static bool Parse(std::string_view source, std::vector<WindowRecord>& records,
                      std::string& error) {
        Parser parser(source, error);
        return parser.ReadSession(records);
    }

private:
    class Parser {
    public:
        Parser(std::string_view source, std::string& error)
            : source_(source), error_(error) {}

        bool ReadSession(std::vector<WindowRecord>& records) {
            if (!Expect('{')) {
                return false;
            }
            bool hasVersion = false;
            bool hasWindows = false;
            int version = 0;
            while (!Consume('}')) {
                std::string key;
                if (!ReadString(key) || !Expect(':')) {
                    return false;
                }
                if (key == "version") {
                    if (!ReadInt(version)) {
                        return Fail("The version field must be an integer.");
                    }
                    hasVersion = true;
                } else if (key == "windows") {
                    if (!ReadArray(records)) {
                        return false;
                    }
                    hasWindows = true;
                } else if (!Skip()) {
                    return false;
                }
                if (Consume('}')) {
                    break;
                }
                if (!Expect(',')) {
                    return false;
                }
            }
            SkipSpace();
            if (position_ != source_.size()) {
                return Fail("Unexpected data after the root object.");
            }
            if (!hasVersion || !hasWindows) {
                return Fail("The session is missing a required field.");
            }
            return version == 1 ? true : Fail("Unsupported session version.");
        }

    private:
        bool Fail(const std::string& message) {
            error_ = message;
            return false;
        }

        void SkipSpace() {
            while (position_ < source_.size() && source_[position_] <= ' ') {
                ++position_;
            }
        }

        bool Consume(char character) {
            SkipSpace();
            if (position_ >= source_.size() || source_[position_] != character) {
                return false;
            }
            ++position_;
            return true;
        }

        bool Expect(char character) {
            return Consume(character) ||
                   Fail(std::string("Expected '") + character + "'.");
        }

        bool ReadString(std::string& value) {
            SkipSpace();
            if (position_ >= source_.size() || source_[position_] != '"') {
                return false;
            }
            ++position_;
            value.clear();
            while (position_ < source_.size()) {
                char character = source_[position_++];
                if (character == '"') {
                    return true;
                }
                if (character == '\\') {
                    if (position_ >= source_.size()) {
                        return false;
                    }
                    character = source_[position_++];
                    if (character == 'n') {
                        character = '\n';
                    } else if (character == 'r') {
                        character = '\r';
                    } else if (character == 't') {
                        character = '\t';
                    } else if (character != '"' && character != '\\' && character != '/') {
                        return false;
                    }
                }
                value += character;
            }
            return false;
        }

        bool ReadInt(int& value) {
            SkipSpace();
            const size_t start = position_;
            if (position_ < source_.size() && source_[position_] == '-') {
                ++position_;
            }
            while (position_ < source_.size() && source_[position_] >= '0' &&
                   source_[position_] <= '9') {
                ++position_;
            }
            if (start == position_ ||
                (source_[start] == '-' && start + 1 == position_)) {
                return false;
            }
            try {
                const long long number = std::stoll(
                    std::string(source_.substr(start, position_ - start)));
                if (number < (std::numeric_limits<int>::min)() ||
                    number > (std::numeric_limits<int>::max)()) {
                    return false;
                }
                value = static_cast<int>(number);
                return true;
            } catch (...) {
                return false;
            }
        }

        bool ReadArray(std::vector<WindowRecord>& records) {
            if (!Expect('[')) {
                return false;
            }
            if (Consume(']')) {
                return true;
            }
            while (true) {
                WindowRecord record;
                if (!ReadRecord(record)) {
                    return false;
                }
                records.push_back(std::move(record));
                if (Consume(']')) {
                    return true;
                }
                if (!Expect(',')) {
                    return false;
                }
            }
        }

        bool ReadRecord(WindowRecord& record) {
            if (!Expect('{')) {
                return false;
            }
            bool fields[9]{};
            while (!Consume('}')) {
                std::string key;
                std::string text;
                if (!ReadString(key) || !Expect(':')) {
                    return false;
                }
                int* number = nullptr;
                if (key == "title") {
                    if (!ReadString(text)) return false;
                    record.title = FromUtf8(text);
                    fields[0] = true;
                } else if (key == "className") {
                    if (!ReadString(text)) return false;
                    record.className = FromUtf8(text);
                    fields[1] = true;
                } else if (key == "exePath") {
                    if (!ReadString(text)) return false;
                    record.exePath = FromUtf8(text);
                    fields[2] = true;
                } else if (key == "state") {
                    if (!ReadString(text)) return false;
                    record.state = FromUtf8(text);
                    fields[3] = true;
                } else if (key == "x") {
                    number = &record.x;
                    fields[4] = ReadInt(*number);
                } else if (key == "y") {
                    number = &record.y;
                    fields[5] = ReadInt(*number);
                } else if (key == "width") {
                    number = &record.width;
                    fields[6] = ReadInt(*number);
                } else if (key == "height") {
                    number = &record.height;
                    fields[7] = ReadInt(*number);
                } else if (key == "monitorIndex") {
                    number = &record.monitorIndex;
                    fields[8] = ReadInt(*number);
                } else if (!Skip()) {
                    return false;
                }
                if (number != nullptr && !fields[4 + (&record.x == number ? 0 :
                                                       &record.y == number ? 1 :
                                                       &record.width == number ? 2 :
                                                       &record.height == number ? 3 : 4)]) {
                    return false;
                }
                if (Consume('}')) {
                    break;
                }
                if (!Expect(',')) {
                    return false;
                }
            }
            for (const bool field : fields) {
                if (!field) {
                    return Fail("A window record is missing a required field.");
                }
            }
            return true;
        }

        bool Skip() {
            std::string text;
            if (ReadString(text)) {
                return true;
            }
            int number = 0;
            return ReadInt(number);
        }

        std::string_view source_;
        std::string& error_;
        size_t position_{};
    };
};
