#include <string>
#include <ctime>

struct FileInfo {
    char name[256];      // or use a fixed max filename length
    time_t mtime;
    time_t ctime;
};