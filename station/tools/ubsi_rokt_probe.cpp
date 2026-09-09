#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using Socket = SOCKET;
constexpr Socket InvalidSocket = INVALID_SOCKET;

void closeSocket(Socket socket)
{
    if (socket != InvalidSocket)
        closesocket(socket);
}

sockaddr_in endpoint(const std::string& address, std::uint16_t port)
{
    sockaddr_in result{};
    result.sin_family = AF_INET;
    result.sin_port = htons(port);

    if (inet_pton(AF_INET, address.c_str(), &result.sin_addr) != 1)
        throw std::runtime_error("Invalid IPv4 address: " + address);

    return result;
}

std::array<std::uint8_t, 128> roktCommand(
    std::initializer_list<std::uint8_t> body)
{
    std::array<std::uint8_t, 128> result{};

    result[0] = 'R';
    result[1] = 'O';
    result[2] = 'K';
    result[3] = 'T';

    std::size_t index = 4;
    for (const auto byte : body) {
        if (index >= result.size())
            break;

        result[index++] = byte;
    }

    return result;
}

void sendCommand(
    Socket sender,
    const sockaddr_in& remote,
    const std::array<std::uint8_t, 128>& command,
    const char* name)
{
    const int sent = ::sendto(
        sender,
        reinterpret_cast<const char*>(command.data()),
        static_cast<int>(command.size()),
        0,
        reinterpret_cast<const sockaddr*>(&remote),
        sizeof(remote));

    if (sent != static_cast<int>(command.size()))
        throw std::runtime_error(
            std::string("Cannot send ROKT command: ") + name);

    std::cout
        << "SEND " << name
        << " bytes=" << sent
        << '\n';
}

unsigned median(std::vector<unsigned> values)
{
    if (values.empty())
        return 0;

    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

struct AddressSamples {
    unsigned address = 0;
    std::vector<unsigned> raw;
    std::vector<unsigned> analog;
    unsigned contactOne = 0;
};

} // namespace

