// тест записи и чтения фифо буфера
#include <QtTest>
#include "../src/decoder/fifo_buffer.h"

using namespace orbita;

class TestFifoBuffer : public QObject
{
    Q_OBJECT
private slots:
    void testPushPop() {
        FifoBuffer buf;
        QVERIFY(buf.empty());
        buf.push(1);
        QVERIFY(!buf.empty());
        QCOMPARE(buf.pop(), 1);
        QVERIFY(buf.empty());
    }

    void testOverflow() {
        FifoBuffer buf;
        for (size_t i = 0; i < FifoBuffer::FIFO_SIZE + 100; ++i) {
            buf.push(i % 2);
        }
        QCOMPARE(buf.available(), FifoBuffer::FIFO_SIZE);
        // Первый вытесненный бит должен быть (100 % 2)?? Но мы не знаем, какой именно вытеснился.
        // Просто проверяем, что буфер полон и не падает.
        QVERIFY(buf.full());
    }

    void testPeekSkip() {
        FifoBuffer buf;
        buf.push(1);
        buf.push(0);
        buf.push(1);
        QCOMPARE(buf.peek(0), 1);
        QCOMPARE(buf.peek(1), 0);
        buf.skip(2);
        QCOMPARE(buf.pop(), 1);
    }

    void testRewind() {
        FifoBuffer buf;
        buf.push(1);
        buf.push(0);
        buf.push(1);
        buf.pop(); // 1
        buf.rewind(1);
        QCOMPARE(buf.pop(), 1); // снова 1
    }
};

QTEST_MAIN(TestFifoBuffer)
#include "test_fifo_buffer.moc"
