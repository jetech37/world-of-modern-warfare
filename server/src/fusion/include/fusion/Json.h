/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_JSON_H
#define FUSION_JSON_H

#include <map>
#include <memory>
#include <string>
#include <vector>

// A small JSON reader for the fusion data files (weapons.json): objects, arrays, strings,
// numbers, booleans and null. The server has no JSON library; this keeps the rules standalone.
namespace Fusion::Json
{
    struct Value
    {
        enum class Kind
        {
            Null,
            Bool,
            Number,
            String,
            Array,
            Object,
        };

        Kind kind = Kind::Null;
        bool boolean = false;
        double number = 0.0;
        std::string string;
        std::vector<Value> array;
        std::map<std::string, Value> object;

        bool IsObject() const { return kind == Kind::Object; }
        bool IsArray() const { return kind == Kind::Array; }
        Value const* Get(std::string const& key) const;
        double NumberOr(std::string const& key, double fallback) const;
        std::string StringOr(std::string const& key, std::string const& fallback) const;
        bool BoolOr(std::string const& key, bool fallback) const;
    };

    // Parse a whole document; false with a message (and its byte offset) on malformed input.
    bool Parse(std::string const& text, Value& out, std::string& error);
}

#endif
