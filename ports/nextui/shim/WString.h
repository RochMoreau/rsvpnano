#pragma once
// Arduino String on top of std::string.
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

class __FlashStringHelper;

class String {
public:
    String() = default;
    String(const char* cs) : s_(cs ? cs : "") {}
    String(const char* cs, size_t length) : s_(cs ? std::string(cs, length) : std::string()) {}
    String(const __FlashStringHelper* fs) : String(reinterpret_cast<const char*>(fs)) {}
    String(const std::string& s) : s_(s) {}
    String(std::string&& s) : s_(std::move(s)) {}
    String(std::string_view s) : s_(s) {}
    explicit String(char c) : s_(1, c) {}
    explicit String(unsigned char value, unsigned char base = 10) : s_(toBase(value, base)) {}
    explicit String(int value, unsigned char base = 10) : s_(toBaseSigned(value, base)) {}
    explicit String(unsigned int value, unsigned char base = 10) : s_(toBase(value, base)) {}
    explicit String(long value, unsigned char base = 10) : s_(toBaseSigned(value, base)) {}
    explicit String(unsigned long value, unsigned char base = 10) : s_(toBase(value, base)) {}
    explicit String(long long value, unsigned char base = 10) : s_(toBaseSigned(value, base)) {}
    explicit String(unsigned long long value, unsigned char base = 10) : s_(toBase(value, base)) {}
    explicit String(float value, unsigned int decimals = 2) : s_(fixed(value, decimals)) {}
    explicit String(double value, unsigned int decimals = 2) : s_(fixed(value, decimals)) {}

    String(const String&) = default;
    String(String&&) noexcept = default;
    String& operator=(const String&) = default;
    String& operator=(String&&) noexcept = default;
    String& operator=(const char* cs) {
        s_ = cs ? cs : "";
        return *this;
    }

    size_t length() const { return s_.size(); }
    bool isEmpty() const { return s_.empty(); }
    const char* c_str() const { return s_.c_str(); }
    char* begin() { return s_.data(); }
    char* end() { return s_.data() + s_.size(); }
    const char* begin() const { return s_.data(); }
    const char* end() const { return s_.data() + s_.size(); }
    bool reserve(size_t n) { s_.reserve(n); return true; }
    operator std::string_view() const { return s_; }
    const std::string& std() const { return s_; }

    char operator[](size_t i) const { return i < s_.size() ? s_[i] : '\0'; }
    char& operator[](size_t i) { return s_[i]; }
    char charAt(size_t i) const { return (*this)[i]; }
    void setCharAt(size_t i, char c) { if (i < s_.size()) s_[i] = c; }

    bool concat(const String& o) { s_ += o.s_; return true; }
    bool concat(const char* cs) { if (cs) s_ += cs; return true; }
    bool concat(const char* cs, size_t n) { if (cs) s_.append(cs, n); return true; }
    bool concat(char c) { s_ += c; return true; }
    bool concat(int v) { s_ += std::to_string(v); return true; }
    bool concat(unsigned v) { s_ += std::to_string(v); return true; }
    bool concat(long v) { s_ += std::to_string(v); return true; }
    bool concat(unsigned long v) { s_ += std::to_string(v); return true; }
    bool concat(long long v) { s_ += std::to_string(v); return true; }
    bool concat(unsigned long long v) { s_ += std::to_string(v); return true; }
    bool concat(double v) { s_ += fixed(v, 2); return true; }

    template<typename T>
    String& operator+=(const T& v) { concat(v); return *this; }

    friend String operator+(String lhs, const String& rhs) { lhs.s_ += rhs.s_; return lhs; }
    friend String operator+(String lhs, const char* rhs) { lhs.concat(rhs); return lhs; }
    friend String operator+(String lhs, char rhs) { lhs.s_ += rhs; return lhs; }
    friend String operator+(String lhs, int rhs) { lhs.concat(rhs); return lhs; }
    friend String operator+(String lhs, unsigned rhs) { lhs.concat(rhs); return lhs; }
    friend String operator+(String lhs, long rhs) { lhs.concat(rhs); return lhs; }
    friend String operator+(String lhs, unsigned long rhs) { lhs.concat(rhs); return lhs; }
    friend String operator+(const char* lhs, const String& rhs) { String out(lhs); out.s_ += rhs.s_; return out; }

