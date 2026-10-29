/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Json.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace Fusion::Json
{
    Value const* Value::Get(std::string const& key) const
    {
        if (kind != Kind::Object)
            return nullptr;
        auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }

    double Value::NumberOr(std::string const& key, double fallback) const
    {
        Value const* v = Get(key);
        return v && v->kind == Kind::Number ? v->number : fallback;
    }

    std::string Value::StringOr(std::string const& key, std::string const& fallback) const
    {
        Value const* v = Get(key);
        return v && v->kind == Kind::String ? v->string : fallback;
    }

    bool Value::BoolOr(std::string const& key, bool fallback) const
    {
        Value const* v = Get(key);
        return v && v->kind == Kind::Bool ? v->boolean : fallback;
    }

    namespace
    {
        struct Parser
        {
            std::string const& s;
            size_t i = 0;
            std::string error;
            int depth = 0;

            void SkipWs()
            {
                while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
                    ++i;
            }

            bool Fail(char const* what)
            {
                if (error.empty())
                    error = std::string(what) + " at byte " + std::to_string(i);
                return false;
            }

            bool Literal(char const* word)
            {
                size_t n = std::char_traits<char>::length(word);
                if (s.compare(i, n, word) != 0)
                    return Fail("unexpected token");
                i += n;
                return true;
            }

            bool ParseString(std::string& out)
            {
                if (i >= s.size() || s[i] != '"')
                    return Fail("expected string");
                ++i;
                while (i < s.size())
                {
                    char c = s[i++];
                    if (c == '"')
                        return true;
                    if (c != '\\')
                    {
                        out.push_back(c);
                        continue;
                    }
                    if (i >= s.size())
                        break;
                    char e = s[i++];
                    switch (e)
                    {
                        case '"': out.push_back('"'); break;
                        case '\\': out.push_back('\\'); break;
                        case '/': out.push_back('/'); break;
                        case 'b': out.push_back('\b'); break;
                        case 'f': out.push_back('\f'); break;
                        case 'n': out.push_back('\n'); break;
                        case 'r': out.push_back('\r'); break;
                        case 't': out.push_back('\t'); break;
                        case 'u':
                        {
                            if (i + 4 > s.size())
                                return Fail("short \\u escape");
                            unsigned code = unsigned(std::strtoul(s.substr(i, 4).c_str(), nullptr, 16));
                            i += 4;
                            // Basic Multilingual Plane to UTF-8; asset names are ASCII in practice.
                            if (code < 0x80)
                                out.push_back(char(code));
                            else if (code < 0x800)
                            {
                                out.push_back(char(0xC0 | (code >> 6)));
                                out.push_back(char(0x80 | (code & 0x3F)));
                            }
                            else
                            {
                                out.push_back(char(0xE0 | (code >> 12)));
                                out.push_back(char(0x80 | ((code >> 6) & 0x3F)));
                                out.push_back(char(0x80 | (code & 0x3F)));
                            }
                            break;
                        }
                        default:
                            return Fail("bad escape");
                    }
                }
                return Fail("unterminated string");
            }

            bool ParseNumber(Value& v)
            {
                size_t start = i;
                if (i < s.size() && (s[i] == '-' || s[i] == '+'))
                    ++i;
                while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' ||
                    s[i] == 'e' || s[i] == 'E' || s[i] == '-' || s[i] == '+'))
                    ++i;
                if (start == i)
                    return Fail("expected number");
                char* end = nullptr;
                std::string tok = s.substr(start, i - start);
                v.kind = Value::Kind::Number;
                v.number = std::strtod(tok.c_str(), &end);
                if (!end || *end != '\0')
                    return Fail("malformed number");
                return true;
            }

            bool ParseValue(Value& v)
            {
                if (++depth > 64)
                    return Fail("nesting too deep");
                SkipWs();
                if (i >= s.size())
                    return Fail("unexpected end");
                bool ok;
                char c = s[i];
                if (c == '{')
                    ok = ParseObject(v);
                else if (c == '[')
                    ok = ParseArray(v);
                else if (c == '"')
                {
                    v.kind = Value::Kind::String;
                    ok = ParseString(v.string);
                }
                else if (c == 't')
                {
                    v.kind = Value::Kind::Bool;
                    v.boolean = true;
                    ok = Literal("true");
                }
                else if (c == 'f')
                {
                    v.kind = Value::Kind::Bool;
                    ok = Literal("false");
                }
                else if (c == 'n')
                {
                    v.kind = Value::Kind::Null;
                    ok = Literal("null");
                }
                else
                    ok = ParseNumber(v);
                --depth;
                return ok;
            }

            bool ParseArray(Value& v)
            {
                v.kind = Value::Kind::Array;
                ++i;
                SkipWs();
                if (i < s.size() && s[i] == ']')
                {
                    ++i;
                    return true;
                }
                while (true)
                {
                    Value item;
                    if (!ParseValue(item))
                        return false;
                    v.array.push_back(std::move(item));
                    SkipWs();
                    if (i < s.size() && s[i] == ',')
                    {
                        ++i;
                        continue;
                    }
                    if (i < s.size() && s[i] == ']')
                    {
                        ++i;
                        return true;
                    }
                    return Fail("expected , or ]");
                }
            }

            bool ParseObject(Value& v)
            {
                v.kind = Value::Kind::Object;
                ++i;
                SkipWs();
                if (i < s.size() && s[i] == '}')
                {
                    ++i;
                    return true;
                }
                while (true)
                {
                    SkipWs();
                    std::string key;
                    if (!ParseString(key))
                        return false;
                    SkipWs();
                    if (i >= s.size() || s[i] != ':')
                        return Fail("expected :");
                    ++i;
                    Value item;
                    if (!ParseValue(item))
                        return false;
                    v.object[key] = std::move(item);
                    SkipWs();
                    if (i < s.size() && s[i] == ',')
                    {
                        ++i;
                        continue;
                    }
                    if (i < s.size() && s[i] == '}')
                    {
                        ++i;
                        return true;
                    }
                    return Fail("expected , or }");
                }
            }
        };
    }

    bool Parse(std::string const& text, Value& out, std::string& error)
    {
        Parser p{text, 0, {}};
        out = Value{};
        if (!p.ParseValue(out))
        {
            error = p.error;
            return false;
        }
        p.SkipWs();
        if (p.i != text.size())
        {
            error = "trailing data at byte " + std::to_string(p.i);
            return false;
        }
        return true;
    }
}
