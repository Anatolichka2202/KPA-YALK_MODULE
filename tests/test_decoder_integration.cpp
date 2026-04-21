#include <QtTest>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <atomic>
#include <thread>
#include <chrono>

#include "../src/decoder/bitstream_recoverer.h"
#include "../src/decoder/fifo_buffer.h"
#include "../src/device/e2010_device.h"
#include "../src/decoder/frame_decoder_m16.h"

using namespace orbita;
using namespace std::chrono_literals;

class TestDecoderIntegration : public QObject
{
    Q_OBJECT

private slots:
    void testRealDecoding() {
        E2010Device device;
        if (!device.init(0, 0, 10000.0)) {
            QSKIP("E2010 device not available, skipping integration test");
        }
        device.stop();

        // Увеличим FIFO для безопасности, но всё равно будем вычитывать параллельно
        FifoBuffer bitFifo;
        BitstreamRecoverer recoverer(bitFifo, BitstreamRecoverer::NORMAL);
        FrameDecoderM16 decoder(bitFifo);

        std::atomic<int> groupCount{0};
        decoder.setGroupCallback([&](const std::vector<uint16_t>&,
                                     const std::vector<uint16_t>&) {
            ++groupCount;
        });

        QSignalSpy samplesSpy(&device, &E2010Device::samplesReady);
        QSignalSpy errorSpy(&device, &E2010Device::error);

        // Поток-обработчик: ждёт сэмплы и обрабатывает их
        std::atomic<bool> stopProcessing{false};
        std::jthread processor([&](std::stop_token stoken) {
            bool thresholdSet = false;
            while (!stoken.stop_requested()) {
                // Обрабатываем накопленные сигналы (можно использовать wait, но для простоты спим)
                if (samplesSpy.count() > 0) {
                    // Извлекаем все накопленные блоки
                    while (samplesSpy.count() > 0) {
                        auto args = samplesSpy.takeFirst();
                        auto samples = args.at(0).value<std::vector<int16_t>>();
                        if (!thresholdSet && !samples.empty()) {
                            recoverer.computeThreshold(samples.data(), samples.size());
                            thresholdSet = true;
                        }
                        recoverer.processSamples(samples.data(), samples.size());
                    }
                    // После добавления новых битов запускаем декодер
                    decoder.process();
                }
                std::this_thread::sleep_for(10ms);
            }
            // Финальная обработка остатков
            decoder.process();
        });

        QVERIFY(device.start());

        QElapsedTimer timer;
        timer.start();
        QTest::qWait(6000);  // 6 секунд
        qint64 elapsed = timer.elapsed();

        device.stop();
        stopProcessing = true;
        processor.request_stop();
        processor.join();

        QVERIFY(errorSpy.isEmpty());

        qDebug() << "Decoding finished. Groups received:" << groupCount.load();
        qDebug() << "Collection time (ms):" << elapsed;

        int phraseErr, groupErr;
        decoder.getErrors(phraseErr, groupErr);
        qDebug() << "Phrase error %:" << phraseErr;
        qDebug() << "Group error %:" << groupErr;

        // За 60 секунд должно быть около 188 млн бит / (2048*12) ≈ 7650 групп
        QVERIFY(groupCount.load() >= 100); // хотя бы 100 групп для начала
        QVERIFY(phraseErr <= 30);
        QVERIFY(groupErr <= 30);
    }
};

QTEST_MAIN(TestDecoderIntegration)
#include "test_decoder_integration.moc"