    bool operator==(const String& o) const { return s_ == o.s_; }
    bool operator==(const char* cs) const { return s_ == (cs ? cs : ""); }
    bool operator!=(const String& o) const { return s_ != o.s_; }
    bool operator!=(const char* cs) const { return !(*this == cs); }
    bool operator<(const String& o) const { return s_ < o.s_; }
    bool equals(const String& o) const { return s_ == o.s_; }
    bool equals(const char* cs) const { return *this == cs; }
    bool equalsIgnoreCase(const String& o) const {
        if (o.s_.size() != s_.size()) return false;
        for (size_t i = 0; i < s_.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(s_[i])) != std::tolower(static_cast<unsigned char>(o.s_[i])))
                return false;
        return true;
    }
    int compareTo(const String& o) const { return s_.compare(o.s_); }

    bool startsWith(const String& p) const { return s_.starts_with(p.s_); }
    bool startsWith(const char* p) const { return p && s_.starts_with(p); }
    bool endsWith(const String& p) const { return s_.ends_with(p.s_); }
    bool endsWith(const char* p) const { return p && s_.ends_with(p); }

    int indexOf(char c, unsigned from = 0) const { return pos(s_.find(c, from)); }
    int indexOf(const char* n, unsigned from = 0) const { return n ? pos(s_.find(n, from)) : -1; }
    int indexOf(const String& n, unsigned from = 0) const { return pos(s_.find(n.s_, from)); }
    int lastIndexOf(char c) const { return pos(s_.rfind(c)); }
    int lastIndexOf(char c, unsigned from) const { return pos(s_.rfind(c, from)); }
    int lastIndexOf(const String& n) const { return pos(s_.rfind(n.s_)); }

    String substring(unsigned b) const { return b >= s_.size() ? String() : String(s_.substr(b)); }
    String substring(unsigned b, unsigned e) const {
        if (b > e) std::swap(b, e);
        if (b >= s_.size()) return String();
        e = std::min<unsigned>(e, static_cast<unsigned>(s_.size()));
        return String(s_.substr(b, e - b));
    }

    void replace(char f, char r) { for (char& c : s_) if (c == f) c = r; }
    void replace(const String& f, const String& r) {
        if (f.s_.empty()) return;
        size_t p = 0;
        while ((p = s_.find(f.s_, p)) != std::string::npos) { s_.replace(p, f.s_.size(), r.s_); p += r.s_.size(); }
    }
    void remove(unsigned i) { if (i < s_.size()) s_.erase(i); }
    void remove(unsigned i, unsigned n) { if (i < s_.size()) s_.erase(i, n); }
    void toLowerCase() { for (char& c : s_) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    void toUpperCase() { for (char& c : s_) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
    void trim() {
        size_t b = 0, e = s_.size();
        while (b < e && std::isspace(static_cast<unsigned char>(s_[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(s_[e - 1]))) --e;
        s_ = s_.substr(b, e - b);
    }
    long toInt() const { return std::strtol(s_.c_str(), nullptr, 10); }
    float toFloat() const { return std::strtof(s_.c_str(), nullptr); }
    double toDouble() const { return std::strtod(s_.c_str(), nullptr); }
    void getBytes(unsigned char* buf, unsigned n, unsigned index = 0) const { toCharArray(reinterpret_cast<char*>(buf), n, index); }
    void toCharArray(char* buf, unsigned n, unsigned index = 0) const {
        if (!buf || n == 0) return;
        const size_t count = index < s_.size() ? std::min<size_t>(n - 1, s_.size() - index) : 0;
        if (count) std::memcpy(buf, s_.data() + index, count);
        buf[count] = '\0';
    }
    void clear() { s_.clear(); }

private:
    static int pos(size_t p) { return p == std::string::npos ? -1 : static_cast<int>(p); }
    template<typename T>
    static std::string toBase(T v, unsigned base) {
        if (base == 10) return std::to_string(v);
        if (base < 2 || base > 36) base = 10;
        std::string out;
        unsigned long long value = v;
        do { const unsigned d = value % base; out.insert(out.begin(), static_cast<char>(d < 10 ? '0' + d : 'a' + d - 10)); value /= base; } while (value);
        return out;
    }
    template<typename T>
    static std::string toBaseSigned(T v, unsigned base) {
        if (base == 10) return std::to_string(v);
        return toBase(static_cast<unsigned long long>(static_cast<std::make_unsigned_t<T>>(v)), base);
    }
    static std::string fixed(double v, unsigned decimals) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.*f", static_cast<int>(decimals), v);
        return buffer;
    }
    std::string s_;
};
