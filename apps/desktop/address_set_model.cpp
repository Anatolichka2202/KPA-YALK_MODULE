#include "address_set_model.h"

#include <set>
#include <stdexcept>

AddressSetModel::AddressSetModel()
{
    // Первый набор создаётся сразу, чтобы добавление параметров из каталога
    // всегда имело однозначную цель. Кнопка «Добавить набор» создаёт второй и
    // последующие независимые наборы без остановки мониторинга.
    addSet(QStringLiteral("Набор 1"));
}

int AddressSetModel::count() const noexcept
{
    return static_cast<int>(sets_.size());
}

int AddressSetModel::activeIndex() const noexcept
{
    return activeIndex_;
}

const AddressSetDefinition& AddressSetModel::at(int index) const
{
    if (index < 0 || index >= count())
        throw std::out_of_range("Индекс набора адресов вне диапазона");
    return sets_[static_cast<std::size_t>(index)];
}

void AddressSetModel::setActiveIndex(int index)
{
    if (index < 0 || index >= count())
        throw std::out_of_range("Индекс набора адресов вне диапазона");
    activeIndex_ = index;
}

int AddressSetModel::addSet(const QString& title)
{
    const QString actualTitle = title.trimmed().isEmpty()
        ? QStringLiteral("Набор %1").arg(nextId_)
        : title.trimmed();
    const QString id = QStringLiteral("address-set-%1").arg(nextId_++);
    sets_.push_back({id, actualTitle, {}});
    activeIndex_ = count() - 1;
    return activeIndex_;
}

bool AddressSetModel::removeSet(int index)
{
    // Нельзя оставить свободный контур без активного набора: тогда действия из
    // библиотеки параметров не имели бы определённого получателя.
    if (count() <= 1 || index < 0 || index >= count())
        return false;

    sets_.erase(sets_.begin() + index);
    if (activeIndex_ >= count())
        activeIndex_ = count() - 1;
    else if (index < activeIndex_)
        --activeIndex_;
    return true;
}

void AddressSetModel::renameSet(int index, const QString& title)
{
    const QString actualTitle = title.trimmed();
    if (actualTitle.isEmpty())
        return;
    at(index);
    sets_[static_cast<std::size_t>(index)].title = actualTitle;
}

void AddressSetModel::replaceActive(const std::vector<orbita::ChannelSpec>& specs)
{
    at(activeIndex_);
    sets_[static_cast<std::size_t>(activeIndex_)].specs = specs;
}

bool AddressSetModel::containsInActive(const std::string& address) const
{
    const auto& active = at(activeIndex_).specs;
    for (const auto& spec : active) {
        if (spec.address == address)
            return true;
    }
    return false;
}

void AddressSetModel::addToActive(const std::vector<orbita::ChannelSpec>& specs)
{
    auto& active = sets_[static_cast<std::size_t>(activeIndex_)].specs;
    for (const auto& spec : specs) {
        if (spec.address.empty() || containsInActive(spec.address))
            continue;
        active.push_back(spec);
    }
}

std::vector<orbita::ChannelSpec> AddressSetModel::combinedSpecs() const
{
    std::vector<orbita::ChannelSpec> combined;
    std::set<std::string> seenAddresses;
    for (const auto& set : sets_) {
        for (const auto& spec : set.specs) {
            if (spec.address.empty() || !seenAddresses.insert(spec.address).second)
                continue;
            combined.push_back(spec);
        }
    }
    return combined;
}
