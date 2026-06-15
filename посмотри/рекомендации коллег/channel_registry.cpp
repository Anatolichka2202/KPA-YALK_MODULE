#include "channel_registry.h"
#include <algorithm>

namespace orbita {

// ─────────────────────────────────────────────────────────────────────────────
// Приватная статика: строим снапшот с правильными poolIndex
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<ChannelRegistry::Snapshot>
ChannelRegistry::buildSnapshot(const std::vector<ChannelDesc>& descs,
                                const std::vector<std::string>& names,
                                const std::vector<std::string>& cats)
{
    auto snap = std::make_shared<Snapshot>();
    snap->entries.reserve(descs.size());

    for (size_t i = 0; i < descs.size(); ++i) {
        Entry e;
        e.desc = descs[i];
        if (i < names.size()) e.name     = names[i];
        if (i < cats.size())  e.category = cats[i];

        // Назначаем per-type индекс — именно это исправляет баг с setAnalog(i,...)
        switch (e.desc.adressType) {
        case ORBITA_ADDR_TYPE_ANALOG_10BIT:
        case ORBITA_ADDR_TYPE_ANALOG_9BIT:
            e.poolIndex = snap->analogCount++;
            break;
        case ORBITA_ADDR_TYPE_CONTACT:
            e.poolIndex = snap->contactCount++;
            break;
        case ORBITA_ADDR_TYPE_FAST_1:
        case ORBITA_ADDR_TYPE_FAST_2:
        case ORBITA_ADDR_TYPE_FAST_3:
        case ORBITA_ADDR_TYPE_FAST_4:
            e.poolIndex = snap->fastCount++;
            break;
        case ORBITA_ADDR_TYPE_TEMPERATURE:
            e.poolIndex = snap->tempCount++;
            break;
        case ORBITA_ADDR_TYPE_BUS:
            e.poolIndex = snap->busCount++;
            break;
        default:
            e.poolIndex = -1;   // неизвестный тип — пропускаем в onDecoderGroup
            break;
        }

        snap->entries.push_back(std::move(e));
    }
    return snap;
}

// ─────────────────────────────────────────────────────────────────────────────
// Публичный API
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<const ChannelRegistry::Snapshot>
ChannelRegistry::update(const std::vector<ChannelDesc>& descs,
                        const std::vector<std::string>& names,
                        const std::vector<std::string>& cats)
{
    auto snap = buildSnapshot(descs, names, cats);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = snap;
    }
    return snap;
}

std::shared_ptr<const ChannelRegistry::Snapshot>
ChannelRegistry::get() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;   // копируем shared_ptr — O(1), без копирования данных
}

bool ChannelRegistry::hasChannels() const
{
    auto snap = get();
    return snap && !snap->entries.empty();
}

void ChannelRegistry::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.reset();
}

} // namespace orbita
