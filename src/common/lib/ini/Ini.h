#pragma once
#include <map>
#include <string>
#include <vector>

namespace coop {

// Small UTF-8 INI reader/writer that keeps the file order and comments when saving.
class Ini {
public:
    bool Load(const std::wstring& path);
    bool Save(const std::wstring& path) const;

    std::string Get(const std::string& section, const std::string& key, const std::string& def = "") const;
    int GetInt(const std::string& section, const std::string& key, int def = 0) const;
    float GetFloat(const std::string& section, const std::string& key, float def = 0.0f) const;
    bool GetBool(const std::string& section, const std::string& key, bool def = false) const;

    void Set(const std::string& section, const std::string& key, const std::string& value);
    void SetInt(const std::string& section, const std::string& key, int value);

private:
    struct Line {
        std::string section;  // section the line belongs to
        std::string key;      // empty for comments/section headers
        std::string raw;      // original text
    };
    std::vector<Line> m_lines;
    std::map<std::string, std::string> m_values;  // "section\x01key" (lower-case) -> value

    static std::string MakeKey(const std::string& s, const std::string& k);
};

}  // namespace coop
