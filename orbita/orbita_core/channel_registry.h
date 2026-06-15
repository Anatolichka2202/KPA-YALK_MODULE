/**
 * @file channel_registry.h
 * @brief Потокобезопасный реестр активных каналов с горячей заменой (RCU).
 *
 * Конфигурация каналов — неизменяемый Snapshot за shared_ptr:
 *   • UI-поток: update() создаёт новый snapshot и атомарно меняет указатель.
 *   • Декодерный поток: get() берёт shared_ptr (один lock) и итерирует без блокировок.
 *   • Старый snapshot живёт, пока его держит хоть один читатель.
 *
 * Адаптировано из рекомендаций коллеги: добавлено поле address (значения
 * выдаются наружу по адресу) и индекс по адресу для getValueByAddress.
 */

#pragma once

#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <unordered_map>
#include "../address/address_parser.h"   // ChannelDesc

namespace orbita {

class ChannelRegistry {
public:
    struct Entry {
        ChannelDesc desc;            ///< что декодировать (wordIndex, groups, cycles, type)
        std::string address;         ///< нормализованный адрес — ключ выдачи наружу
        std::string name;            ///< имя параметра (из БД/файла)
        std::string category;        ///< категория (из БД)
    };

    struct Snapshot {
        std::vector<Entry> entries;
        std::unordered_map<std::string, size_t> addrIndex;  ///< address → позиция в entries
    };

    ChannelRegistry() = default;

    /// Атомарно заменяет набор каналов. descs/addresses/names/cats — параллельные массивы.
    std::shared_ptr<const Snapshot> update(
        const std::vector<ChannelDesc>& descs,
        const std::vector<std::string>& addresses,
        const std::vector<std::string>& names = {},
        const std::vector<std::string>& cats  = {});

    /// Текущий снапшот (может быть nullptr, если каналы не настроены).
    std::shared_ptr<const Snapshot> get() const;

    bool hasChannels() const;
    void clear();

private:
    mutable std::mutex              mutex_;
    std::shared_ptr<const Snapshot> snapshot_;
};

} // namespace orbita
