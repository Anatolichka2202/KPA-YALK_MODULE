#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include "../src/device/e2010_device.h"
#include "../src/decoder/frame_decoder_m16.h"
#include "../src/decoder/bitstream_recoverer.h"

#include "address/address_manager.h"
#include "value/value_converter.h"
#include "../src/decoder/fifo_buffer.h"
using namespace orbita;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // Настройки
    const double sample_rate = 10'000'000.0; // 10 МГц
    const double bit_rate = 3'145'728.0;     // 3.145728 Мбит/с для M16
    const bool invert = false;               // обратный режим

    // Устройство
    E2010Device device;
    if (!device.init(0, 0, sample_rate / 1000.0)) {
        qCritical() << "Failed to init E2010";
        return 1;
    }

    // Восстановитель битового потока
    FifoBuffer bitFifo;
    BitstreamRecoverer::Config cfg;
    cfg.sample_rate_hz = sample_rate;
    cfg.bit_rate_hz    = bit_rate;
    cfg.invert         = invert;
    BitstreamRecoverer recoverer(cfg, bitFifo);

    // Декодер M16
    auto decoder = std::make_unique<FrameDecoderM16>();
    decoder->setCallback([](const std::vector<uint16_t>& group) {
        qDebug() << "Group received, size =" << group.size();
    });

    AddressManager addrMgr(16); // M16
    addrMgr.loadFromFile("../../data/BTAadress.txt"); // путь к файлу адресов

    decoder->setCallback([&](const std::vector<uint16_t>& group) {
        // Извлекаем значения для всех каналов
        for (const auto& ch : addrMgr.getChannels()) {
            // Вычисляем индекс слова в группе с учётом текущих счётчиков (group_num, cikl_num)
            // Пока для простоты будем брать первое вхождение (без учёта циклов/групп)
            size_t wordIndex = ch.numOutElemG - 1; // индексация с 0
            if (wordIndex < group.size()) {
                uint16_t raw = group[wordIndex];
                double value = orbita::analog10bitToVoltage(raw, ch.adressType);
                //qDebug() << "Channel" << ch.adressType << "raw:" << raw << "value:" << value;
            }
        }
    });

    // Соединяем цепочку
    QObject::connect(&device, &E2010Device::samplesReady,
                     [&](const std::vector<int16_t>& samples) {
                         recoverer.processSamples(samples.data(), samples.size());
                         size_t avail = bitFifo.available();
                         static int block_cnt = 0;
                         if (++block_cnt % 10 == 0) {
                             qDebug() << "Block" << block_cnt << "available bits:" << avail;
                         }
                         while (avail > 0) {
                             decoder->feedBit(bitFifo.pop());
                             --avail;
                         }
                     });
    device.start();
    qDebug() << "Stream started...";

    QTimer::singleShot(15000, [&]() {
        device.stop();
        qDebug() << "Stopped.";
        QCoreApplication::quit();
    });

    return app.exec();
}
