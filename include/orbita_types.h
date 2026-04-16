#ifndef ORBITA_TYPES_HPP
#define ORBITA_TYPES_HPP

#include <cstdint>

namespace orbita {

// Типы адресов (должны совпадать со старыми значениями из C-API)
constexpr int ORBITA_ADDR_TYPE_ANALOG_10BIT  = 0;
constexpr int ORBITA_ADDR_TYPE_CONTACT       = 1;
constexpr int ORBITA_ADDR_TYPE_FAST_2        = 2;
constexpr int ORBITA_ADDR_TYPE_FAST_1        = 3;
constexpr int ORBITA_ADDR_TYPE_FAST_3        = 4;
constexpr int ORBITA_ADDR_TYPE_FAST_4        = 5;
constexpr int ORBITA_ADDR_TYPE_BUS           = 6;
constexpr int ORBITA_ADDR_TYPE_TEMPERATURE   = 7;
constexpr int ORBITA_ADDR_TYPE_ANALOG_9BIT   = 8;

} // namespace orbita

#endif
