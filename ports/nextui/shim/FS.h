#pragma once
// Arduino-ESP32 FS API backed by POSIX files under a host directory.
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>

#include "Stream.h"

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

namespace fs {

    enum SeekMode {
        SeekSet = 0,
        SeekCur = 1,
        SeekEnd = 2,
    };

    class FileImpl;
    using FileImplPtr = std::shared_ptr<FileImpl>;

    class File : public Stream {
    public:
        File() = default;
        explicit File(FileImplPtr impl) : impl_(std::move(impl)) {}

        size_t write(uint8_t value) override;
        size_t write(const uint8_t* buffer, size_t size) override;
        using Print::write;
        int available() override;
        int read() override;
        int peek() override;
        void flush() override;
        size_t read(uint8_t* buffer, size_t size);
        size_t readBytes(char* buffer, size_t length) { return read(reinterpret_cast<uint8_t*>(buffer), length); }
        size_t readBytes(uint8_t* buffer, size_t length) override { return read(buffer, length); }
        bool seek(uint32_t position, SeekMode mode);
        bool seek(uint32_t position) { return seek(position, SeekSet); }
        size_t position() const;
        size_t size() const;
        bool setBufferSize(size_t) { return true; }
        void close();
        explicit operator bool() const;
        time_t getLastWrite();
        const char* path() const;
        const char* name() const;
        bool isDirectory() const;
        File openNextFile(const char* mode = FILE_READ);
        String getNextFileName();
        String getNextFileName(bool* isDir);
        void rewindDirectory();

    private:
        FileImplPtr impl_;
    };

    class FS {
    public:
        FS() = default;
        explicit FS(std::string root) : root_(std::move(root)) {}

        void setRoot(std::string root) { root_ = std::move(root); }
        const std::string& root() const { return root_; }
        std::string hostPath(const char* path) const;

        File open(const char* path, const char* mode = FILE_READ, bool create = false);
        File open(const String& path, const char* mode = FILE_READ, bool create = false) {
            return open(path.c_str(), mode, create);
        }
        bool exists(const char* path);
        bool exists(const String& path) { return exists(path.c_str()); }
        bool remove(const char* path);
        bool remove(const String& path) { return remove(path.c_str()); }
        bool rename(const char* from, const char* to);
        bool rename(const String& from, const String& to) { return rename(from.c_str(), to.c_str()); }
        bool mkdir(const char* path);
        bool mkdir(const String& path) { return mkdir(path.c_str()); }
        bool rmdir(const char* path);
        bool rmdir(const String& path) { return rmdir(path.c_str()); }
        uint64_t totalBytes();
        uint64_t usedBytes();

    private:
        std::string root_;
    };

} // namespace fs

using fs::File;
using fs::FS;
using fs::SeekCur;
using fs::SeekEnd;
using fs::SeekSet;
