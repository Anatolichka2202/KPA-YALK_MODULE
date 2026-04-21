// тест восстановителя на реальном сигнале
#include <QtTest>
#include <QThread>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include "../src/device/e2010_device.h"

#include "../src/decoder/bitstream_recoverer.h"
#include "../src/decoder/fifo_buffer.h"

using namespace orbita;

class TestBitRecovererReal : public QObject
{
    Q_OBJECT

private slots:
    void testRealData() {
        E2010Device device;

        if (!device.init(0, 1, 10000.0)) {
            QSKIP("E2010 device not available, skipping test");
        }
        device.stop();

        FifoBuffer bitFifo;
        BitstreamRecoverer recoverer(bitFifo, BitstreamRecoverer::NORMAL);
        device.setRecoverer(&recoverer);
        QSignalSpy samplesSpy(&device, &E2010Device::samplesReady);
        QSignalSpy errorSpy(&device, &E2010Device::error);

       // QVERIFY(device.init(0, 0, 10000.0));

        QVERIFY(device.start());

        // Собираем данные в течение 2 секунд
        QElapsedTimer timer;
        timer.start();
        QTest::qWait(2000);
        qint64 elapsed = timer.elapsed();

        device.stop();
        QTest::qWait(600);

        QVERIFY(errorSpy.isEmpty());
        QVERIFY(samplesSpy.count() > 0);

        // Порог вычисляем по первому блоку
        bool thresholdSet = false;
        int blockIndex = 0;
        bool firstBlockSaved = false; // флаг, чтобы сохранить только первый блок
        for (const auto& args : samplesSpy) {
            auto samples = args.at(0).value<std::vector<int16_t>>();
            if (!thresholdSet && !samples.empty()) {
                recoverer.computeThreshold(samples.data(), samples.size());
                thresholdSet = true;
            }
            recoverer.processSamples(samples.data(), samples.size());

            // Сохраняем первый блок сырых отсчётов в CSV
            if (!firstBlockSaved && !samples.empty()) {
                QFile raw("raw_samples.csv");
                if (raw.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    QTextStream out(&raw);
                    for (int16_t s : samples) {
                        out << s << "\n";
                    }
                    raw.close();
                    qDebug() << "Saved" << samples.size() << "raw samples to raw_samples.csv";
                }
                firstBlockSaved = true;
            }
        }
        size_t bitCount = bitFifo.available();
        qDebug() << "Bits recovered:" << bitCount;
        qDebug() << "Time (ms):" << elapsed;
        qDebug() << "Bit rate (bits/sec):" << (bitCount * 1000.0 / elapsed);

        QVERIFY(bitCount > 0);

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

        // Опционально: сохранить поток
        QFile bitFile("full_bitstream.txt");
        if (bitFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&bitFile);
            size_t totalBits = bitFifo.available();
            for (size_t i = 0; i < totalBits; ++i) {
                stream << bitFifo.peek(i);
            }
            stream << "\n";
            bitFile.close();
            qDebug() << "Saved" << totalBits << "bits to full_bitstream.txt";
        }

    }
};

QTEST_MAIN(TestBitRecovererReal)
#include "test_bitrecoverer_real.moc"
