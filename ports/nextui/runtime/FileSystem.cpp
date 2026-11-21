// fs::FS / fs::File backed by POSIX stdio and directory streams.
#include <FS.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace fs {

    class FileImpl {
    public:
        ~FileImpl() { close(); }

        void close() {
            if (file != nullptr) {
                std::fclose(file);
                file = nullptr;
            }
            if (directory != nullptr) {
                closedir(directory);
                directory = nullptr;
            }
        }

        FS* owner = nullptr;
        std::string path;     // Firmware path, rooted at the FS ("/library/books/a.epub").
        std::string hostPath; // Absolute host path.
        std::string name;     // Final component.
        FILE* file = nullptr;
        DIR* directory = nullptr;
        bool isDirectory = false;
        bool writable = false;
    };

    namespace {
        std::string normalize(const char* path) {
            std::string result = path != nullptr ? path : "";
            if (result.empty() || result.front() != '/')
                result.insert(result.begin(), '/');
            while (result.size() > 1 && result.back() == '/')
                result.pop_back();
            return result;
        }

        std::string basename(const std::string& path) {
            const size_t slash = path.find_last_of('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        bool isDirectoryPath(const std::string& hostPath) {
            struct stat info{};
            return ::stat(hostPath.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
        }
    } // namespace

    // -----------------------------------------------------------------------------------------
    // File
    // -----------------------------------------------------------------------------------------
    size_t File::write(uint8_t value) {
        return write(&value, 1);
    }

    size_t File::write(const uint8_t* buffer, size_t size) {
        if (!impl_ || impl_->file == nullptr || !impl_->writable || buffer == nullptr)
            return 0;
        return std::fwrite(buffer, 1, size, impl_->file);
    }

    int File::available() {
        if (!impl_ || impl_->file == nullptr)
            return 0;
        const size_t total = size();
        const size_t current = position();
        return current < total ? static_cast<int>(std::min<size_t>(total - current, 0x7fffffff)) : 0;
    }

    int File::read() {
        uint8_t value = 0;
        return read(&value, 1) == 1 ? value : -1;
    }

    int File::peek() {
        if (!impl_ || impl_->file == nullptr)
            return -1;
        const int value = std::fgetc(impl_->file);
        if (value == EOF)
            return -1;
        std::ungetc(value, impl_->file);
        return value;
    }

    void File::flush() {
        if (impl_ && impl_->file != nullptr)
            std::fflush(impl_->file);
    }

    size_t File::read(uint8_t* buffer, size_t size) {
        if (!impl_ || impl_->file == nullptr || buffer == nullptr)
            return 0;
        return std::fread(buffer, 1, size, impl_->file);
    }

    bool File::seek(uint32_t position, SeekMode mode) {
        if (!impl_ || impl_->file == nullptr)
            return false;
        const int whence = mode == SeekCur ? SEEK_CUR : (mode == SeekEnd ? SEEK_END : SEEK_SET);
        // Arduino-ESP32 (VFS) refuses to seek beyond the end of a read-only file.
        if (mode == SeekSet && !impl_->writable && position > size())
            return false;
        return fseeko(impl_->file, static_cast<off_t>(position), whence) == 0;
    }

    size_t File::position() const {
        if (!impl_ || impl_->file == nullptr)
            return 0;
        const off_t value = ftello(impl_->file);
        return value < 0 ? 0 : static_cast<size_t>(value);
    }

    size_t File::size() const {
        if (!impl_)
            return 0;
        if (impl_->file != nullptr) {
            std::fflush(impl_->file);
            struct stat info{};
            if (fstat(fileno(impl_->file), &info) == 0)
                return static_cast<size_t>(info.st_size);
        }
        return 0;
    }

    void File::close() {
        if (impl_) {
            impl_->close();
            impl_.reset();
        }
    }

    File::operator bool() const {
        return impl_ && (impl_->file != nullptr || impl_->directory != nullptr);
    }

    time_t File::getLastWrite() {
        if (!impl_)
            return 0;
        struct stat info{};
        return ::stat(impl_->hostPath.c_str(), &info) == 0 ? info.st_mtime : 0;
    }

    const char* File::path() const {
        return impl_ ? impl_->path.c_str() : nullptr;
    }

    const char* File::name() const {
        return impl_ ? impl_->name.c_str() : nullptr;
    }

    bool File::isDirectory() const {
        return impl_ && impl_->isDirectory;
    }

    File File::openNextFile(const char* mode) {
        if (!impl_ || impl_->directory == nullptr || impl_->owner == nullptr)
            return File();
        while (dirent* entry = readdir(impl_->directory)) {
            if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
                continue;
            std::string child = impl_->path == "/" ? "/" : impl_->path + "/";
            child += entry->d_name;
            File file = impl_->owner->open(child.c_str(), mode);
            if (file)
                return file;
        }
        return File();
    }

    String File::getNextFileName() {
        bool isDir = false;
        return getNextFileName(&isDir);
    }

    String File::getNextFileName(bool* isDir) {
        if (!impl_ || impl_->directory == nullptr)
            return String();
        while (dirent* entry = readdir(impl_->directory)) {
            if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
                continue;
            std::string child = impl_->path == "/" ? "/" : impl_->path + "/";
            child += entry->d_name;
            if (isDir != nullptr)
                *isDir = isDirectoryPath(impl_->hostPath + "/" + entry->d_name);
            return String(child);
        }
        return String();
    }

    void File::rewindDirectory() {
        if (impl_ && impl_->directory != nullptr)
            rewinddir(impl_->directory);
    }

    // -----------------------------------------------------------------------------------------
    // FS
    // -----------------------------------------------------------------------------------------
    std::string FS::hostPath(const char* path) const {
        const std::string normalized = normalize(path);
        return normalized == "/" ? root_ : root_ + normalized;
    }

    File FS::open(const char* path, const char* mode, bool create) {
        if (root_.empty() || path == nullptr)
            return File();
        auto impl = std::make_shared<FileImpl>();
        impl->owner = this;
        impl->path = normalize(path);
        impl->hostPath = hostPath(path);
        impl->name = basename(impl->path);
        const std::string requested = mode != nullptr ? mode : FILE_READ;

        if (isDirectoryPath(impl->hostPath)) {
            if (requested != FILE_READ)
                return File();
            impl->directory = opendir(impl->hostPath.c_str());
            impl->isDirectory = true;
            return impl->directory != nullptr ? File(impl) : File();
        }

        if (requested != FILE_READ && create) {
            // Arduino's create flag makes missing parent directories.
            const std::string parent = impl->hostPath.substr(0, impl->hostPath.find_last_of('/'));
            for (size_t slash = root_.size() + 1; slash != std::string::npos && slash < parent.size();) {
                slash = parent.find('/', slash + 1);
                ::mkdir(parent.substr(0, slash).c_str(), 0775);
            }
        }

        // Binary mode; "r+" style modes are passed straight through.
        std::string stdioMode = requested;
        if (stdioMode.find('b') == std::string::npos)
            stdioMode += 'b';
        impl->file = std::fopen(impl->hostPath.c_str(), stdioMode.c_str());
        if (impl->file == nullptr)
            return File();
        impl->writable = requested != FILE_READ;
        return File(impl);
    }

    bool FS::exists(const char* path) {
        struct stat info{};
        return !root_.empty() && ::stat(hostPath(path).c_str(), &info) == 0;
    }

    bool FS::remove(const char* path) {
        return !root_.empty() && ::unlink(hostPath(path).c_str()) == 0;
    }

    bool FS::rename(const char* from, const char* to) {
        if (root_.empty())
            return false;
        const std::string target = hostPath(to);
        // FAT/VFS rename refuses to replace an existing file; keep that contract for atomic-write code.
        struct stat info{};
        if (::stat(target.c_str(), &info) == 0) {
            errno = EEXIST;
            return false;
        }
        return std::rename(hostPath(from).c_str(), target.c_str()) == 0;
    }

    bool FS::mkdir(const char* path) {
        if (root_.empty())
            return false;
        const std::string host = hostPath(path);
        return ::mkdir(host.c_str(), 0775) == 0 || (errno == EEXIST && isDirectoryPath(host));
    }

    bool FS::rmdir(const char* path) {
        return !root_.empty() && ::rmdir(hostPath(path).c_str()) == 0;
    }

    uint64_t FS::totalBytes() {
        struct statvfs info{};
        if (root_.empty() || statvfs(root_.c_str(), &info) != 0)
            return 0;
        return static_cast<uint64_t>(info.f_blocks) * info.f_frsize;
    }

    uint64_t FS::usedBytes() {
        struct statvfs info{};
        if (root_.empty() || statvfs(root_.c_str(), &info) != 0)
            return 0;
        return static_cast<uint64_t>(info.f_blocks - info.f_bfree) * info.f_frsize;
    }

} // namespace fs
