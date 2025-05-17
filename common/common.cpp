#include "common.hpp"
#include <sys/stat.h>       // ::stat
#include <iomanip>
#include <sstream>
#include <cstring>

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
std::string list_files_with_mac(const fs::path& dir)
{
    std::ostringstream out;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;

        struct stat st{};
        if (::stat(entry.path().c_str(), &st) != 0) continue;

        out << "Nome: " << entry.path().filename().string() << '\n'
            << "  Acesso (atime):    " << to_string(st.st_atime) << '\n'
            << "  Modificado (mtime): " << to_string(st.st_mtime) << '\n'
            << "  Criado (ctime):     " << to_string(st.st_ctime) << '\n'
            << "-------------------------------------------------------------\n";
    }
    return out.str().empty() ? "(nenhum arquivo encontrado)\n" : out.str();
}

} // namespace common
