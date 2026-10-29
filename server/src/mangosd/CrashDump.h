// Azeroth Warfare: a crash leaves a minidump and a note in the logs folder (Windows/MSVC).
//
// On Windows the CRT's SIGSEGV handler is per thread (only the main thread gets it), so an
// access violation in the world or a map thread used to end the process with nothing in
// Server.log. Install() sets a process-wide unhandled-exception filter that writes
//   <LogsDir>/mangosd-<date>-<time>.dmp        (open in Visual Studio / WinDbg with mangosd.pdb)
//   <LogsDir>/mangosd-<date>-<time>.crash.txt  (exception code, module and offset)
// and appends the same line to Server.log; the offset resolves with
// server/contrib/crash/symbolize.py. Elsewhere it does nothing.

#ifndef MANGOSD_CRASHDUMP_H
#define MANGOSD_CRASHDUMP_H

#include <string>

namespace CrashDump
{
    // logsDir: where the files go ("" = the working directory); serverLog: the basic log's
    // file name inside it ("" = none).
    void Install(std::string const& logsDir, std::string const& serverLog);
}

#endif
