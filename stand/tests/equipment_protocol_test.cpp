#include "orbita_stand/equipment_adapters.h"
#include "orbita_stand/yalk_frame.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace orbita::stand;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
}

int main()
{
    try {
        require(IsdHttpRouter::switchPath(2, 17, true) == "/type=2num=17val=1",
                "ISD switching command differs from Delphi reference");
        require(IsdHttpRouter::analogPath(5, 2000, false)
                    == "/type=1num=5val=2000work=0",
                "ISD analog command differs from Delphi reference");
        require(IsdHttpRouter::fullResetPath() == "/type=4num=1",
                "ISD full reset differs from Delphi reference");
        require(R4831SerialAdapter::resistanceCommand(680.5) == "680.5\r\n",
                "R4831 ASCII command differs from Delphi reference");
        require(R4831SerialAdapter::resistanceCommand(680.5, true) == "680,5\r\n",
                "R4831 decimal-comma profile is broken");
        require(LegacyUdpPowerSupply::voltageCommand(27.0) == "VOLT 02700\r",
                "legacy power-supply voltage command differs from Delphi reference");
        require(LegacyUdpPowerSupply::currentCommand(1.25) == "CURR 00125\r",
                "legacy power-supply current command differs from Delphi reference");
        require(UbsiUdpAdapter::modeCommand(8) == std::vector<std::uint8_t>({0x44, 0x01, 0x08}),
                "UBSI mode frame differs from adapter firmware");
        require(UbsiUdpAdapter::modeCommand(8, true)
                    == std::vector<std::uint8_t>({0x44, 0x03, 0x08}),
                "UBSI single-mode flag differs from adapter firmware");
        const auto rokotReset = UbsiUdpAdapter::rokotResetCommand();
        require(rokotReset.size() == 128
                    && std::vector<std::uint8_t>(rokotReset.begin(), rokotReset.begin() + 9)
                        == std::vector<std::uint8_t>({'R','O','K','T',0x16,0,0,0,0}),
                "ROKOT reset packet differs from captured KPA command");
        const auto rokotYalk = UbsiUdpAdapter::rokotConfigureYalkCommand();
        require(rokotYalk.size() == 128
                    && std::vector<std::uint8_t>(rokotYalk.begin(), rokotYalk.begin() + 9)
                        == std::vector<std::uint8_t>({'R','O','K','T',0x14,1,43,1,0}),
                "ROKOT YALK address packet differs from captured KPA command");
        const auto rokotYtp = UbsiUdpAdapter::rokotConfigureYtpCommand();
        require(std::vector<std::uint8_t>(rokotYtp.begin(), rokotYtp.begin() + 8)
                    == std::vector<std::uint8_t>({'R','O','K','T',0x15,1,1,1}),
                "ROKOT YTP address packet differs from captured KPA command");
        const auto rokotSelect = UbsiUdpAdapter::rokotSelectYalkCommand();
        require(std::vector<std::uint8_t>(rokotSelect.begin(), rokotSelect.begin() + 8)
                    == std::vector<std::uint8_t>({'R','O','K','T',0x0A,0,0,1}),
                "ROKOT YALK select packet differs from captured KPA command");
        require(UbsiUdpAdapter::wordIndexForUlkAddress(1) == 0
                    && UbsiUdpAdapter::wordIndexForUlkAddress(99) == 98,
                "ULK address must map to the slow-frame word using address-1");
        std::vector<std::uint8_t> yalkPacket(200, 0);
        yalkPacket[0] = 0x34;
        yalkPacket[1] = 0xA2;
        yalkPacket[192] = 0x79; // Delphi BufferYALK[97], zero calibration
        yalkPacket[196] = 0x9A; // Delphi BufferYALK[99], full-scale calibration
        const auto yalk9 = UbsiUdpAdapter::decodeYalkPacket(yalkPacket, 0x01FF);
        require(yalk9.size() == 100 && yalk9[0] == 0x0034,
                "YALK mode 0/6 little-endian 9-bit decode is wrong");
        require(yalk9[96] == 0x0079 && yalk9[98] == 0x009A,
                "YALK calibration positions 97/99 are wrong");
        std::vector<std::uint8_t> kpaPacket{0x02, 0x00, 0x2B, 0x00};
        kpaPacket.insert(kpaPacket.end(), yalkPacket.begin(), yalkPacket.end());
        const auto kpaYalk = UbsiUdpAdapter::decodeYalkPacket(kpaPacket, 0x01FF);
        require(kpaYalk == yalk9,
                "KPA/Rokot four-byte transport header must not shift YALK words");
        const auto yalk10 = UbsiUdpAdapter::decodeYalkPacket(yalkPacket, 0x03FF);
        require(yalk10[0] == 0x0234,
                "YALK mode 11 10-bit decode is wrong");

        //бок проверки декодирования данных адаптера
        const auto sample = decodeYalkSample(0x0323);
        require(sample.rawWord == 0x0323,
                "raw YALK word is wrong");
        require(sample.analogCode == 0x0123,
                "YALK analog code is wrong");
        require(sample.contact,
                "YALK contact bit is wrong");
        std::vector<std::uint8_t> slowFrame(200,0);
        slowFrame[0] = 0x23; // младший байт
        slowFrame[1] = 0x03; // старший байт

        const auto samples = decodeYalkSlowFrame(slowFrame);
        require(samples.size() == 100,
                "YALK slow frame must contain 100 samples");
        require(samples[0].rawWord == 0x0323,
                "YALK slow raw word is wrong");
        require(samples[0].analogCode == 0x0123,
                "YALK slow analog code is wrong");
        require(samples[0].contact,
                "YALK slow contact is wrong");
        std::vector<std::uint8_t> kpaSlowFrame{0x02, 0x00, 0x2B, 0x00};
        kpaSlowFrame.insert(kpaSlowFrame.end(), slowFrame.begin(), slowFrame.end());
        const auto kpaSamples = decodeYalkSlowFrame(kpaSlowFrame);
        require(kpaSamples.size() == 100 && kpaSamples[0].rawWord == 0x0323,
                "KPA/Rokot header must not shift the slow YALK frame");
        std::vector<std::uint8_t> badFrame(120, 0);
        bool badFrameRejected = false;
        try{
        decodeYalkSlowFrame(badFrame);
        }catch (const std::invalid_argument&) {
            badFrameRejected = true;
        }
        require(
            badFrameRejected,
            "YALK decoder accepted invalid 120-byte frame"
            );
        std::cout << "Equipment protocol tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Equipment protocol test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
