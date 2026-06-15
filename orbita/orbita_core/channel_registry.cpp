#include "channel_registry.h"

namespace orbita {

std::shared_ptr<const ChannelRegistry::Snapshot>
ChannelRegistry::update(const std::vector<ChannelDesc>& descs,
                        const std::vector<std::string>& addresses,
                        const std::vector<std::string>& names,
                        const std::vector<std::string>& cats)
{
    auto snap = std::make_shared<Snapshot>();
    snap->entries.reserve(descs.size());

    for (size_t i = 0; i < descs.size(); ++i) {
        Entry e;
        e.desc = descs[i];
        if (i < addresses.size()) e.address  = addresses[i];
        if (i < names.size())     e.name     = names[i];
        if (i < cats.size())      e.category = cats[i];

        if (!e.address.empty())
            snap->addrIndex.emplace(e.address, snap->entries.size());
        snap->entries.push_back(std::move(e));
    }

    std::shared_ptr<const Snapshot> result = snap;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = result;
    }
    return result;
}

std::shared_ptr<const ChannelRegistry::Snapshot>
ChannelRegistry::get() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
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
