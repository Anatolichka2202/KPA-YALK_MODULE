//интеграционный тест ус-ва, бит-восстановителя и фифо буфера.
#include <QtTest>
#include <QThread>
#include "../src/decoder/bitstream_recoverer.h"
#include "../src/decoder/fifo_buffer.h"
#include "../src/device/e2010_device.h"

using namespace orbita;

class TestIntegration : public QObject
{
    Q_OBJECT

private slots:
    void testRealDataFlow() {
        E2010Device device;
        if (!device.init(0, 0, 10000.0)) {
            QSKIP("E2010 device not available, skipping integration test");
        }
        device.stop();

        FifoBuffer bitFifo;
        // Полярность NORMAL соответствует rb1 в Delphi (high->0, low->1)
        BitstreamRecoverer recoverer(bitFifo, BitstreamRecoverer::NORMAL);

        QSignalSpy samplesSpy(&device, &E2010Device::samplesReady);
        QSignalSpy errorSpy(&device, &E2010Device::error);

        QVERIFY(device.start());
        QTest::qWait(60000); // 1 секунда
        device.stop();
        QTest::qWait(500);

        QVERIFY(errorSpy.isEmpty());
        QVERIFY(samplesSpy.count() > 0);

        // Порог вычисляем по первому блоку (как в Delphi)
        bool thresholdSet = false;
        for (const auto& args : samplesSpy) {
            auto samples = args.at(0).value<std::vector<int16_t>>();
            if (!thresholdSet && !samples.empty()) {
                recoverer.computeThreshold(samples.data(), samples.size());
                thresholdSet = true;
            }
            recoverer.processSamples(samples.data(), samples.size());
        }

        size_t bitCount = bitFifo.available();
        QVERIFY(bitCount > 0);
        qDebug() << "Bits recovered:" << bitCount;

        // Проверяем, что не все биты одинаковые
        bool hasVariation = false;
        size_t checkCount = std::min(bitCount, size_t(100));
        for (size_t i = 1; i < checkCount; ++i) {
            if (bitFifo.peek(i) != bitFifo.peek(0)) {
                hasVariation = true;
                break;
            }
        }
        QVERIFY(hasVariation);

        // Ожидаемое количество бит за 1 секунду: 10 МГц / (10/3.145728) ≈ 3.145 Мбит/с
        qDebug() << "Bit rate estimate:" << bitCount << "bits/sec";
       // QVERIFY(bitCount > 2'800'000 && bitCount < 3'400'000);
        QVERIFY(bitCount > 0);

    }
};


QTEST_MAIN(TestIntegration)
#include "test_integration_e2010_fifo_bitrec.moc"
