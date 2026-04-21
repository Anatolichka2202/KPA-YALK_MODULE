#include <QtTest>
#include "../src/decoder/bitstream_recoverer.h"
#include "../src/decoder/fifo_buffer.h"

using namespace orbita;

class TestBitstreamRecoverer : public QObject
{
    Q_OBJECT
private slots:
    void testSyntheticSignal() {
        const double freqRatio = 10.0 / 3.145728; // ≈3.179
        std::vector<uint8_t> original = {1,0,1,0,1,0,1,0,1,0,1,1,0,0,1,1,0,0,1,0};
        std::vector<int16_t> samples;
        const int16_t low = 100, high = 900;
        double phase = 0.0;
        for (uint8_t bit : original) {
            int16_t val = bit ? high : low;
            int nSamples = static_cast<int>(std::floor(phase + freqRatio) - std::floor(phase));
            phase = std::fmod(phase + freqRatio, 1.0);
            for (int i = 0; i < nSamples; ++i) samples.push_back(val);
        }
        FifoBuffer fifo;
        BitstreamRecoverer recoverer(fifo, BitstreamRecoverer::NORMAL); // или INVERTED – какая разница
        recoverer.computeThreshold(samples.data(), samples.size(), samples.size() / 10);
        recoverer.processSamples(samples.data(), samples.size());
        recoverer.flush(); // обязательно!
        std::vector<uint8_t> recovered;
        while (fifo.available() > 0) recovered.push_back(fifo.pop());
        // проверяем, что recovered совпадает с original с точностью до 1 бита
        QVERIFY(std::abs((int)recovered.size() - (int)original.size()) <= 1);
        size_t match = 0;
        for (size_t i = 0; i < std::min(original.size(), recovered.size()); ++i)
            if (original[i] == recovered[i]) match++;
        QVERIFY(match > 0.95 * original.size());
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

        // NORMAL: high->0, low->1
        {
            FifoBuffer fifo;
            BitstreamRecoverer recoverer(fifo, BitstreamRecoverer::NORMAL);
            recoverer.computeThreshold(samples.data(), samples.size(), samples.size());
            recoverer.processSamples(samples.data(), samples.size());
            recoverer.flush();
            std::vector<uint8_t> recovered;
            while (fifo.available() > 0) recovered.push_back(fifo.pop());
            for (size_t i = 0; i < original.size(); ++i)
                QCOMPARE(recovered[i], static_cast<uint8_t>(1 - original[i]));
        }
        // INVERTED: high->1, low->0
        {
            FifoBuffer fifo;
            BitstreamRecoverer recoverer(fifo, BitstreamRecoverer::INVERTED);
            recoverer.computeThreshold(samples.data(), samples.size(), samples.size());
            recoverer.processSamples(samples.data(), samples.size());
            recoverer.flush();
            std::vector<uint8_t> recovered;
            while (fifo.available() > 0) recovered.push_back(fifo.pop());
            for (size_t i = 0; i < original.size(); ++i)
                QCOMPARE(recovered[i], original[i]);
        }
    }

    void testLongRun() {
        std::vector<uint8_t> bits;
        for (int i = 0; i < 500; ++i) bits.push_back(1);
        for (int i = 0; i < 500; ++i) bits.push_back(0);

        std::vector<int16_t> samples;
        const int16_t low = 1000, high = 5000;
        const double ratio = 10.0 / 3.145728;
        double phase = 0.0;
        for (uint8_t bit : bits) {
            int16_t val = bit ? high : low;
            int n = static_cast<int>(std::floor(phase + ratio) - std::floor(phase));
            phase = std::fmod(phase + ratio, 1.0);
            for (int j = 0; j < n; ++j) samples.push_back(val);
        }

        FifoBuffer fifo;
        BitstreamRecoverer recoverer(fifo, BitstreamRecoverer::NORMAL);
        recoverer.computeThreshold(samples.data(), samples.size(), samples.size() / 10);
        recoverer.processSamples(samples.data(), samples.size());
        recoverer.flush();

        std::vector<uint8_t> recovered;
        while (fifo.available() > 0) recovered.push_back(fifo.pop());

        QVERIFY(recovered.size() >= bits.size() - 2);
        QVERIFY(recovered.size() <= bits.size() + 2);
        for (size_t i = 0; i < std::min(bits.size(), recovered.size()); ++i)
            QCOMPARE(recovered[i], bits[i]);
    }

    void testRealisticSignal() {
        const int16_t low = 1050, high = 5500;
        const double freqRatio = 10.0 / 3.145728;
        const int numBits = 1000;

        std::vector<int16_t> samples;
        std::vector<uint8_t> original;

        srand(12345);
        double phase = 0.0;
        for (int i = 0; i < numBits; ++i) {
            uint8_t bit = rand() % 2;
            original.push_back(bit);
            int16_t val = bit ? high : low;
            int nSamples = static_cast<int>(std::floor(phase + freqRatio) - std::floor(phase));
            phase = std::fmod(phase + freqRatio, 1.0);
            for (int j = 0; j < nSamples; ++j) {
                int16_t noisy = val + (rand() % 11 - 5);
                samples.push_back(noisy);
            }
        }

        FifoBuffer fifo;
        BitstreamRecoverer recoverer(fifo, BitstreamRecoverer::NORMAL);
        recoverer.computeThreshold(samples.data(), samples.size(), samples.size() / 10);
        recoverer.processSamples(samples.data(), samples.size());
        recoverer.flush();

        std::vector<uint8_t> recovered;
        while (fifo.available() > 0) recovered.push_back(fifo.pop());

        QVERIFY(abs((int)recovered.size() - (int)original.size()) <= 2);
        size_t matchCount = 0;
        for (size_t i = 0; i < std::min(original.size(), recovered.size()); ++i)
            if (original[i] == recovered[i]) matchCount++;
        double matchRate = (double)matchCount / std::min(original.size(), recovered.size());
        QVERIFY(matchRate > 0.99);
    }
};

QTEST_MAIN(TestBitstreamRecoverer)
#include "test_bitrecoverer.moc"
