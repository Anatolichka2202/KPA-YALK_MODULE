#include "address_parser.h"
#include <cctype>
#include <stdexcept>
#include <algorithm>
#include "../include/orbita_types.h"
namespace orbita {

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int parse_two_digits(const char* s, int& val, int& koef) {
    if (!is_digit(s[0]) || !is_digit(s[1])) return -1;
    val = s[0] - '0';
    koef = s[1] - '0';
    return 0;
}

ChannelDesc AddressParser::parseLine(const std::string& line) {
    ChannelDesc desc;
    std::string s = line;
    // удаляем пробелы/табуляции в конце
    size_t end = s.find_first_of(" \t");
    if (end != std::string::npos) s = s.substr(0, end);
    if (s.empty()) throw std::runtime_error("Empty line");

    size_t i = 0;
    if (s[i] != 'M' && s[i] != 'm') throw std::runtime_error("Invalid address: missing 'M'");
    i++;
    if (!is_digit(s[i]) || !is_digit(s[i+1])) throw std::runtime_error("Invalid informativnost");
    int inf = (s[i]-'0')*10 + (s[i+1]-'0');
    i += 2;
    uint32_t pBeginOffset = 1;
    if (inf == 16) {
        // Пропускаем любые нецифровые символы (буква 'П' в любой кодировке, 'Đ', 'Ě' и т.д.)
        while (i < s.length() && !std::isdigit(static_cast<unsigned char>(s[i]))) {
            i++;
        }
        if (i >= s.length()) throw std::runtime_error("Missing offset after M16");
        if (s[i] == '1') pBeginOffset = 1;
        else if (s[i] == '2') pBeginOffset = 2;
        else throw std::runtime_error("Invalid offset (must be 1 or 2)");
        i++;
    }

    uint32_t offset = 0;
    uint32_t stepOutGins = 1;

    while (i < s.length()) {
        char ch = s[i];
        if (ch == 'A' || ch == 'a') {
            if (i+2 >= s.length()) throw std::runtime_error("Incomplete A field");
            int val, koef;
            if (parse_two_digits(&s[i+1], val, koef) != 0) throw std::runtime_error("Invalid A digits");
            uint32_t Fa = (koef==0 ? 8 : (koef==1 ? 4 : 2));
            offset += (val - 1) * stepOutGins;
            stepOutGins *= Fa;
            i += 3;
        }
        else if (ch == 'B' || ch == 'b') {
            if (i+2 >= s.length()) throw std::runtime_error("Incomplete B field");
            int val, koef;
            if (parse_two_digits(&s[i+1], val, koef) != 0) throw std::runtime_error("Invalid B digits");
            uint32_t Fb = (koef==0 ? 8 : (koef==1 ? 4 : 2));
            offset += (val - 1) * stepOutGins;
            stepOutGins *= Fb;
            i += 3;
        }
        else if (ch == 'C' || ch == 'c') {
            if (i+2 >= s.length()) throw std::runtime_error("Incomplete C field");
            int val, koef;
            if (parse_two_digits(&s[i+1], val, koef) != 0) throw std::runtime_error("Invalid C digits");
            uint32_t Fc = (koef==0 ? 8 : (koef==1 ? 4 : 2));
            offset += (val - 1) * stepOutGins;
            stepOutGins *= Fc;
            i += 3;
        }
        else if (ch == 'D' || ch == 'd') {
            if (i+2 >= s.length()) throw std::runtime_error("Incomplete D field");
            int val, koef;
            if (parse_two_digits(&s[i+1], val, koef) != 0) throw std::runtime_error("Invalid D digits");
            uint32_t Fd = (koef==0 ? 8 : (koef==1 ? 4 : 2));
            offset += (val - 1) * stepOutGins;
            stepOutGins *= Fd;
            i += 3;
        }
        else if (ch == 'E' || ch == 'e') {
            if (i+2 >= s.length()) throw std::runtime_error("Incomplete E field");
            int val, koef;
            if (parse_two_digits(&s[i+1], val, koef) != 0) throw std::runtime_error("Invalid E digits");
            uint32_t Fe = (koef==0 ? 8 : (koef==1 ? 4 : 2));
            offset += (val - 1) * stepOutGins;
            stepOutGins *= Fe;
            i += 3;
        }
        else if (ch == 'X' || ch == 'x') {
            if (i+2 >= s.length()) throw std::runtime_error("Incomplete X field");
            int val, koef;
            if (parse_two_digits(&s[i+1], val, koef) != 0) throw std::runtime_error("Invalid X digits");
            uint32_t Fx = (koef==0 ? 8 : (koef==1 ? 4 : 2));
            offset += (val - 1) * stepOutGins;
            stepOutGins *= Fx;
            i += 3;
        }
        else if (ch == 'T' || ch == 't') {
            if (i+2 >= s.length()) throw std::runtime_error("Incomplete T field");
            std::string tspec = s.substr(i+1, 2);
            if (tspec == "01") {
                if (i+5 < s.length() && s[i+3] == '-' && s[i+4] == '0' && s[i+5] == '1') {
                    desc.adressType = ORBITA_ADDR_TYPE_ANALOG_9BIT;
                    i += 6;
                } else {
                    desc.adressType = ORBITA_ADDR_TYPE_ANALOG_10BIT;
                    i += 3;
                }
            } else if (tspec == "05") {
                desc.adressType = ORBITA_ADDR_TYPE_CONTACT;
                i += 3;
            } else if (tspec == "11") {
                desc.adressType = ORBITA_ADDR_TYPE_TEMPERATURE;
                i += 3;
            } else if (tspec == "21") {
                desc.adressType = ORBITA_ADDR_TYPE_FAST_1;
                i += 3;
            } else if (tspec == "22") {
                desc.adressType = ORBITA_ADDR_TYPE_FAST_2;
                i += 3;
            } else if (tspec == "23") {
                desc.adressType = ORBITA_ADDR_TYPE_FAST_3;
                i += 3;
            } else if (tspec == "24") {
                desc.adressType = ORBITA_ADDR_TYPE_FAST_4;
                i += 3;
            } else if (tspec == "25") {
                desc.adressType = ORBITA_ADDR_TYPE_BUS;
                i += 3;
            } else {
                throw std::runtime_error("Unknown T type");
            }
            // Обработка номера бита для контактных
            if (desc.adressType == ORBITA_ADDR_TYPE_CONTACT) {
                if (i < s.length() && (s[i] == 'P' || s[i] == 'p')) {
                    i++;
                    if (i+1 >= s.length()) throw std::runtime_error("Missing bit number after P");
                    int p1 = s[i] - '0';
                    int p2 = s[i+1] - '0';
                    if (p1 < 0 || p1 > 9 || p2 < 0 || p2 > 9) throw std::runtime_error("Invalid bit number");
                    desc.bitNumber = p1*10 + p2;
                    i += 2;
                } else {
                    desc.bitNumber = 0; // или ошибка? в Delphi по умолчанию 0
                }
            }
            break;
        }
        else {
            throw std::runtime_error(std::string("Unexpected character: ") + ch);
        }
    }

    uint32_t numOutElemG = pBeginOffset + offset;
    uint32_t stepOutG = stepOutGins;
    if (inf == 16) {
        numOutElemG = pBeginOffset + 2 * offset;
        stepOutG = 2 * stepOutGins;
    }
    desc.numOutElemG = numOutElemG;
    desc.stepOutG = stepOutG;
    // Остальные поля инициализированы по умолчанию (векторы пусты)
    return desc;
}

std::vector<ChannelDesc> AddressParser::loadFile(const std::string& filename) {
    std::vector<ChannelDesc> channels;
    FILE* f = fopen(filename.c_str(), "r");
    if (!f) throw std::runtime_error("Cannot open file: " + filename);
    char line[1024];
    int line_num = 0;
    while (fgets(line, sizeof(line), f)) {
        line_num++;
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        char* comment = strpbrk(line, "\t ");
        if (comment) *comment = '\0';
        if (strlen(line) == 0) continue;
        try {
            ChannelDesc desc = parseLine(line);
            channels.push_back(std::move(desc));
        } catch (const std::exception& e) {
            fclose(f);
            throw std::runtime_error("Line " + std::to_string(line_num) + ": " + e.what());
        }
    }
    fclose(f);
    return channels;
}

std::vector<ChannelDesc> AddressParser::loadLines(const std::vector<std::string>& lines) {
    std::vector<ChannelDesc> channels;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].empty()) continue;
        try {
            ChannelDesc desc = parseLine(lines[i]);
            channels.push_back(std::move(desc));
        } catch (const std::exception& e) {
            throw std::runtime_error("Line " + std::to_string(i+1) + ": " + e.what());
        }
    }
    return channels;
}

} // namespace orbita
