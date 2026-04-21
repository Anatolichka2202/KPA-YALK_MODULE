#include <QtTest>
#include <QDebug>
#include <vector>
#include <cstdint>

#include "../src/decoder/fifo_buffer.h"
#include "../src/decoder/frame_decoder_m16.h"

using namespace orbita;

class TestFrameDecoderM16 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { qDebug() << "Starting FrameDecoderM16 unit tests..."; }
    void cleanupTestCase() { qDebug() << "Unit tests finished."; }

    void fillFifo(FifoBuffer& fifo, const std::vector<uint8_t>& bits) {
        for (uint8_t b : bits) fifo.push(b);
    }

    // Маркерная последовательность из Delphi
    std::vector<uint8_t> generateMarkerBits() {
        std::vector<uint8_t> m(384, 0);
        m[24]=1; m[48]=1; m[72]=1; m[96]=1;
        m[216]=1; m[288]=1; m[312]=1; m[360]=1;
        return m;
    }

    // ------------------------------------------------------------
    void testSearchMarker_Found() {
        FifoBuffer fifo;
        FrameDecoderM16 decoder(fifo);

        auto marker = generateMarkerBits();
        fillFifo(fifo, marker);
        // больше ничего не добавляем

#ifdef TESTING
        decoder.publicSearchFirstFraseMarker();
        QVERIFY(decoder.isFirstFraseFound());
        QCOMPARE(decoder.getPointCount(), 383);
        // после отката rewind(1) в FIFO должно остаться 383 бита (было 384, один откатили)
        QCOMPARE(fifo.available(), size_t(383));
#endif
    }

    void testSearchMarker_NotFound() {
        FifoBuffer fifo;
        FrameDecoderM16 decoder(fifo);

        std::vector<uint8_t> wrong(384, 0);
        fillFifo(fifo, wrong);

#ifdef TESTING
        decoder.publicSearchFirstFraseMarker();
        QVERIFY(!decoder.isFirstFraseFound());
        QCOMPARE(fifo.available(), size_t(383)); // один бит pop'нут
#endif
    }

    // ------------------------------------------------------------
    void testAnalyseFrase_SingleWord() {
        FifoBuffer fifo;
        FrameDecoderM16 decoder(fifo);

        auto marker = generateMarkerBits();
        fillFifo(fifo, marker);
        // Слово: 12 бит, LSB first, значение 0x555, 12-й бит = 1
        std::vector<uint8_t> wordBits = {
            1,0,1,0,1,0,1,0,1,0,1,0   // LSB first: 0x555 = 0101 0101 0101
        };
        wordBits[11] = 1; // старший бит = 1
        fillFifo(fifo, wordBits);

#ifdef TESTING
        decoder.publicSearchFirstFraseMarker();   // синхронизация
        QVERIFY(decoder.isFirstFraseFound());
        decoder.publicAnalyseFrase();             // обработка одного слова

        // Проверяем, что слово обработано: word_num_ стал 2
        QCOMPARE(decoder.getWordNum(), 2);
        // Проверяем, что FIFO опустел (1 бит маркера + 12 бит слова = 13, все прочитаны)
        QCOMPARE(fifo.available(), size_t(0));
#endif
    }

    // ------------------------------------------------------------
    void testSyncLossOnBadMarker() {
        FifoBuffer fifo;
        FrameDecoderM16 decoder(fifo);

        auto marker = generateMarkerBits();
        fillFifo(fifo, marker);
        // 383 случайных бита
        for (int i=0; i<383; ++i) fifo.push(rand()%2);
        // Плохой маркер (все нули)
        std::vector<uint8_t> bad(384, 0);
        fillFifo(fifo, bad);

#ifdef TESTING
        decoder.publicSearchFirstFraseMarker();
        QVERIFY(decoder.isFirstFraseFound());
        // Обрабатываем 16 слов (384 бита) – последнее слово вызовет потерю синхронизации
        for (int w=0; w<16; ++w) {
            decoder.publicAnalyseFrase();
        }
        QVERIFY(!decoder.isFirstFraseFound());
#endif
    }

    // ------------------------------------------------------------
    void testPhraseErrorStatistics() {
        FifoBuffer fifo;
        FrameDecoderM16 decoder(fifo);

        for (int i=0; i<2000; ++i) fifo.push(rand()%2);

#ifdef TESTING
        // Вызываем поиск маркера вручную много раз, пока есть биты
        while (fifo.available() >= 384) {
            decoder.publicSearchFirstFraseMarker();
        }
        int p, g;
        decoder.getErrors(p, g);
        QVERIFY(p > 0);
        QCOMPARE(g, 0);
#endif
    }

    // ------------------------------------------------------------
    void testReset() {
        FifoBuffer fifo;
        FrameDecoderM16 decoder(fifo);

        auto marker = generateMarkerBits();
        fillFifo(fifo, marker);
        fifo.push(0); fifo.push(1); // чтобы process() не упирался в пустоту

        decoder.process(); // он найдёт маркер и обработает немного

        decoder.reset();

#ifdef TESTING
        QVERIFY(!decoder.isFirstFraseFound());
        QCOMPARE(decoder.getPointCount(), 0);
        QCOMPARE(decoder.getWordNum(), 1);
#endif
        int p, g;
        decoder.getErrors(p, g);
        QCOMPARE(p, 0);
        QCOMPARE(g, 0);
    }

    // ------------------------------------------------------------
    void testEmptyFifo() {
        FifoBuffer fifo;
        FrameDecoderM16 decoder(fifo);
        decoder.process();
        QVERIFY(fifo.empty());
    }
};

QTEST_MAIN(TestFrameDecoderM16)
#include "test_frame_decoder_m16.moc"
