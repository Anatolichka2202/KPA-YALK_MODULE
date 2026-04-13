#include "address_parser.h"
#include <string>
#include <vector>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

// --------------------------------------------------------------
// Вспомогательные функции (аналог Delphi-функций)
// --------------------------------------------------------------

static inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

static int parse_two_digits(const char* s, int& val, int& koef) {
    if (!is_digit(s[0]) || !is_digit(s[1])) return -1;
    val = s[0] - '0';
    koef = s[1] - '0';
    return 0;
}

static void fill_group_arr(orbita_channel_desc_t* desc, uint32_t first_elem, uint32_t step, uint32_t group_size) {
    if (step <= group_size) {
        desc->flagGroup = 0;
        return;
    }
    desc->flagGroup = 1;
    uint32_t fElem = first_elem;
    uint32_t fNumGr = (fElem - 1) / group_size + 1;
    uint32_t sNumGr = (step - 1) / group_size;
    std::vector<uint16_t> groups;
    for (uint32_t gr = fNumGr; gr <= 32; gr += sNumGr) {
        groups.push_back((uint16_t)gr);
    }
    desc->numGroups = (uint16_t)groups.size();
    desc->arrNumGroup = (uint16_t*)malloc(desc->numGroups * sizeof(uint16_t));
    if (desc->arrNumGroup) {
        for (size_t i = 0; i < groups.size(); ++i) desc->arrNumGroup[i] = groups[i];
    }
}

static void fill_cikl_arr(orbita_channel_desc_t* desc, uint32_t first_elem, uint32_t step, uint32_t group_size, uint32_t cycle_size) {
    if (step <= cycle_size) {
        desc->flagCikl = 0;
        fill_group_arr(desc, first_elem, step, group_size);
        return;
    }
    desc->flagCikl = 1;
    uint32_t fElem = first_elem;
    uint32_t fNumC = (fElem - 1) / cycle_size + 1;
    uint32_t sNumC = (step - 1) / cycle_size;
    std::vector<uint16_t> cikls;
    for (uint32_t cc = fNumC; cc <= 4; cc += sNumC) {
        cikls.push_back((uint16_t)cc);
    }
    desc->numCikls = (uint16_t)cikls.size();
    desc->arrNumCikl = (uint16_t*)malloc(desc->numCikls * sizeof(uint16_t));
    if (desc->arrNumCikl) {
        for (size_t i = 0; i < cikls.size(); ++i) desc->arrNumCikl[i] = cikls[i];
    }
    desc->flagGroup = 0;
}

