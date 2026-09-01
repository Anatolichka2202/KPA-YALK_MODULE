#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

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

    std::cout << "SEND " << name << " bytes=" << sent << '\n';
}

std::uint16_t wordLe(
    const std::array<std::uint8_t, 2048>& buffer,
    std::size_t offset)
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(buffer[offset])
        | (static_cast<std::uint16_t>(buffer[offset + 1]) << 8));
}

} // namespace

int main(int argc, char** argv)
{
    std::cout.setf(std::ios::unitbuf);

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

        // RX: адаптер шлёт broadcast на :1113.
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

        // TX: исходящий интерфейс стенда.
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

        // Последовательность снята с живой рабочей KPA.
        const auto reset =
            roktCommand({
                0x16, 0x00, 0x00, 0x00
            });

        const auto addressYalk =
            roktCommand({
                0x14,
                0x01,
                0x2B,
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

        // ЖИВОЙ ЯТП:
        // 52 4F 4B 54 0A 02 00 01 00 ...
        const auto startYtp =
            roktCommand({
                0x0A,
                0x02,
                0x00,
                0x01,
                0x00
            });

        sendCommand(sender, remote, reset, "reset");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        sendCommand(sender, remote, addressYalk, "addr_yalk");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        sendCommand(sender, remote, addressYtp, "addr_ytp");

        // Референсная KPA делает заметную паузу перед start YTP.
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));

        sendCommand(sender, remote, startYtp, "start_ytp");

        const auto expectedSender = endpoint(adapterIp, port);

        unsigned frames68 = 0;
        unsigned otherFrames = 0;
        unsigned changes = 0;

        std::array<std::uint16_t, 32> previous{};
        bool baselineReady = false;

        //настраиваем время работы монитора
        const auto started =
            std::chrono::steady_clock::now();

        const auto settleUntill =
            started + std::chrono::seconds(1);

        const auto deadline =
            started + std::chrono::seconds(30);

        std::cout<<"MONITOR settle=1s duration=30s\n";

        while (std::chrono::steady_clock::now() < deadline)
        {

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
                    "recvfrom failed: " + std::to_string(error));
            }

            if (source.sin_addr.s_addr
                    != expectedSender.sin_addr.s_addr
                || source.sin_port
                       != expectedSender.sin_port) {
                continue;
            }

            if (count != 68) {
                ++otherFrames;
                continue;
            }

            ++frames68;

            std::array<std::uint16_t, 32> current{};

            for(unsigned index = 0; index < current.size(); ++ index)
            {
                current[index] = wordLe(
                    buffer,
                    4 + static_cast<std::size_t>(index)*2);
            }
            //первый ~1 с адаптер последовательно наполняет 32 слова.
            // не считаем первые переходы изменением физ. входа
            if(std::chrono::steady_clock::now() < settleUntill)
            {
                previous = current;
                continue;
            }

            if(!baselineReady)
            {
                previous = current;
                baselineReady = true;

                std::cout << "BASELINE";

                for(unsigned index = 0; index <previous.size(); ++index)
                {
                    std::cout
                        << ' '
                        <<(index < 30 ? "CH" : "CAL")
                        <<std::setw(2)
                        <<std::setfill('0')
                        <<index + 1
                        <<"=0"
                        <<std::hex
                        <<std::setw(4)
                        <<previous[index]
                        <<std::dec;
                }
                std::cout<< '\n';
                continue;
            }

            for(unsigned index = 0; index <previous.size(); ++index)
            {
                if(current[index] == previous[index])
                    continue;

                ++changes;

                const auto elepsedMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started)
                                           .count();

                std::cout
                    <<"CHANGE"
                    <<"t_ms=" << elepsedMs
                    <<' '
                    <<(index < 30 ? "CH" : "CAL")
                    <<std::setw(2)
                    <<std::setfill('0')
                    <<index + 1
                    <<" 0x"
                    <<std::hex
                    <<std::setw(4)
                    <<previous[index]
                    <<" -> 0x"
                    <<std::setw(4)
                    <<current[index]
                    <<std::dec
                    <<" (" << previous[index]
                    <<" -> " << current[index] << ")"
                    << '\n';
            }
            previous = current;
        }

        std::cout
            << "SUMMARY"
            << " frames68=" << frames68
            << " other=" << otherFrames
            << " changes=" << changes
            << '\n';

        if(frames68 == 0)
        {
            std::cout << "RESULT NO_YTP_DATA\n";
            closeSocket(sender);
            closeSocket(receiver);
            WSACleanup();

            return EXIT_SUCCESS;
        }

        std::cout << "RESULT YTP_MONITOR_OK\n";

        closeSocket(sender);
        closeSocket(receiver);
        WSACleanup();

        return EXIT_SUCCESS;
    }
    catch (const std::exception& error) {
        std::cerr << "ERROR " << error.what() << '\n';

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
