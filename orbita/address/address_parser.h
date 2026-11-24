#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace orbita {

struct ChannelDesc {
    uint32_t numOutElemG = 0;
    uint32_t stepOutG = 0;
    uint8_t adressType = 0;
    uint8_t bitNumber = 0;
    std::vector<uint16_t> groups;
    std::vector<uint16_t> cycles;
    int wordIndex = -1;  // индекс в группе (0..2047)
    int poolIndex = -1;  // порядковый номер в типо-специфичном пуле DataPool
    int width = 1;       // кол-во слов канала (Т22, Т24 и далее)
    std::string userName;   // имя параметра (из БД)
    std::string category;   // категория (из БД)
};

class AddressParser {
public:
    static ChannelDesc parseLine(const std::string& line);
    static std::vector<ChannelDesc> loadFile(const std::string& filename);
    static std::vector<ChannelDesc> loadLines(const std::vector<std::string>& lines);
};

} // namespace orbita