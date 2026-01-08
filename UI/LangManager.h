// LangManager.h
// Lightweight localization system for UI strings
// Uses simple JSON key-value parsing without external dependencies
#pragma once

#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <windows.h>

namespace Chess
{
    class LangManager
    {
    public:
        static LangManager& Instance()
        {
            static LangManager instance;
            return instance;
        }

        bool Load(const std::wstring& langCode)
        {
            m_currentLang = langCode;
            m_strings.clear();

            std::wstring langFile = GetLangFilePath(langCode);
            std::ifstream file(langFile);
            if (!file.is_open())
            {
                if (langCode != L"en_US")
                {
                    return Load(L"en_US");
                }
                return false;
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            file.close();

            return ParseJson(content);
        }

        bool LoadFromLanguageName(const std::wstring& languageName)
        {
            std::wstring langCode = L"en_US";
            if (languageName == L"Polski") langCode = L"pl_PL";
            else if (languageName == L"Deutsch") langCode = L"de_DE";
            else if (languageName == L"Français" || languageName == L"Francais") langCode = L"fr_FR";
            return Load(langCode);
        }

        const std::wstring& Get(const std::string& key) const
        {
            auto it = m_strings.find(key);
            if (it != m_strings.end())
            {
                return it->second;
            }
            static std::wstring empty;
            return empty;
        }

        std::wstring GetWithFallback(const std::string& key, const std::wstring& fallback) const
        {
            auto it = m_strings.find(key);
            if (it != m_strings.end() && !it->second.empty())
            {
                return it->second;
            }
            return fallback;
        }

        const std::wstring& GetCurrentLang() const { return m_currentLang; }

    private:
        LangManager()
        {
            Load(L"en_US");
        }

        std::wstring GetLangFilePath(const std::wstring& langCode) const
        {
            wchar_t exePath[MAX_PATH];
            GetModuleFileName(nullptr, exePath, MAX_PATH);
            std::wstring exeDir = exePath;
            size_t pos = exeDir.find_last_of(L"\\/");
            if (pos != std::wstring::npos)
                exeDir = exeDir.substr(0, pos + 1);
            return exeDir + L"lang\\" + langCode + L".json";
        }

        bool ParseJson(const std::string& content)
        {
            size_t i = 0;
            SkipWhitespace(content, i);
            if (i >= content.size() || content[i] != '{') return false;
            ++i;

            while (i < content.size())
            {
                SkipWhitespace(content, i);
                if (i >= content.size()) break;
                if (content[i] == '}') break;

                std::string key = ParseString(content, i);
                if (key.empty()) break;

                SkipWhitespace(content, i);
                if (i >= content.size() || content[i] != ':') break;
                ++i;

                SkipWhitespace(content, i);
                std::string value = ParseString(content, i);

                m_strings[key] = Utf8ToWide(value);

                SkipWhitespace(content, i);
                if (i < content.size() && content[i] == ',') ++i;
            }

            return !m_strings.empty();
        }

        void SkipWhitespace(const std::string& s, size_t& i) const
        {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
                ++i;
        }

        std::string ParseString(const std::string& s, size_t& i) const
        {
            SkipWhitespace(s, i);
            if (i >= s.size() || s[i] != '"') return "";
            ++i;

            std::string result;
            while (i < s.size() && s[i] != '"')
            {
                if (s[i] == '\\' && i + 1 < s.size())
                {
                    ++i;
                    switch (s[i])
                    {
                    case 'n': result += '\n'; break;
                    case 't': result += '\t'; break;
                    case 'r': result += '\r'; break;
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case 'u':
                        if (i + 4 < s.size())
                        {
                            std::string hex = s.substr(i + 1, 4);
                            unsigned int codepoint = std::stoul(hex, nullptr, 16);
                            result += EncodeUtf8(codepoint);
                            i += 4;
                        }
                        break;
                    default: result += s[i]; break;
                    }
                }
                else
                {
                    result += s[i];
                }
                ++i;
            }
            if (i < s.size()) ++i;
            return result;
        }

        std::string EncodeUtf8(unsigned int codepoint) const
        {
            std::string result;
            if (codepoint < 0x80)
            {
                result += static_cast<char>(codepoint);
            }
            else if (codepoint < 0x800)
            {
                result += static_cast<char>(0xC0 | (codepoint >> 6));
                result += static_cast<char>(0x80 | (codepoint & 0x3F));
            }
            else if (codepoint < 0x10000)
            {
                result += static_cast<char>(0xE0 | (codepoint >> 12));
                result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (codepoint & 0x3F));
            }
            else
            {
                result += static_cast<char>(0xF0 | (codepoint >> 18));
                result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (codepoint & 0x3F));
            }
            return result;
        }

        std::wstring Utf8ToWide(const std::string& utf8) const
        {
            if (utf8.empty()) return L"";
            int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
            if (size <= 0) return L"";
            std::wstring result(size - 1, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], size);
            return result;
        }

        std::unordered_map<std::string, std::wstring> m_strings;
        std::wstring m_currentLang = L"en_US";
    };

    namespace Lang
    {
        inline const std::wstring& Get(const std::string& key)
        {
            return LangManager::Instance().Get(key);
        }

        inline std::wstring Get(const std::string& key, const std::wstring& fallback)
        {
            return LangManager::Instance().GetWithFallback(key, fallback);
        }

        inline bool Load(const std::wstring& langCode)
        {
            return LangManager::Instance().Load(langCode);
        }

        inline bool LoadFromName(const std::wstring& languageName)
        {
            return LangManager::Instance().LoadFromLanguageName(languageName);
        }
    }
}
