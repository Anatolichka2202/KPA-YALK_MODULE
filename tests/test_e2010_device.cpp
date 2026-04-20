#include <QtTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QThread>
#include "../src/device/e2010_device.h"

using namespace orbita;

class TestE2010Device : public QObject
{
    Q_OBJECT

public:
    TestE2010Device() = default;

private slots:
    void initTestCase() {
        qDebug() << "Starting E2010Device tests...";
    }

    void cleanupTestCase() {
        qDebug() << "All tests finished.";
    }

    void testInitAndStartStop() {
        E2010Device device;
        QSignalSpy errorSpy(&device, &E2010Device::error);

        QVERIFY(device.init(0, 0, 10000.0));
        QVERIFY(errorSpy.isEmpty());

        QVERIFY(device.start());
        QVERIFY(device.isRunning());

        QTest::qWait(500);

        device.stop();
        QTest::qWait(200);
        QVERIFY(!device.isRunning());
        QVERIFY(errorSpy.isEmpty());
    }

    void testDataAcquisition() {
        E2010Device device;
        QSignalSpy samplesSpy(&device, &E2010Device::samplesReady);
        QSignalSpy errorSpy(&device, &E2010Device::error);

        QVERIFY(device.init(0, 0, 10000.0));
        QVERIFY(device.start());

        QTest::qWait(2000);   // 2 секунды сбора данных

        device.stop();
        QTest::qWait(500);

        QVERIFY(samplesSpy.count() > 0);
        QVERIFY(errorSpy.isEmpty());

        size_t totalSamples = 0;
        for (const auto& args : samplesSpy) {
            auto samples = args.at(0).value<std::vector<int16_t>>();
            totalSamples += samples.size();
        }
        qDebug() << "Total samples received:" << totalSamples;
        QVERIFY(totalSamples > 0);
    }

    void testRestart() {
        E2010Device device;
        QSignalSpy errorSpy(&device, &E2010Device::error);

        for (int i = 0; i < 3; ++i) {
            QVERIFY(device.init(0, 0, 10000.0));
            QVERIFY(device.start());
            QTest::qWait(500);
            device.stop();
            QTest::qWait(200);
            QVERIFY(!device.isRunning());
            QVERIFY(errorSpy.isEmpty());
        }
    }

    void testInvalidChannel() {
        E2010Device device;
        QVERIFY(!device.init(0, 5, 10000.0));
    }

    void testNoDevice() {
        E2010Device device;
        bool ok = device.init(999, 0, 10000.0);
        Q_UNUSED(ok);
    }
};

QTEST_MAIN(TestE2010Device)
#include "test_e2010_device.moc"
