#include <gtest/gtest.h>
#include "channel_registry.h"
#include "orbita_types.h"

using namespace orbita;

// ─── Хелперы ─────────────────────────────────────────────────────────────────
static ChannelDesc makeAnalog(int wordIndex = 0) {
    ChannelDesc d;
    d.adressType = ORBITA_ADDR_TYPE_ANALOG_10BIT;
    d.wordIndex  = wordIndex;
    return d;
}
static ChannelDesc makeContact(int wordIndex = 0, int bit = 1) {
    ChannelDesc d;
    d.adressType = ORBITA_ADDR_TYPE_CONTACT;
    d.wordIndex  = wordIndex;
    d.bitNumber  = static_cast<uint8_t>(bit);
    return d;
}
static ChannelDesc makeFast(int wordIndex = 0) {
    ChannelDesc d;
    d.adressType = ORBITA_ADDR_TYPE_FAST_1;
    d.wordIndex  = wordIndex;
    return d;
}

// ─── Тесты ───────────────────────────────────────────────────────────────────

TEST(ChannelRegistry, EmptyByDefault) {
    ChannelRegistry reg;
    EXPECT_FALSE(reg.hasChannels());
    EXPECT_EQ(reg.get(), nullptr);
}

// Главный тест: смешанная конфигурация, poolIndex должны быть per-type
TEST(ChannelRegistry, MixedTypesCorrectPoolIndex) {
    ChannelRegistry reg;
    // Порядок: аналог, контакт, аналог, быстрый, контакт
    std::vector<ChannelDesc> descs = {
        makeAnalog(10),    // → analog[0]
        makeContact(20,3), // → contact[0]
        makeAnalog(30),    // → analog[1]
        makeFast(40),      // → fast[0]
        makeContact(50,7), // → contact[1]
    };
    auto snap = reg.update(descs);

    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->analogCount,   2);
    EXPECT_EQ(snap->contactCount,  2);
    EXPECT_EQ(snap->fastCount,     1);
    EXPECT_EQ(snap->tempCount,     0);

    // Это был баг: при глобальном индексе i получалось:
    //   setAnalog(0,...) setContact(1,...) setAnalog(2,...) — неверно!
    // Теперь:
    EXPECT_EQ(snap->entries[0].poolIndex, 0);  // analog[0]   ✓
    EXPECT_EQ(snap->entries[1].poolIndex, 0);  // contact[0]  ✓ (не 1!)
    EXPECT_EQ(snap->entries[2].poolIndex, 1);  // analog[1]   ✓
    EXPECT_EQ(snap->entries[3].poolIndex, 0);  // fast[0]     ✓
    EXPECT_EQ(snap->entries[4].poolIndex, 1);  // contact[1]  ✓
}

// Горячая замена: старый снапшот остаётся валидным
TEST(ChannelRegistry, HotSwap_OldSnapshotStillValid) {
    ChannelRegistry reg;

    auto snap1 = reg.update({makeAnalog(10)});
    EXPECT_EQ(snap1->analogCount, 1);

    // Меняем конфигурацию на лету
    auto snap2 = reg.update({makeAnalog(10), makeAnalog(20), makeContact(30)});
    EXPECT_EQ(snap2->analogCount,  2);
    EXPECT_EQ(snap2->contactCount, 1);

    // snap1 всё ещё жив (shared_ptr) — декодер мог держать его
    EXPECT_EQ(snap1->analogCount, 1);   // ← не затёрт
    EXPECT_NE(snap1.get(), snap2.get());
}

// Пустой update сбрасывает каналы
TEST(ChannelRegistry, UpdateWithEmpty) {
    ChannelRegistry reg;
    reg.update({makeAnalog()});
    EXPECT_TRUE(reg.hasChannels());

    reg.update({});
    // Снапшот создаётся, но пустой
    auto snap = reg.get();
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->analogCount, 0);
    EXPECT_TRUE(snap->entries.empty());
}

// Имена и категории передаются корректно
TEST(ChannelRegistry, NamesAndCategories) {
    ChannelRegistry reg;
    std::vector<ChannelDesc> descs = { makeAnalog(5), makeAnalog(6) };
    std::vector<std::string>  names = { "Параметр А1", "Параметр А2" };
    std::vector<std::string>  cats  = { "БСИ МКА-1", "БСИ МКА-1" };

    auto snap = reg.update(descs, names, cats);
    EXPECT_EQ(snap->entries[0].name,     "Параметр А1");
    EXPECT_EQ(snap->entries[1].name,     "Параметр А2");
    EXPECT_EQ(snap->entries[0].category, "БСИ МКА-1");
}

// clear() должен делать get() == nullptr
TEST(ChannelRegistry, Clear) {
    ChannelRegistry reg;
    reg.update({makeAnalog()});
    reg.clear();
    EXPECT_EQ(reg.get(), nullptr);
    EXPECT_FALSE(reg.hasChannels());
}
