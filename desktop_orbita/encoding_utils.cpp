#include "encoding_utils.h"
#include <QFile>
#include <QDebug>
#include <windows.h>
#include <vector>

namespace encoding {

std::string readFileToUtf8(const std::string& path) {
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QByteArray raw = file.readAll();
    if (raw.isEmpty())
        return {};

    // UTF-8 BOM (EF BB BF) → strip BOM, return as-is
    if (raw.size() >= 3 &&
        (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB &&
        (unsigned char)raw[2] == 0xBF) {
        return std::string(raw.constData() + 3, raw.size() - 3);
    }

    // Try UTF-8 validation (MB_ERR_INVALID_CHARS rejects invalid sequences)
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            raw.constData(), (int)raw.size(), nullptr, 0) > 0) {
        return std::string(raw.constData(), raw.size());
    }

    // Fall back: Windows-1251 → UTF-16 → UTF-8
    int wlen = MultiByteToWideChar(1251, 0, raw.constData(), (int)raw.size(), nullptr, 0);
    if (wlen == 0)
        return {};
    std::vector<wchar_t> wbuf(wlen);
    MultiByteToWideChar(1251, 0, raw.constData(), (int)raw.size(), wbuf.data(), wlen);

    int utf8len = WideCharToMultiByte(CP_UTF8, 0,
                                      wbuf.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (utf8len == 0)
        return {};
    std::vector<char> utf8buf(utf8len);
    WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), wlen,
                        utf8buf.data(), utf8len, nullptr, nullptr);
    return std::string(utf8buf.data(), utf8len);
}

std::string normalizeAddress(const std::string& utf8_address) {
    // Адрес — первый токен строки; trimmed() убирает ведущие пробелы и \r.
    QString qstr = QString::fromUtf8(utf8_address.c_str()).trimmed();
    static const std::unordered_map<QChar, QChar> mapping = {
        // Заглавные кириллические -> латиница
        { QChar(0x0410), QChar('A') }, // А
        { QChar(0x0412), QChar('B') }, // В
        { QChar(0x0415), QChar('E') }, // Е
        { QChar(0x041A), QChar('K') }, // К
        { QChar(0x041C), QChar('M') }, // М
        { QChar(0x041D), QChar('H') }, // Н
        { QChar(0x041E), QChar('O') }, // О
        { QChar(0x041F), QChar('P') }, // П
        { QChar(0x0420), QChar('P') }, // Р
        { QChar(0x0421), QChar('C') }, // С
        { QChar(0x0422), QChar('T') }, // Т
        { QChar(0x0425), QChar('X') }, // Х
        // Строчные кириллические -> латиница
        { QChar(0x0430), QChar('A') }, // а
        { QChar(0x0432), QChar('B') }, // в
        { QChar(0x0435), QChar('E') }, // е
        { QChar(0x043A), QChar('K') }, // к
        { QChar(0x043C), QChar('M') }, // м
        { QChar(0x043D), QChar('H') }, // н
        { QChar(0x043E), QChar('O') }, // о
        { QChar(0x043F), QChar('P') }, // п
        { QChar(0x0440), QChar('P') }, // р
        { QChar(0x0441), QChar('C') }, // с
        { QChar(0x0442), QChar('T') }, // т
        { QChar(0x0445), QChar('X') }, // х
        // Защита от Latin-1 артефактов (Windows-1251 байт, ошибочно
        // прочитанный как Latin-1): тот же байт даёт эти кодовые точки
        { QChar(0x00C0), QChar('A') }, // À  (А, байт 0xC0)
        { QChar(0x00C2), QChar('B') }, // Â  (В, байт 0xC2)
        { QChar(0x00C5), QChar('E') }, // Å  (Е, байт 0xC5)
        { QChar(0x00CA), QChar('K') }, // Ê  (К, байт 0xCA)
        { QChar(0x00CC), QChar('M') }, // Ì  (М, байт 0xCC)
        { QChar(0x00CD), QChar('H') }, // Í  (Н, байт 0xCD)
        { QChar(0x00CE), QChar('O') }, // Î  (О, байт 0xCE)
        { QChar(0x00CF), QChar('P') }, // Ï  (П, байт 0xCF)
        { QChar(0x00D0), QChar('P') }, // Ð  (Р, байт 0xD0)
        { QChar(0x00D1), QChar('C') }, // Ñ  (С, байт 0xD1)
        { QChar(0x00D2), QChar('T') }, // Ò  (Т, байт 0xD2)
        { QChar(0x00D5), QChar('X') }  // Õ  (Х, байт 0xD5)
    };
    QString result;
    for (QChar ch : qstr) {
        if (ch.isSpace()) break;             // адрес кончился — дальше комментарий оператора
        auto it = mapping.find(ch);
        result.append(it != mapping.end() ? it->second : ch);
    }
    return result.toUpper().toStdString();
}

} // namespace encoding