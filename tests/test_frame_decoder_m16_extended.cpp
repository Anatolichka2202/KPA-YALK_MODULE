#include <QtTest>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <vector>

#include "../src/decoder/fifo_buffer.h"
#include "../src/decoder/frame_decoder_m16.h"

using namespace orbita;

class TestFrameDecoderM16Extended : public QObject
{
    Q_OBJECT

private slots:
    void testFromFile() {
        // Открываем файл с битами
        QFile file("../../data/full_bitstream.txt");
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QSKIP("Bitstream file not found, skipping extended test.");
        }

        QTextStream stream(&file);
        QString content = stream.readAll();
        file.close();

        // Преобразуем символы '0'/'1' в биты
        FifoBuffer fifo;
        for (QChar ch : content) {
            if (ch == '0') fifo.push(0);
            else if (ch == '1') fifo.push(1);
            // остальные игнорируем
        }

        FrameDecoderM16 decoder(fifo);

        int groupCount = 0;
        decoder.setGroupCallback([&](const std::vector<uint16_t>&, const std::vector<uint16_t>&) {
            ++groupCount;
        });

        // Запускаем обработку всего FIFO
        decoder.process();

        int phraseErr, groupErr;
        decoder.getErrors(phraseErr, groupErr);

        qDebug() << "File test: groups =" << groupCount
                 << "phrase errors % =" << phraseErr
                 << "group errors % =" << groupErr;

        // Ожидаем, что найдено хотя бы несколько групп (зависит от длины файла)
        QVERIFY(groupCount > 0);
        QVERIFY(phraseErr < 50);
        QVERIFY(groupErr < 50);
    }
};

QTEST_MAIN(TestFrameDecoderM16Extended)
#include "test_frame_decoder_m16_extended.moc"
