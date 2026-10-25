#include "common/lib/ini/Ini.h"

#include <cstdlib>

#include "common/lib/util/Util.h"

namespace coop {

std::string Ini::MakeKey(const std::string& s, const std::string& k) { return ToLower(s) + "\x01" + ToLower(k); }

bool Ini::Load(const std::wstring& path) {
    m_lines.clear();
    m_values.clear();
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(path, bytes)) return false;
    std::string text(bytes.begin(), bytes.end());
    if (text.size() >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF)
        text = text.substr(3);
    std::string section;
    for (auto& rawLine : Split(text, '\n')) {
        std::string raw = rawLine;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        std::string t = Trim(raw);
        Line line{section, "", raw};
        if (!t.empty() && t[0] == '[' && t.back() == ']') {
            section = Trim(t.substr(1, t.size() - 2));
            line.section = section;
        } else if (!t.empty() && t[0] != ';' && t[0] != '#') {
            size_t eq = t.find('=');
            if (eq != std::string::npos) {
                line.key = Trim(t.substr(0, eq));
                std::string value = Trim(t.substr(eq + 1));
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
                m_values[MakeKey(section, line.key)] = value;
            }
        }
        m_lines.push_back(line);
    }
    return true;
}

bool Ini::Save(const std::wstring& path) const {
    std::string out;
    for (auto& l : m_lines) {
        if (!l.key.empty()) {
            auto it = m_values.find(MakeKey(l.section, l.key));
            out += l.key + " = " + (it != m_values.end() ? it->second : "") + "\r\n";
        } else {
            out += l.raw + "\r\n";
        }
    }
    while (out.size() >= 4 && out.compare(out.size() - 4, 4, "\r\n\r\n") == 0) out.resize(out.size() - 2);
    return WriteFileBytes(path, out.data(), out.size());
}

std::string Ini::Get(const std::string& section, const std::string& key, const std::string& def) const {
    auto it = m_values.find(MakeKey(section, key));
    return it != m_values.end() ? it->second : def;
}

int Ini::GetInt(const std::string& section, const std::string& key, int def) const {
    auto v = Get(section, key);
    return v.empty() ? def : (int)strtol(v.c_str(), nullptr, 0);
}

float Ini::GetFloat(const std::string& section, const std::string& key, float def) const {
    auto v = Get(section, key);
    return v.empty() ? def : (float)atof(v.c_str());
}

bool Ini::GetBool(const std::string& section, const std::string& key, bool def) const {
    auto v = ToLower(Get(section, key));
    if (v.empty()) return def;
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

void Ini::Set(const std::string& section, const std::string& key, const std::string& value) {
    std::string mk = MakeKey(section, key);
    bool exists = m_values.count(mk) != 0;
    m_values[mk] = value;
    if (exists) return;
    // insert after the last line of the section (or create the section)
    int lastIdx = -1;
    bool hasSection = false;
    for (size_t i = 0; i < m_lines.size(); ++i) {
        if (ToLower(m_lines[i].section) == ToLower(section)) {
            hasSection = true;
            if (!Trim(m_lines[i].raw).empty() || !m_lines[i].key.empty()) lastIdx = (int)i;
        }
    }
    if (!hasSection) {
        if (!m_lines.empty() && !Trim(m_lines.back().raw).empty()) m_lines.push_back({m_lines.back().section, "", ""});
        m_lines.push_back({section, "", "[" + section + "]"});
        m_lines.push_back({section, key, ""});
        return;
    }
    m_lines.insert(m_lines.begin() + (lastIdx + 1), Line{section, key, ""});
}

void Ini::SetInt(const std::string& section, const std::string& key, int value) { Set(section, key, std::to_string(value)); }

}  // namespace coop
