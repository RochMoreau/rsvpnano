#pragma once
#include <cstdint>
#include <cstring>
#ifndef PROGMEM
#define PROGMEM
#endif
#define PGM_P const char*
#define PSTR(s) (s)
#define pgm_read_byte(addr) (*reinterpret_cast<const uint8_t*>(addr))
#define pgm_read_word(addr) ([](const void* a) { uint16_t v; std::memcpy(&v, a, sizeof v); return v; }(addr))
#define pgm_read_dword(addr) ([](const void* a) { uint32_t v; std::memcpy(&v, a, sizeof v); return v; }(addr))
#define pgm_read_float(addr) ([](const void* a) { float v; std::memcpy(&v, a, sizeof v); return v; }(addr))
#define pgm_read_ptr(addr) ([](const void* a) { const void* v; std::memcpy(&v, a, sizeof v); return v; }(addr))
#define pgm_read_byte_near(addr) pgm_read_byte(addr)
#define pgm_read_word_near(addr) pgm_read_word(addr)
#define memcpy_P std::memcpy
#define strlen_P std::strlen
#define strcmp_P std::strcmp
#define strncmp_P std::strncmp
#define strcpy_P std::strcpy
