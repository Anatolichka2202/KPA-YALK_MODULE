#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>
#include "../src/decoder/bitstream_recoverer.h"
#include "../src/decoder/fifo_buffer.h"

#include <QtTest>

using namespace orbita;

class TestBitstreamRecoverer : public QObject
{
    Q_OBJECT
private slots:
    void testSyntheticSignal() {
        // Генерируем сигнал, где каждый бит представлен 3 отсчётами (имитация)
        std::vector<uint8_t> original = {1,0,1,0,1,0,1,0,1,0,1,1,0,0,1,1,0,0,1,0};
        std::vector<int16_t> samples;
        const int16_t low = 100, high = 900;
        const int points_per_bit = 3;
        for (uint8_t bit : original) {
            int16_t val = bit ? high : low;
            for (int i = 0; i < points_per_bit; ++i) samples.push_back(val);
        }

        FifoBuffer fifo;
        BitstreamRecoverer recoverer(fifo, BitstreamRecoverer::NORMAL);
        recoverer.computeThreshold(samples.data(), samples.size());
        recoverer.processSamples(samples.data(), samples.size());

        std::vector<uint8_t> recovered;
        while (fifo.available() > 0) recovered.push_back(fifo.pop());

        QCOMPARE(recovered.size(), original.size());
        for (size_t i = 0; i < original.size(); ++i)
            QCOMPARE(recovered[i], original[i]);
    }

    void testPolarity() {
        std::vector<uint8_t> original = {1,0,1,0,1};
        std::vector<int16_t> samples;
        const int16_t low = 100, high = 900;
        const int points_per_bit = 3;
        for (uint8_t bit : original) {
            int16_t val = bit ? high : low;
            for (int i = 0; i < points_per_bit; ++i) samples.push_back(val);
        }

        FifoBuffer fifo;
        BitstreamRecoverer recoverer(fifo, BitstreamRecoverer::INVERTED);
        recoverer.computeThreshold(samples.data(), samples.size());
        recoverer.processSamples(samples.data(), samples.size());

        std::vector<uint8_t> recovered;
        while (fifo.available() > 0) recovered.push_back(fifo.pop());

        for (size_t i = 0; i < original.size(); ++i)
            QCOMPARE(recovered[i], static_cast<uint8_t>(1 - original[i]));
    }

    void testLongRun() {
        // 1000 одинаковых битов – проверим, что восстановление не теряет биты
        std::vector<uint8_t> original(1000, 1);
        std::vector<int16_t> samples;
        const int16_t low = 100, high = 900;
        const int points_per_bit = 3;
        for (uint8_t bit : original) {
            int16_t val = bit ? high : low;
            for (int i = 0; i < points_per_bit; ++i) samples.push_back(val);
        }

        FifoBuffer fifo;
        BitstreamRecoverer recoverer(fifo, BitstreamRecoverer::NORMAL);
        recoverer.computeThreshold(samples.data(), samples.size());
        recoverer.processSamples(samples.data(), samples.size());

        size_t recovered_count = fifo.available();
        QVERIFY(std::abs(static_cast<int>(recovered_count - original.size())) <= 2);
    }
};

QTEST_MAIN(TestBitstreamRecoverer)
#include "test_bitrecoverer.moc"
