#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include "../src/device/e2010_device.h"
#include "../src/decoder/frame_decoder_m16.h"
#include "../src/decoder/bitstream_recoverer.h"

using namespace orbita;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // Настройки
    const double sample_rate = 10'000'000.0; // 10 МГц
    const double bit_rate = 3'145'728.0;     // 3.145728 Мбит/с для M16
    const bool invert = true;               // обратный режим

    // Устройство
    E2010Device device;
    if (!device.init(0, 0, sample_rate / 1000.0)) {
        qCritical() << "Failed to init E2010";
        return 1;
    }

    // Восстановитель битового потока
    BitstreamRecoverer::Config rec_cfg;
    rec_cfg.sample_rate_hz = sample_rate;
    rec_cfg.bit_rate_hz = bit_rate;
    rec_cfg.invert = invert;
    BitstreamRecoverer recoverer(rec_cfg);

    // Декодер M16
    auto decoder = std::make_unique<FrameDecoderM16>();
    decoder->setCallback([](const std::vector<uint16_t>& group) {
        qDebug() << "Group received, size =" << group.size();
    });

    // Соединяем цепочку
    QObject::connect(&device, &E2010Device::samplesReady,
                     [&](const std::vector<int16_t>& samples) {
                         recoverer.processSamples(samples.data(), samples.size(), nullptr);
                         size_t avail = recoverer.availableBits();
                         static int block_cnt = 0;
                         if (++block_cnt % 10 == 0) {
                             qDebug() << "Block" << block_cnt << "available bits:" << avail;
                         }
                         while (avail > 0) {
                             decoder->feedBit(recoverer.readBit());
                             --avail;
                         }
                     });
    device.start();
    qDebug() << "Stream started...";

    QTimer::singleShot(1000, [&]() {
        device.stop();
        qDebug() << "Stopped.";
        QCoreApplication::quit();
    });

    return app.exec();
}
