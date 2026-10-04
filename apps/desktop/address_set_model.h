#pragma once

/**
 * @file address_set_model.h
 * @brief Модель нескольких независимых наборов адресов свободного контура.
 *
 * Один физический источник телеметрии получает объединение адресов всех
 * включённых наборов. Границы наборов при этом сохраняются для интерфейса и
 * последующего сопоставления показаний, а не теряются в одном списке.
 */

#include "orbita.h"

#include <QString>
#include <vector>

struct AddressSetDefinition {
    QString id;
    QString title;
    std::vector<orbita::ChannelSpec> specs;
};

class AddressSetModel final {
public:
    AddressSetModel();

    int count() const noexcept;
    int activeIndex() const noexcept;
    const AddressSetDefinition& at(int index) const;

    void setActiveIndex(int index);
    int addSet(const QString& title = {});
    bool removeSet(int index);
    void renameSet(int index, const QString& title);
    void replaceActive(const std::vector<orbita::ChannelSpec>& specs);
    void addToActive(const std::vector<orbita::ChannelSpec>& specs);
    std::vector<orbita::ChannelSpec> combinedSpecs() const;

private:
    bool containsInActive(const std::string& address) const;

    std::vector<AddressSetDefinition> sets_;
    int activeIndex_ = 0;
    int nextId_ = 1;
};