// --------------------------------------------------------------
// Основная функция парсинга строки (без обрезания комментариев)
// --------------------------------------------------------------
int address_parse_line(const char* line, orbita_channel_desc_t* out_desc) {
    if (!line || !out_desc) return -1;
    memset(out_desc, 0, sizeof(orbita_channel_desc_t));
    std::string s(line);
    // удаляем пробелы/табуляции в конце (комментарии отрезаны снаружи)
    size_t end = s.find_first_of(" \t");
    if (end != std::string::npos) s = s.substr(0, end);
    if (s.empty()) return -2;

    size_t i = 0;
    if (s[i] != 'M' && s[i] != 'm') return -3;
    i++;
    if (!is_digit(s[i]) || !is_digit(s[i+1])) return -4;
    int inf = (s[i]-'0')*10 + (s[i+1]-'0');
    i += 2;
    uint32_t pBeginOffset = 1;
    if (inf == 16) {
        // Проверка на 'П' (UTF-8 0xD0 0x9F) или 'p'/'P' или 0x8F
        if ((i+1 < s.length() && (unsigned char)s[i] == 0xD0 && (unsigned char)s[i+1] == 0x9F) ||
            s[i] == 'p' || s[i] == 'P' || (unsigned char)s[i] == 0x8F) {
            i++;
            if (s[i] == '1') pBeginOffset = 1;
            else if (s[i] == '2') pBeginOffset = 2;
            else return -5;
            i++;
        }
    }
    uint32_t offset = 0;
    uint32_t stepOutGins = 1;
    while (i < s.length()) {
        char ch = s[i];
        if (ch == 'A' || ch == 'a') {
            if (i+2 >= s.length()) return -6;
            int val, koef;
            if (parse_two_digits(&s[i+1], val, koef) != 0) return -7;
            uint32_t Fa = (koef==0?8: (koef==1?4:2));
            offset += (val - 1) * stepOutGins;
            stepOutGins *= Fa;
            i += 3;
        }
        else if (ch == 'B' || ch == 'b') {
            if (i+2 >= s.length()) return -8;
            int val, koef;
            if (parse_two_digits(&s[i+1], val, koef) != 0) return -9;
            uint32_t Fb = (koef==0?8: (koef==1?4:2));
            offset += (val - 1) * stepOutGins;
            stepOutGins *= Fb;
            i += 3;
        }
        else if (ch == 'C' || ch == 'c') {
            if (i+2 >= s.length()) return -10;
            int val, koef;
            if (parse_two_digits(&s[i+1], val, koef) != 0) return -11;
            uint32_t Fc = (koef==0?8: (koef==1?4:2));
            offset += (val - 1) * stepOutGins;
            stepOutGins *= Fc;
            i += 3;
        }
        else if (ch == 'D' || ch == 'd') {
            if (i+2 >= s.length()) return -12;
            int val, koef;
            if (parse_two_digits(&s[i+1], val, koef) != 0) return -13;
            uint32_t Fd = (koef==0?8: (koef==1?4:2));
            offset += (val - 1) * stepOutGins;
            stepOutGins *= Fd;
            i += 3;
        }
        else if (ch == 'E' || ch == 'e') {
            if (i+2 >= s.length()) return -14;
            int val, koef;
            if (parse_two_digits(&s[i+1], val, koef) != 0) return -15;
            uint32_t Fe = (koef==0?8: (koef==1?4:2));
            offset += (val - 1) * stepOutGins;
            stepOutGins *= Fe;
            i += 3;
        }
        else if (ch == 'X' || ch == 'x') {
            if (i+2 >= s.length()) return -16;
            int val, koef;
            if (parse_two_digits(&s[i+1], val, koef) != 0) return -17;
            uint32_t Fx = (koef==0?8: (koef==1?4:2));
            offset += (val - 1) * stepOutGins;
            stepOutGins *= Fx;
            i += 3;
        }
        else if (ch == 'T' || ch == 't') {
            if (i+2 >= s.length()) return -18;
            std::string tspec = s.substr(i+1, 2);
            if (tspec == "01") {
                if (i+5 < s.length() && s[i+3] == '-' && s[i+4] == '0' && s[i+5] == '1') {
                    out_desc->adressType = ORBITA_ADDR_TYPE_ANALOG_9BIT;
                    i += 6;
                } else {
                    out_desc->adressType = ORBITA_ADDR_TYPE_ANALOG_10BIT;
                    i += 3;
                }
            } else if (tspec == "05") {
                out_desc->adressType = ORBITA_ADDR_TYPE_CONTACT;
                i += 3;
            } else if (tspec == "11") {
                out_desc->adressType = ORBITA_ADDR_TYPE_TEMPERATURE;
                i += 3;
            } else if (tspec == "21") {
                out_desc->adressType = ORBITA_ADDR_TYPE_FAST_1;
                i += 3;
            } else if (tspec == "22") {
                out_desc->adressType = ORBITA_ADDR_TYPE_FAST_2;
                i += 3;
            } else if (tspec == "23") {
                out_desc->adressType = ORBITA_ADDR_TYPE_FAST_3;
                i += 3;
            } else if (tspec == "24") {
                out_desc->adressType = ORBITA_ADDR_TYPE_FAST_4;
                i += 3;
            } else if (tspec == "25") {
                out_desc->adressType = ORBITA_ADDR_TYPE_BUS;
                i += 3;
            } else {
                return -19;
            }
            if (i < s.length() && (s[i] == 'P' || s[i] == 'p')) {
                if (out_desc->adressType != ORBITA_ADDR_TYPE_CONTACT) return -20;
                if (i+2 >= s.length()) return -21;
                int p1 = s[i+1]-'0', p2 = s[i+2]-'0';
                if (p1<0 || p1>9 || p2<0 || p2>9) return -22;
                out_desc->bitNumber = p1*10 + p2;
                i += 3;
            }
            break;
        }
        else {
            return -23;
        }
    }

    uint32_t numOutElemG = pBeginOffset + offset;
    uint32_t stepOutG = stepOutGins;
    if (inf == 16) {
        numOutElemG = pBeginOffset + 2 * offset;
        stepOutG = 2 * stepOutGins;
    }
    out_desc->numOutElemG = numOutElemG;
    out_desc->stepOutG = stepOutG;
    out_desc->flagGroup = 0;
    out_desc->flagCikl = 0;
    out_desc->arrNumGroup = nullptr;
    out_desc->numGroups = 0;
    out_desc->arrNumCikl = nullptr;
    out_desc->numCikls = 0;
    return 0;
}

// Публичная обёртка (уже объявлена в orbita_address.h)
int orbita_parse_address_line(const char* line, orbita_channel_desc_t* out_desc) {
    return address_parse_line(line, out_desc);
}

void orbita_free_channel_desc(orbita_channel_desc_t* desc) {
    if (desc) {
        free(desc->arrNumGroup);
        free(desc->arrNumCikl);
        memset(desc, 0, sizeof(orbita_channel_desc_t));
    }
}

int orbita_load_address_file(const char* filename, orbita_channel_desc_t** out_channels, int* out_count) {
    if (!filename || !out_channels || !out_count) return -1;
    FILE* f = fopen(filename, "r");
    if (!f) return -2;
    std::vector<orbita_channel_desc_t> channels;
    char line[1024];
    int line_num = 0;
    while (fgets(line, sizeof(line), f)) {
        line_num++;
        size_t len = strlen(line);
        while (len>0 && (line[len-1]=='\n' || line[len-1]=='\r')) line[--len] = '\0';
        char* comment = strpbrk(line, "\t ");
        if (comment) *comment = '\0';
        if (strlen(line) == 0) continue;
        orbita_channel_desc_t desc;
        int res = orbita_parse_address_line(line, &desc);
        if (res != 0) {
            fclose(f);
            return -3 - line_num;
        }
        channels.push_back(desc);
    }
    fclose(f);
    *out_count = (int)channels.size();
    *out_channels = (orbita_channel_desc_t*)malloc(*out_count * sizeof(orbita_channel_desc_t));
    if (!*out_channels) return -4;
    memcpy(*out_channels, channels.data(), *out_count * sizeof(orbita_channel_desc_t));
    return 0;
}

void orbita_free_channels(orbita_channel_desc_t* channels, int count) {
    if (channels) {
        for (int i = 0; i < count; ++i) orbita_free_channel_desc(&channels[i]);
        free(channels);
    }
}
