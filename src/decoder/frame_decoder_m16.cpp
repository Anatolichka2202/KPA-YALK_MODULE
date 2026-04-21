#include "frame_decoder_m16.h"
#include <algorithm>
#include <cstdio>   // для printf отладки
#include <QDebug>
namespace orbita {

// ----------------------------------------------------------------------
// Конструктор – инициализация всех переменных как в Delphi
// ----------------------------------------------------------------------
FrameDecoderM16::FrameDecoderM16(FifoBuffer& input_fifo)
    : fifo(input_fifo)
    , firstFraseFl(false)
    , pointCount(0)
    , wordNum(1)
    , fraseNum(1)
    , groupNum(1)
    , ciklNum(1)
    , groupWordCount(1)
    , myFraseNum(1)
    , codStr(0)
    , startWriteMasGroup(false)
    , flSinxC(false)
    , flKadr(false)
    , flBeg(false)
    , flagOutFraseNum(false)
    , markerNumGroup(0)
    , markerGroup(0)
    , countEvenFraseMGToMG(0)
    , countForMF(0)
    , countForMG(0)
    , countErrorMF(0)
    , countErrorMG(0)
    , totalPhrases(0)
    , totalGroups(0)
    , flagL(true)
    , iBit(1)
    , masGroup(GROUP_SIZE, 0)
    , masGroupAll(GROUP_SIZE, 0)
    , hasNewGroup(false)
{
}

// ----------------------------------------------------------------------
// Сброс состояния (как в конструкторе)
// ----------------------------------------------------------------------
void FrameDecoderM16::reset() {
    firstFraseFl = false;
    pointCount = 0;
    wordNum = 1;
    fraseNum = 1;
    groupNum = 1;
    ciklNum = 1;
    groupWordCount = 1;
    myFraseNum = 1;
    codStr = 0;
    startWriteMasGroup = false;
    flSinxC = false;
    flKadr = false;
    flBeg = false;
    flagOutFraseNum = false;
    markerNumGroup = 0;
    markerGroup = 0;
    countEvenFraseMGToMG = 0;
    countForMF = 0;
    countForMG = 0;
    countErrorMF = 0;
    countErrorMG = 0;
    totalPhrases = 0;
    totalGroups = 0;
    flagL = true;
    iBit = 1;
    hasNewGroup = false;
    std::fill(masGroup.begin(), masGroup.end(), 0);
    std::fill(masGroupAll.begin(), masGroupAll.end(), 0);
}

// ----------------------------------------------------------------------
// Чтение одного бита из FIFO (как функция Read в Delphi)
// ----------------------------------------------------------------------
uint8_t FrameDecoderM16::Read() {
    return fifo.pop();
}

// ----------------------------------------------------------------------
// Чтение бита со смещением (без изменения указателя чтения)
// offset – количество битов вперёд от текущей позиции чтения
// ----------------------------------------------------------------------
uint8_t FrameDecoderM16::Read(int offset) {
    // В Delphi используется fifoLevelRead + offset с циклическим пересчётом.
    // Здесь предполагаем, что fifo.peek(offset) даёт бит на offset позиций вперёд.
    return fifo.peek(offset);
}

// ----------------------------------------------------------------------
// Вывод ошибок фраз (аналог OutMF) – можно заменить на лог
// ----------------------------------------------------------------------
void FrameDecoderM16::OutMF(int err) {
    // Здесь можно вывести процент ошибок или просто сбросить счётчики.
    // В Delphi выводится на прогресс-бар.
    totalPhrases += countForMF;
    // Процент ошибок можно вычислить позже в getErrors()
}

// ----------------------------------------------------------------------
// Вывод ошибок групп (аналог OutMG)
// ----------------------------------------------------------------------
void FrameDecoderM16::OutMG(int err) {
    totalGroups += countForMG;
}

// ----------------------------------------------------------------------
// Поиск маркера первой фразы (SearchFirstFraseMarker)
// ----------------------------------------------------------------------
void FrameDecoderM16::SearchFirstFraseMarker() {
    uint8_t current = Read();

    // Отладка
    static int dbg_count = 0;
    if (dbg_count < 10) {
        qDebug() << "SearchFirstFraseMarker: current=" << (int)current
                 << " b24=" << (int)Read(24)
                 << " b48=" << (int)Read(48)
                 << " b72=" << (int)Read(72)
                 << " b96=" << (int)Read(96)
                 << " b120=" << (int)Read(120)
                 << " b144=" << (int)Read(144)
                 << " b168=" << (int)Read(168)
                 << " b216=" << (int)Read(216)
                 << " b240=" << (int)Read(240)
                 << " b264=" << (int)Read(264)
                 << " b288=" << (int)Read(288)
                 << " b312=" << (int)Read(312)
                 << " b336=" << (int)Read(336)
                 << " b360=" << (int)Read(360);
        dbg_count++;
    }

    countForMF++;

    // Проверка битов на позициях 0,24,48,... (как в Delphi)
    if ((current == 0) && (Read(24) == 1) && (Read(48) == 1) &&
        (Read(72) == 1) && (Read(96) == 1) && (Read(120) == 0) &&
        (Read(144) == 0) && (Read(168) == 0) && (Read(216) == 1) &&
        (Read(240) == 0) && (Read(264) == 0) && (Read(288) == 1) &&
        (Read(312) == 1) && (Read(336) == 0) && (Read(360) == 1)) {
        firstFraseFl = true;
        pointCount = 383;
        // Откат на один бит назад (dec(fifoLevelRead); inc(fifoBufCount))
        fifo.rewind(1);
    } else {
        countErrorMF++;
    }

    if (countForMF == 127) {
        countForMF = 0;
        OutMF(countErrorMF);
        countErrorMF = 0;
    }
}

// ----------------------------------------------------------------------
// Заполнение бита в слово (FillBitInWord)
// ----------------------------------------------------------------------
void FrameDecoderM16::FillBitInWord() {
    uint8_t bit = Read();
    if (bit == 1)
        codStr = (codStr << 1) | 1;
    else
        codStr = (codStr << 1);
}

// ----------------------------------------------------------------------
// Заполнение массива группы (FillArrayGroup)
// ----------------------------------------------------------------------
void FrameDecoderM16::FillArrayGroup() {
    // В Delphi: wordInfo := (codStr and 2047); masGroupAll[groupWordCount] := codStr; masGroup[groupWordCount] := wordInfo;
    uint16_t wordInfo = codStr & 0x7FF;          // 11 младших бит
    masGroupAll[groupWordCount - 1] = codStr;    // 0-индексация
    masGroup[groupWordCount - 1] = wordInfo;
    groupWordCount++;
}

// ----------------------------------------------------------------------
// Заполнение кольцевого буфера цикла (заглушка для TLM)
// ----------------------------------------------------------------------
void FrameDecoderM16::FillArrayCircle() {
    // Пока не нужно
}

// ----------------------------------------------------------------------
// Сбор маркера номера группы (CollectMarkNumGroup)
// ----------------------------------------------------------------------
void FrameDecoderM16::CollectMarkNumGroup() {
    if ((fraseNum == 2) || (fraseNum == 4) || (fraseNum == 6) ||
        (fraseNum == 8) || (fraseNum == 10) || (fraseNum == 12) || (fraseNum == 14)) {
        if ((codStr & 0x800) != 0)
            markerNumGroup = (markerNumGroup << 1) | 1;
        else
            markerNumGroup = (markerNumGroup << 1);
        if (fraseNum == 14) markerNumGroup = 0;
    }
}

// ----------------------------------------------------------------------
// Сбор маркера группы (CollectMarkGroup)
// ----------------------------------------------------------------------
void FrameDecoderM16::CollectMarkGroup() {
    if ((myFraseNum & 1) == 0) {   // чётная фраза
        if ((codStr & 0x800) != 0)
            markerGroup = (markerGroup << 1) | 1;
        else
            markerGroup = (markerGroup << 1);
    }
}

// ----------------------------------------------------------------------
// Обработка собранного слова (WordProcessing)
// ----------------------------------------------------------------------
void FrameDecoderM16::WordProcessing() {
    if (wordNum == 1) {
        // Начало новой фразы
        fraseNum++;
        if (fraseNum == 129) fraseNum = 1;

        // Маркер кадра (1 в 1 бите 1 слова 16 фразы 1 группы)
        if ((fraseNum == 16) && (groupNum == 1)) {
            if ((codStr & 1) != 0) {      //
                ciklNum = 1;
                flKadr = true;
            }
        }

        // Чётные фразы – сбор маркеров
        if ((myFraseNum & 1) == 0) {
            CollectMarkNumGroup();
            CollectMarkGroup();
            countEvenFraseMGToMG++;

            if ((markerGroup == 114) || (markerGroup == 141)) {
                if (fraseNum != 128) {
                    if (flagL == true) fraseNum = 128;
                    flagL = true;
                }
                if (fraseNum == 128) {
                    flagL = false;
                    if (markerGroup == 114) {
                        // Маркер группы
                        if (countEvenFraseMGToMG != 64) countErrorMG++;
                        countEvenFraseMGToMG = 0;
                        groupNum++;
                        if (groupNum == 33) groupNum = 1;
                        countForMG++;
                        if (countForMG == 32) {
                            OutMG(countErrorMG);
                            countErrorMG = 0;
                            countForMG = 1;
                        }
                        flagOutFraseNum = true;
                    }
                    if (markerGroup == 141) {
                        // Маркер цикла
                        countEvenFraseMGToMG = 0;
                        groupNum = 1;
                        ciklNum++;
                        if (ciklNum == 5) ciklNum = 1;
                        flagOutFraseNum = true;
                        // В Delphi здесь проверка (tlm.flagWriteTLM) and (flKadr) – можно опустить
                        // if (tlm.flagWriteTLM && flKadr) flBeg = true;
                    }
                    markerGroup = 0;
                }
            }
        }

        myFraseNum++;
        if (myFraseNum == 129) myFraseNum = 1;

        fraseNum++;
        if (fraseNum == 129) fraseNum = 1;

        if (flagOutFraseNum && flKadr && fraseNum == 1 && groupNum == 1 && ciklNum == 1) {
            startWriteMasGroup = true;
        }
    }

    // Запись в группу, если разрешено
    if (startWriteMasGroup) {
        FillArrayGroup();
        if (groupWordCount == GROUP_SIZE + 1) {
            // Группа заполнена – вызываем колбэк
            if (groupCallback) {
                groupCallback(masGroup, masGroupAll);
            }
            hasNewGroup = true;
            groupWordCount = 1;   // сброс для следующей группы
        }
    }

    // Сброс текущего слова и переход к следующему
    codStr = 0;
    wordNum++;
    if (wordNum == 17) wordNum = 1;
}

// ----------------------------------------------------------------------
// Анализ фразы (AnalyseFrase)
// ----------------------------------------------------------------------
void FrameDecoderM16::AnalyseFrase() {
    while (iBit <= 12) {
        if (pointCount == -1) {
            firstFraseFl = false;
            iBit = 1;
            break;
        }
        pointCount--;
        FillBitInWord();
        if (iBit == 12) {
            WordProcessing();
            iBit = 1;
        } else {
            iBit++;
        }
    }
}

// ----------------------------------------------------------------------
// Основной цикл обработки (process)
// ----------------------------------------------------------------------
void FrameDecoderM16::process() {
    // В Delphi: while (fifoBufCount >= 100000) and (not flagEnd) do
    // Здесь мы обрабатываем, пока есть достаточно битов (минимум для поиска маркера)
    while (fifo.available() >= 384) {
        if (!firstFraseFl) {
            SearchFirstFraseMarker();
        } else {
            AnalyseFrase();
            if (!firstFraseFl) break; // если синхронизация потеряна
        }
    }
}

// ----------------------------------------------------------------------
// Установка колбэка
// ----------------------------------------------------------------------
void FrameDecoderM16::setGroupCallback(GroupCallback cb) {
    groupCallback = std::move(cb);
}

// ----------------------------------------------------------------------
// Получение статистики ошибок (в процентах)
// ----------------------------------------------------------------------
void FrameDecoderM16::getErrors(int& phrase_err_percent, int& group_err_percent) {
    phrase_err_percent = (totalPhrases > 0) ? (countErrorMF * 100 / totalPhrases) : 0;
    group_err_percent  = (totalGroups  > 0) ? (countErrorMG * 100 / totalGroups)  : 0;
}

// ----------------------------------------------------------------------
// Реализация методов базового класса FrameDecoder
// ----------------------------------------------------------------------
bool FrameDecoderM16::hasGroup() const {
    return hasNewGroup;
}

std::vector<uint16_t> FrameDecoderM16::getGroup() {
    hasNewGroup = false;
    // Возвращаем 11-битные слова (как в оригинальном getGroup)
    return masGroup;
}

void FrameDecoderM16::getErrors(int& phrase_err, int& group_err) const {
    phrase_err = (totalPhrases > 0) ? (countErrorMF * 100 / totalPhrases) : 0;
    group_err  = (totalGroups  > 0) ? (countErrorMG * 100 / totalGroups)  : 0;
}

} // namespace orbita
