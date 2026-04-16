#include <QtTest>
#include "address/address_manager.h"

using namespace orbita;

class TestAddressManager : public QObject
{
    Q_OBJECT
private slots:
    void testFinalizeSlowWithCycles();
    void testFinalizeContactWithGroups();
};

void TestAddressManager::testFinalizeSlowWithCycles()
{
    AddressManager mgr(16);
    ChannelDesc desc;
    desc.numOutElemG = 1;
    desc.stepOutG = 65536 * 2; // больше цикла
    desc.adressType = ORBITA_ADDR_TYPE_ANALOG_10BIT;
    mgr.addChannel(desc);
    mgr.finalize();

    const auto& channels = mgr.getChannels();
    QVERIFY(!channels.empty());
    QVERIFY(channels[0].flagCikl);
    QCOMPARE(channels[0].cycles.size(), 4u); // циклы 1,2,3,4
    QCOMPARE(channels[0].cycles[0], 1);
    QCOMPARE(channels[0].cycles[3], 4);
}

void TestAddressManager::testFinalizeContactWithGroups()
{
    AddressManager mgr(16);
    ChannelDesc desc;
    desc.numOutElemG = 2049; // вторая группа
    desc.stepOutG = 2048;    // шаг на целую группу
    desc.adressType = ORBITA_ADDR_TYPE_CONTACT;
    mgr.addChannel(desc);
    mgr.finalize();

    const auto& channels = mgr.getChannels();
    QVERIFY(channels[0].flagGroup);
    QCOMPARE(channels[0].groups.size(), 32u); // группы 2,3,...,32,1? зависит от реализации
}

QTEST_MAIN(TestAddressManager)