int main(int argc, char** argv)
{
    const std::string adapterIp =
        argc >= 2 ? argv[1] : "192.168.0.115";

    const std::string localIp =
        argc >= 3 ? argv[2] : "192.168.0.50";

    const std::uint16_t port =
        argc >= 4
            ? static_cast<std::uint16_t>(std::stoul(argv[3]))
            : 1113;

    WSADATA wsa{};

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return EXIT_FAILURE;
    }

    Socket receiver = InvalidSocket;
    Socket sender = InvalidSocket;

    try {
        std::cout
            << "TARGET adapter=" << adapterIp
            << " local=" << localIp
            << " port=" << port
            << '\n';

        // ---------------------------------------------------------
        // RX: принимаем broadcast 192.168.0.255:1113.
        // Поэтому bind именно 0.0.0.0:1113.
        // ---------------------------------------------------------

        receiver = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        if (receiver == InvalidSocket)
            throw std::runtime_error("Cannot create receive socket");

        int reuse = 1;

        setsockopt(
            receiver,
            SOL_SOCKET,
            SO_REUSEADDR,
            reinterpret_cast<const char*>(&reuse),
            sizeof(reuse));

        DWORD receiveTimeoutMs = 250;

        setsockopt(
            receiver,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char*>(&receiveTimeoutMs),
            sizeof(receiveTimeoutMs));

        sockaddr_in receiveAddress{};
        receiveAddress.sin_family = AF_INET;
        receiveAddress.sin_port = htons(port);
        receiveAddress.sin_addr.s_addr = htonl(INADDR_ANY);

        if (::bind(
                receiver,
                reinterpret_cast<const sockaddr*>(&receiveAddress),
                sizeof(receiveAddress)) != 0) {
            throw std::runtime_error(
                "Cannot bind 0.0.0.0:" + std::to_string(port));
        }

        // ---------------------------------------------------------
        // TX: отдельный сокет с исходным интерфейсом 192.168.0.50.
        // Source port Windows выбирает сам.
        // ---------------------------------------------------------

        sender = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        if (sender == InvalidSocket)
            throw std::runtime_error("Cannot create command socket");

        const auto local = endpoint(localIp, 0);

        if (::bind(
                sender,
                reinterpret_cast<const sockaddr*>(&local),
                sizeof(local)) != 0) {
            throw std::runtime_error(
                "Cannot bind command socket to " + localIp);
        }

        const auto remote = endpoint(adapterIp, port);

        // ---------------------------------------------------------
        // Команды сняты с работающей референсной программы.
        // Каждый UDP payload = ровно 128 байт.
        // Неуказанные байты остаются нулевыми.
        // ---------------------------------------------------------

        const auto reset =
            roktCommand({
                0x16, 0x00, 0x00, 0x00
            });

        const auto addressYalk =
            roktCommand({
                0x14,
                0x01,
                0x2B,   // 43
                0x01,
                0x00
            });

        const auto addressYtp =
            roktCommand({
                0x15,
                0x01,
                0x01,
                0x01,
                0x00
            });

        const auto startYalk =
            roktCommand({
                0x0A,
                0x00,
                0x00,
                0x01,
                0x00
            });

        sendCommand(sender, remote, reset, "reset");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        sendCommand(sender, remote, addressYalk, "addr_yalk");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        sendCommand(sender, remote, addressYtp, "addr_ytp");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        sendCommand(sender, remote, startYalk, "start_yalk");

        // ---------------------------------------------------------
        // Набираем 16 кадров по 204 байта.
        // Формат из живого pcap:
        //
        //   [4-byte header]
        //   [100 x little-endian uint16]
        //
        // Для этого формата:
        //   analog  = raw & 0x03FF
        //   contact = raw & 0x0400
        // ---------------------------------------------------------

        std::array<AddressSamples, 4> samples{{
            {1},
            {97},
            {98},
            {99}
        }};

        std::array<std::uint8_t, 4> firstHeader{};
        bool haveHeader = false;

        unsigned frames204 = 0;
        unsigned frames4 = 0;
        unsigned frames120 = 0;
        unsigned frames200 = 0;
        unsigned framesOther = 0;

        const auto expectedSender = endpoint(adapterIp, port);

        const auto deadline =
            std::chrono::steady_clock::now()
            + std::chrono::seconds(3);

        while (
            std::chrono::steady_clock::now() < deadline
            && frames204 < 16) {

            std::array<std::uint8_t, 2048> buffer{};

            sockaddr_in source{};
            int sourceLength = sizeof(source);

            const int count = ::recvfrom(
                receiver,
                reinterpret_cast<char*>(buffer.data()),
                static_cast<int>(buffer.size()),
                0,
                reinterpret_cast<sockaddr*>(&source),
                &sourceLength);

            if (count == SOCKET_ERROR) {
                const int error = WSAGetLastError();

                if (error == WSAETIMEDOUT
                    || error == WSAEWOULDBLOCK) {
                    continue;
                }

                throw std::runtime_error(
                    "recvfrom failed: "
                    + std::to_string(error));
            }

            // Принимаем только наш адаптер.
            if (source.sin_addr.s_addr
                    != expectedSender.sin_addr.s_addr
                || source.sin_port
                       != expectedSender.sin_port) {
                continue;
            }

            switch (count) {
            case 4:
                ++frames4;
                continue;

            case 120:
                ++frames120;
                continue;

            case 200:
                ++frames200;
                continue;

            case 204:
                ++frames204;
                break;

            default:
                ++framesOther;
                continue;
            }

            if (!haveHeader) {
                for (std::size_t i = 0; i < 4; ++i)
                    firstHeader[i] = buffer[i];

                haveHeader = true;
            }

            for (auto& item : samples) {
                const std::size_t wordIndex =
                    static_cast<std::size_t>(item.address - 1);

                const std::size_t offset =
                    4 + wordIndex * 2;

                const std::uint16_t raw =
                    static_cast<std::uint16_t>(
                        buffer[offset])
                    |
                    (static_cast<std::uint16_t>(
                         buffer[offset + 1]) << 8);

                const unsigned analog =
                    raw & 0x03FFu;

                const bool contact =
                    (raw & 0x0400u) != 0;

                item.raw.push_back(raw);
                item.analog.push_back(analog);

                if (contact)
                    ++item.contactOne;
            }
        }

        std::cout
            << "STATS"
            << " service4=" << frames4
            << " fast120=" << frames120
            << " slow200=" << frames200
            << " reference204=" << frames204
            << " other=" << framesOther
            << '\n';

        if (!frames204) {
            std::cout
                << "RESULT reference204=0 FAIL\n";

            closeSocket(sender);
            closeSocket(receiver);
            WSACleanup();

            return 3;
        }

        std::cout
            << "HEADER204 hex="
            << std::hex
            << std::setfill('0')
            << std::setw(2)
            << static_cast<unsigned>(firstHeader[0])
            << std::setw(2)
            << static_cast<unsigned>(firstHeader[1])
            << std::setw(2)
            << static_cast<unsigned>(firstHeader[2])
            << std::setw(2)
            << static_cast<unsigned>(firstHeader[3])
            << std::dec
            << '\n';

        for (const auto& item : samples) {
            const auto [minIt, maxIt] =
                std::minmax_element(
                    item.analog.begin(),
                    item.analog.end());

            const unsigned lastRaw =
                item.raw.empty()
                    ? 0
                    : item.raw.back();

            std::cout
                << "ADDR " << item.address
                << " raw_last=0x"
                << std::hex
                << std::setw(4)
                << std::setfill('0')
                << lastRaw
                << std::dec
                << " analog_median="
                << median(item.analog)
                << " analog_min="
                << (minIt == item.analog.end()
                        ? 0
                        : *minIt)
                << " analog_max="
                << (maxIt == item.analog.end()
                        ? 0
                        : *maxIt)
                << " contact_ones="
                << item.contactOne
                << "/"
                << item.analog.size()
                << '\n';
        }

        std::cout
            << "RESULT reference204="
            << frames204
            << " OK\n";

        closeSocket(sender);
        closeSocket(receiver);
        WSACleanup();

        return EXIT_SUCCESS;
    }
    catch (const std::exception& error) {
        std::cerr
            << "ERROR "
            << error.what()
            << '\n';

        closeSocket(sender);
        closeSocket(receiver);
        WSACleanup();

        return EXIT_FAILURE;
    }
}

#else

int main()
{
    return 1;
}

#endif
