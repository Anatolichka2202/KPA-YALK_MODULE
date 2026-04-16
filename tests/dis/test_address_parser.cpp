#include <QtTest>
#include "address/address_parser.h"
#include "include/orbita_types.h"

using namespace orbita;

class TestAddressParser : public QObject
{
    Q_OBJECT
private slots:
    void testM16P1Analog10bit();
    void testM16P2Analog10bit();
    void testM08Analog9bit();
    void testContact();
    void testFastT21();
    void testFastT22();
    void testTemperature();
    void testBus();
};

void TestAddressParser::testM16P1Analog10bit()
{
    ChannelDesc desc = AddressParser::parseLine("M16P1A70B12C10D10T01");
    // M16, P1, A7K0, B1K2, C1K0, D1K0, T01
    // offset = (7-1)*8 + (1-1)*4? нет, B1K2 означает номер 1, коэф 2 → множитель 2
    // Расчёт вручную:
    // A7K0: val=7, koef=0 → Fa=8, stepOutGins=8, offset += (7-1)*1 = 6
    // B1K2: val=1, koef=2 → Fb=2, offset += (1-1)*8 = 0, stepOutGins = 8*2=16
    // C1K0: val=1, koef=0 → Fc=8, offset += (1-1)*16 = 0, stepOutGins = 16*8=128
    // D1K0: val=1, koef=0 → Fd=8, offset += (1-1)*128 = 0, stepOutGins = 128*8=1024
    // Итоговый offset = 6
    // numOutElemG = pBeginOffset + 2*offset = 1 + 12 = 13
    // stepOutG = 2 * stepOutGins = 2*1024 = 2048
    QCOMPARE(desc.numOutElemG, 13u);
    QCOMPARE(desc.stepOutG, 2048u);
    QCOMPARE(desc.adressType, ORBITA_ADDR_TYPE_ANALOG_10BIT);
}

void TestAddressParser::testM16P2Analog10bit()
{
    ChannelDesc desc = AddressParser::parseLine("M16P2A1B1C1T01");
    // P2, все множители минимальные
    QCOMPARE(desc.numOutElemG, 2u);  // 2 + 2*0
    QCOMPARE(desc.stepOutG, 2u);
}

void TestAddressParser::testM08Analog9bit()
{
    ChannelDesc desc = AddressParser::parseLine("M08A1B1T01-01");
    QCOMPARE(desc.numOutElemG, 1u);
    QCOMPARE(desc.stepOutG, 1u);
    QCOMPARE(desc.adressType, ORBITA_ADDR_TYPE_ANALOG_9BIT);
}

void TestAddressParser::testContact()
{
    ChannelDesc desc = AddressParser::parseLine("M16P1A1B1T05P05");
    QCOMPARE(desc.adressType, ORBITA_ADDR_TYPE_CONTACT);
    QCOMPARE(desc.bitNumber, 5);
}

void TestAddressParser::testFastT21()
{
    ChannelDesc desc = AddressParser::parseLine("M16P1A1B1T21");
    QCOMPARE(desc.adressType, ORBITA_ADDR_TYPE_FAST_1);
}

void TestAddressParser::testFastT22()
{
    ChannelDesc desc = AddressParser::parseLine("M16P1A1B1T22");
    QCOMPARE(desc.adressType, ORBITA_ADDR_TYPE_FAST_2);
}

void TestAddressParser::testTemperature()
{
    ChannelDesc desc = AddressParser::parseLine("M16P1A1B1T11");
    QCOMPARE(desc.adressType, ORBITA_ADDR_TYPE_TEMPERATURE);
}

void TestAddressParser::testBus()
{
    ChannelDesc desc = AddressParser::parseLine("M16P1A1B1T25");
    QCOMPARE(desc.adressType, ORBITA_ADDR_TYPE_BUS);
}

QTEST_MAIN(TestAddressParser)
