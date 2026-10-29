/*
 * Azeroth Warfare: aw-account, makes a login account without the world server running (whose
 * console is the stock way, but needs the client's map data first). Prints the SQL for the realmd
 * database, with the verifier made by the server's own SRP6:
 *
 *   aw-account NAME PASSWORD [gmlevel] | mariadb realmd
 *   aw-account --new NAME PASSWORD | mariadb realmd     (sign-up: fails if the name is taken)
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "Crypto/Authentication/SRP6.h"
#include "Crypto/Hash/SHA1.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

// The shared library's log wants its file name from the executable.
char const* g_mainLogFileName = "aw-account.log";

namespace
{
    // The server uppercases names and passwords before hashing (AccountMgr::normalizeString);
    // account names are ASCII here.
    std::string Upper(std::string s)
    {
        for (char& c : s)
            c = char(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }

    std::string Hex(uint8 const* bytes, size_t n)
    {
        static char const digits[] = "0123456789ABCDEF";
        std::string out;
        for (size_t i = 0; i < n; ++i)
        {
            out.push_back(digits[bytes[i] >> 4]);
            out.push_back(digits[bytes[i] & 15]);
        }
        return out;
    }

    bool SafeName(std::string const& s)
    {
        if (s.empty() || s.size() > 16)
            return false;
        for (char c : s)
            if (!std::isalnum(static_cast<unsigned char>(c)))
                return false;
        return true;
    }
}

int main(int argc, char** argv)
{
    // A sign-up never touches an account that exists: the plain INSERT fails on the name instead
    // of resetting its password, and the level is always a player's.
    bool const fresh = argc > 1 && std::string(argv[1]) == "--new";
    if (fresh)
    {
        ++argv;
        --argc;
    }
    if (argc < 3 || (fresh && argc > 3))
    {
        std::fprintf(stderr, "usage: aw-account NAME PASSWORD [gmlevel 0-6] | mariadb realmd\n");
        return 2;
    }
    std::string const name = Upper(argv[1]);
    std::string const pass = Upper(argv[2]);
    int const gm = argc > 3 ? std::atoi(argv[3]) : 0;
    if (!SafeName(name) || pass.empty() || pass.size() > 16 || pass.find('\'') != std::string::npos
        || pass.find('\\') != std::string::npos || gm < 0 || gm > 6)
    {
        std::fprintf(stderr, "aw-account: names are 1-16 letters or digits, passwords 1-16 characters\n");
        return 2;
    }

    auto digest = Crypto::Hash::SHA1::ComputeFrom(name + ":" + pass);
    SRP6 srp;
    if (!srp.CalculateVerifier(Hex(digest.data(), digest.size())))
    {
        std::fprintf(stderr, "aw-account: could not make the verifier\n");
        return 1;
    }
    std::string const s = srp.GetSalt().AsHexStr();
    std::string const v = srp.GetVerifier().AsHexStr();
    std::printf("INSERT INTO `account` (`username`, `gmlevel`, `v`, `s`, `joindate`) VALUES ('%s', %d, '%s', '%s', NOW())%s;\n",
        name.c_str(), gm, v.c_str(), s.c_str(),
        fresh ? "" : "\n  ON DUPLICATE KEY UPDATE `v` = VALUES(`v`), `s` = VALUES(`s`), `gmlevel` = VALUES(`gmlevel`)");
    std::printf("REPLACE INTO `realmcharacters` (`realmid`, `acctid`, `numchars`) SELECT `realmlist`.`id`, `account`.`id`, 0 "
                "FROM `realmlist`, `account` LEFT JOIN `realmcharacters` ON `acctid` = `account`.`id` WHERE `acctid` IS NULL;\n");
    return 0;
}
