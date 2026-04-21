#pragma once
#include <vector>
#include <string>
#include "address_parser.h"
#include <cstdint>
namespace orbita {

class AddressManager {
public:
    explicit AddressManager(int informativnost);
    void addChannel(const ChannelDesc& desc);
    void addChannels(const std::vector<ChannelDesc>& descs);
    void loadFromFile(const std::string& filename);
    void loadFromLines(const std::vector<std::string>& lines);
    void finalize();

    const std::vector<ChannelDesc>& getChannels() const { return channels_; }
    int getInformativnost() const { return informativnost_; }
    int getGroupSize() const { return group_size_; }
    int getCycleSize() const { return cycle_size_; }

private:
    int informativnost_;
    int group_size_;
    int cycle_size_;
    std::vector<ChannelDesc> channels_;

    void finalizeChannel(ChannelDesc& desc);
    void fillGroupArray(ChannelDesc& desc, uint32_t first_elem, uint32_t step);
};

} // namespace orbita
