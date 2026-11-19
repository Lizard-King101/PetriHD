#include "file.hpp"

#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace phd {

bool write_file_atomic(const std::string& path, const std::vector<std::uint8_t>& data) {
    const auto temporary = path + ".tmp";
    FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) {
        return false;
    }
    bool ok = std::fwrite(data.data(), 1, data.size(), file) == data.size() && std::fflush(file) == 0;
#ifndef _WIN32
    ok = ok && fsync(fileno(file)) == 0;
#endif
    ok = std::fclose(file) == 0 && ok;
#ifdef _WIN32
    ok = ok && MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    ok = ok && std::rename(temporary.c_str(), path.c_str()) == 0;
    if (ok) {
        // Make the rename itself durable.
        const auto slash = path.find_last_of('/');
        const auto directory = slash == std::string::npos ? std::string(".") : path.substr(0, slash + 1);
        const int fd = open(directory.c_str(), O_RDONLY);
        if (fd >= 0) {
            fsync(fd);
            close(fd);
        }
    }
#endif
    if (!ok) {
        std::remove(temporary.c_str());
    }
    return ok;
}

bool read_file(const std::string& path, std::vector<std::uint8_t>& data) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        return false;
    }
    data.clear();
    std::uint8_t buffer[1 << 16];
    std::size_t n;
    while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        data.insert(data.end(), buffer, buffer + n);
    }
    const bool ok = !std::ferror(file);
    std::fclose(file);
    return ok;
}

} // namespace phd
