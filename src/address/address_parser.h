#pragma once
#include <string>
#include <vector>

namespace orbita {

struct ChannelDesc {
    uint32_t numOutElemG = 0;
    uint32_t stepOutG = 0;
    uint8_t adressType = 0;
    uint8_t bitNumber = 0;
    std::vector<uint16_t> groups;
    std::vector<uint16_t> cycles;
};

class AddressParser {
public:
    static ChannelDesc parseLine(const std::string& line);
    static std::vector<ChannelDesc> loadFile(const std::string& filename);
    static std::vector<ChannelDesc> loadLines(const std::vector<std::string>& lines);
};

} // namespace orbita
