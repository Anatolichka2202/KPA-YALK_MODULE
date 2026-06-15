/**
 * @file channel_registry.h
 * @brief Потокобезопасный реестр активных каналов с горячей заменой.
 *
 * Проблемы которые решает этот класс:
 *
 * 1. ГОРЯЧАЯ ЗАМЕНА: оператор меняет конфигурацию на лету, пока декодер
 *    работает. Прежний код писал channels_ без мьютекса → UB.
 *
 * 2. НЕВЕРНАЯ ИНДЕКСАЦИЯ: в onDecoderGroup использовался глобальный
 *    индекс i для data_pool_->setAnalog(i,...), но DataPool хранит
 *    аналоговые/контактные и пр. в раздельных векторах. Поле poolIndex
 *    хранит правильный per-type индекс, вычисленный один раз при update().
 *
 * Принцип:
 *   • UI-поток вызывает update() → создаётся новый shared_ptr<Snapshot>
 *   • Декодерный поток вызывает get() → получает shared_ptr (один lock)
 *   • Декодер держит snapshot за время обработки одной группы
 *   • Старый snapshot разрушается когда последний держатель его отпускает
 */

#pragma once

#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include "address/address_parser.h"   // ChannelDesc
#include "orbita_types.h"             // ORBITA_ADDR_TYPE_*

namespace orbita {

class ChannelRegistry {
public:
    // ─── Одна запись в реестре ────────────────────────────────────────────
    struct Entry {
        ChannelDesc desc;            ///< полное описание канала (wordIndex, groups, ...)
        int         poolIndex = -1;  ///< индекс в соответствующем массиве DataPool
        std::string name;            ///< имя параметра из БД / файла конфига
        std::string category;        ///< категория из БД
    };

    // ─── Снапшот — неизменяемое состояние на момент apply ────────────────
    struct Snapshot {
        std::vector<Entry> entries;
        int analogCount   = 0;
        int contactCount  = 0;
        int fastCount     = 0;
        int tempCount     = 0;
        int busCount      = 0;
    };

    ChannelRegistry() = default;

    /**
     * @brief Атомарно заменяет набор каналов.
     *
     * Безопасно вызывать из UI-потока пока декодер работает.
     * @param descs  уже финализированные ChannelDesc (после AddressManager::finalize)
     * @param names  имена параметров, порядок соответствует descs
     * @param cats   категории, порядок соответствует descs
     * @return новый снапшот (caller использует его для resize DataPool)
     */
    std::shared_ptr<const Snapshot> update(
        const std::vector<ChannelDesc>& descs,
        const std::vector<std::string>& names = {},
        const std::vector<std::string>& cats  = {});

    /**
     * @brief Получить текущий снапшот.
     *
     * Один lock, затем копия shared_ptr — декодерный поток держит
     * snap пока обрабатывает группу. Нет блокировок на время итерации.
     */
    std::shared_ptr<const Snapshot> get() const;

    /// Есть ли хоть один настроенный канал?
    bool hasChannels() const;

    /// Сбросить (например при stop())
    void clear();

private:
    mutable std::mutex              mutex_;
    std::shared_ptr<const Snapshot> snapshot_;

    static std::shared_ptr<Snapshot> buildSnapshot(
        const std::vector<ChannelDesc>& descs,
        const std::vector<std::string>& names,
        const std::vector<std::string>& cats);
};

} // namespace orbita
