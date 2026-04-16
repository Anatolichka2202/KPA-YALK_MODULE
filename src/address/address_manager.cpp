#include "address_manager.h"
#include <stdexcept>
#include <algorithm>

namespace orbita {

static int groupSizeFromInf(int inf) {
    switch (inf) {
    case 16: return 2048;
    case 8:  return 1024;
    case 4:  return 512;
    case 2:  return 256;
    case 1:  return 128;
    default: throw std::runtime_error("Invalid informativnost");
    }
}

AddressManager::AddressManager(int informativnost)
    : informativnost_(informativnost)
    , group_size_(groupSizeFromInf(informativnost))
    , cycle_size_(group_size_ * 32)
{}

void AddressManager::addChannel(const ChannelDesc& desc) {
    channels_.push_back(desc);
}

void AddressManager::addChannels(const std::vector<ChannelDesc>& descs) {
    channels_.insert(channels_.end(), descs.begin(), descs.end());
}

void AddressManager::loadFromFile(const std::string& filename) {
    auto descs = AddressParser::loadFile(filename);
    addChannels(descs);
    finalize(); // автоматически заполняем группы/циклы
}

void AddressManager::loadFromLines(const std::vector<std::string>& lines) {
    auto descs = AddressParser::loadLines(lines);
    addChannels(descs);
    finalize();
}

void AddressManager::finalize() {
    for (auto& desc : channels_) {
        finalizeChannel(desc);
    }
}

void AddressManager::finalizeChannel(ChannelDesc& desc) {
    desc.groups.clear();
    desc.cycles.clear();

    uint32_t step = desc.stepOutG;
    uint32_t first = desc.numOutElemG;

    if (step > static_cast<uint32_t>(cycle_size_)) {
        // Шаг больше цикла – заполняем циклы
        uint32_t fElem = first;
        uint32_t fNumC = (fElem - 1) / cycle_size_ + 1;
        uint32_t sNumC = (step - 1) / cycle_size_;
        for (uint32_t cc = fNumC; cc <= 4; cc += sNumC) {
            desc.cycles.push_back(static_cast<uint16_t>(cc));
        }
        // После определения циклов, если шаг внутри цикла больше группы, заполняем группы
        uint32_t fElemGr = first - (fNumC - 1) * cycle_size_;
        if (step > static_cast<uint32_t>(group_size_)) {
            fillGroupArray(desc, fElemGr, step);
        }
    } else if (step > static_cast<uint32_t>(group_size_)) {
        // Шаг в пределах цикла, но больше группы – заполняем группы
        fillGroupArray(desc, first, step);
    }
    // Если шаг <= group_size, то ничего не заполняем – слово есть в каждой группе каждого цикла
}

void AddressManager::fillGroupArray(ChannelDesc& desc, uint32_t first_elem, uint32_t step) {
    desc.groups.clear();
    uint32_t fElem = first_elem;
    uint32_t fNumGr = (fElem - 1) / group_size_ + 1;
    uint32_t sNumGr = (step - 1) / group_size_;
    for (uint32_t gr = fNumGr; gr <= 32; gr += sNumGr) {
        desc.groups.push_back(static_cast<uint16_t>(gr));
    }
}

} // namespace orbita
