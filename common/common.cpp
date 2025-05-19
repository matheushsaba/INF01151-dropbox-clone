#include "common.hpp"
#include <sys/stat.h>       // ::stat
#include <iomanip>
#include <sstream>
#include <cstring>
#include <iostream>
#include <vector>
#include "FileInfo.hpp"

namespace fs = std::filesystem;

namespace {
/* -------------------------------------------------------------------------
   Helper: convert a struct stat timestamp to a human-readable string
   ------------------------------------------------------------------------- */
std::string to_string(time_t t)
{
    std::ostringstream os;
    os << std::put_time(std::localtime(&t), "%c");   // e.g. “Sat May 17 12:34:56 2025”
    return os.str();
}
} //-- anonymous namespace

namespace common {

std::string ensure_sync_dir(const fs::path& base,
                            const std::string& username)
{
    fs::path dir = base / ("sync_dir_" + username);

    std::error_code ec;
    fs::create_directories(dir, ec);      // creates intermediate parts; no-op if it exists
    if (ec)
        throw std::runtime_error("could not create " + dir.string()
                                 + " : " + ec.message());

    return fs::absolute(dir).string();
}

/* -------------------------------------------------------------------------
   Build the formatted list *once* – both the client and the server used
   to carry a copy-paste of this very loop.               :contentReference[oaicite:0]{index=0}
   ------------------------------------------------------------------------- */
void list_files_with_mac(const std::vector<FileInfo>& files)
{
    for (const auto& info : files) {
        std::cout << "Name: " << info.name << '\n'
                  << "  Modified (mtime): " << info.mtime << '\n'
                  << "  Created (ctime):     " << info.ctime << '\n'
                  << "-------------------------------------------------------------\n";
    }
    if (files.empty()) {
        std::cout << "(no file found)\n";
    }
}

} // namespace common
