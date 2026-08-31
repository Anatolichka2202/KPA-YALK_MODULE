#include "orbita_stand/equipment_adapters.h"
#include "orbita_stand/ulk_udp_transport.h"
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
        require(IsdHttpRouter::yalkPreparePath() == "/type=7num=1",
                "ISD YALK prepare command differs from captured KPA traffic");
        require(IsdHttpRouter::yalkVoltagePath(1, 0.0)
                    == "/type=5num=1val=0.00work=1bus=1"
                && IsdHttpRouter::yalkVoltagePath(1, 3.1)
                    == "/type=5num=1val=3.10work=1bus=1"
                && IsdHttpRouter::yalkVoltagePath(1, 6.2)
                    == "/type=5num=1val=6.20work=1bus=1",
                "ISD YALK voltage command differs from captured KPA traffic");
        require(IsdHttpRouter::yalkOutputBusOffPath(1)
                    == "/type=1num=1val=0work=1bus=0"
                && IsdHttpRouter::yalkOutputOffPath(1)
                    == "/type=1num=1val=0work=0",
                "ISD YALK output-off sequence differs from captured KPA traffic");
        require(R4831SerialAdapter::resistanceCommand(680.5) == "680.5\r\n",
                "R4831 ASCII command differs from Delphi reference");
        require(R4831SerialAdapter::resistanceCommand(680.5, true) == "680,5\r\n",
                "R4831 decimal-comma profile is broken");
        require(LegacyUdpPowerSupply::voltageCommand(27.0) == "VOLT 02700\r",
                "legacy power-supply voltage command differs from Delphi reference");
        require(LegacyUdpPowerSupply::currentCommand(1.25) == "CURR 00125\r",
                "legacy power-supply current command differs from Delphi reference");
        require(UlkUdpTransport::modeCommand(8)
                    == std::vector<std::uint8_t>({0x44, 0x01, 0x08}),
                "ULK mode frame differs from adapter firmware");
        require(UlkUdpTransport::classify(4) == UlkFrameKind::Service4
                    && UlkUdpTransport::classify(120) == UlkFrameKind::Fast120
                    && UlkUdpTransport::classify(200) == UlkFrameKind::Slow200
                    && UlkUdpTransport::classify(204) == UlkFrameKind::Reference204,
                "ULK frame classifier is wrong");
        std::vector<std::uint8_t> yalkPacket(200, 0);
        yalkPacket[0] = 0x34;
        yalkPacket[1] = 0xA2;
        yalkPacket[192] = 0x79; // Delphi BufferYALK[97], zero calibration
        yalkPacket[196] = 0x9A; // Delphi BufferYALK[99], full-scale calibration
        const auto yalk9 = decodeYalkSlowFrame(yalkPacket);
        require(yalk9.size() == 100 && yalk9[0].analogCode == 0x0034,
                "YALK mode 0/6 little-endian 9-bit decode is wrong");
        require(yalk9[96].analogCode == 0x0079 && yalk9[98].analogCode == 0x009A,
                "YALK calibration positions 97/99 are wrong");

        std::vector<std::uint8_t> referenceFrame(204, 0);
        referenceFrame[0] = 0x02;
        referenceFrame[2] = 0x2B;
        referenceFrame[4] = 0x23;
        referenceFrame[5] = 0x05; // addr1 raw=0x0523: analog=0x123, signal=1
        referenceFrame[4 + 2 * 96] = 0x7D; // addr97 = 125
        referenceFrame[4 + 2 * 98] = 0x9A;
        referenceFrame[5 + 2 * 98] = 0x03; // addr99 = 922
        const auto reference = decodeYalkReferenceFrame(referenceFrame);
        require(reference.size() == 100
                    && reference[0].rawWord == 0x0523
                    && reference[0].analogCode == 0x0123
                    && reference[0].contact,
                "YALK reference204 header/masks decode is wrong");
        require(reference[96].analogCode == 125
                    && reference[98].analogCode == 922,
                "YALK reference204 calibration positions 97/99 are wrong");
        bool referenceRejectedSlow = false;
        try { decodeYalkSlowFrame(referenceFrame); }
        catch (const std::invalid_argument&) { referenceRejectedSlow = true; }
        require(referenceRejectedSlow,
                "Legacy Slow200 decoder accepted a Reference204 frame");

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
