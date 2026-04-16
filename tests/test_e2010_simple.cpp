//тест проверки работы устройства
//ПРОВЕРЯЕМ РАБОТУ ДРАЙВЕРА И УСРОЙСТВА.
//никаких связок.

#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include "../src/device/e2010_device.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    E2010Device device;
    qDebug() << "Initializing E2010Device...";
    if (!device.init(0, 10000.0)) {
        qCritical() << "Failed to init device";
        return 1;
    }
    qDebug() << "Init OK";

    int sampleCount = 0;
    QObject::connect(&device, &E2010Device::samplesReady,
                     [&](const std::vector<int16_t>& samples) {
                         sampleCount += samples.size();
                         qDebug() << "Received" << samples.size() << "samples, total" << sampleCount;
                     });

    qDebug() << "Starting stream...";
    device.start();

    // Останавливаем через 5 секунд
    QTimer::singleShot(5000, [&]() {
        qDebug() << "Stopping...";
        device.stop();
        QCoreApplication::quit();
    });

    int result = app.exec();
    qDebug() << "Total samples received:" << sampleCount;
    return result;
}
