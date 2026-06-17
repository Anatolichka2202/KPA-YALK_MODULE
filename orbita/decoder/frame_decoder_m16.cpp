#include "frame_decoder_m16.h"
#include "../orbita_core/logger.h"
#include <cstring>
#include <chrono>

namespace orbita {

// ----------------------------------------------------------------------
// Конструктор – инициализация всех переменных (как в Delphi)
// ----------------------------------------------------------------------
FrameDecoderM16::FrameDecoderM16(FifoBuffer& input_fifo)
    : fifo(input_fifo)
    , tlmWriter_(nullptr)
    , tlmStartTimeMs_(0)
    , tlmBlockNum_(1)
    , tlmOneGcCounter_(0)
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
    , countForMG(1)
    , countErrorMF(0)
    , countErrorMG(0)
    , totalPhrases(0)
    , totalGroups(0)
    , flagL(true)
    , iBit(1)
    , masGroup(GROUP_SIZE, 0)
    , masGroupAll(GROUP_SIZE, 0)
    , hasNewGroup(false)
    , current_time_seconds_(0)
    , time_updated_(false)
    , time_decode_index_(0)
{
}

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
    countForMG = 1;
    countErrorMF = 0;
    countErrorMG = 0;
    totalPhrases = 0;
    totalGroups = 0;
    flagL = true;
    iBit = 1;
    hasNewGroup = false;
    std::fill(masGroup.begin(), masGroup.end(), 0);
    std::fill(masGroupAll.begin(), masGroupAll.end(), 0);
    if (tlmWriter_) {
        tlmCycleBuffer_.clear();
        tlmWriter_.reset();
    }
    flBeg = false;
    flSinxC = false;
    tlmOneGcCounter_ = 0;

    current_time_seconds_ = 0;
    time_updated_ = false;
    time_decode_index_ = 0;
    memset(time_decode_buf_, 0, sizeof(time_decode_buf_));
}

// ----------------------------------------------------------------------
// Чтение одного бита (с удалением)
// ----------------------------------------------------------------------
uint8_t FrameDecoderM16::Read() {
    return fifo.pop();
}

// ----------------------------------------------------------------------
// Чтение бита со смещением (без удаления) – offset 1..360
// ----------------------------------------------------------------------
uint8_t FrameDecoderM16::Read(int offset) {
    // В Delphi offset-1 = индекс от текущей позиции (0 = текущий бит)
    return fifo.peek(offset - 1);
}

// ----------------------------------------------------------------------
// Вывод ошибок фраз (логирование)
// ----------------------------------------------------------------------
void FrameDecoderM16::OutMF(int err) {
    totalPhrases += countForMF;
    // Можно вывести процент: qDebug() << "Phrase errors:" << err;
}

void FrameDecoderM16::OutMG(int err) {
    totalGroups += countForMG;
    // qDebug() << "Group errors:" << err;
}


// ----------------------------------------------------------------------
// Поиск первого маркера фразы
// ----------------------------------------------------------------------
void FrameDecoderM16::SearchFirstFraseMarker() {
    uint8_t current = Read();
    countForMF++;

    if ((current == 0) && (Read(24) == 1) && (Read(48) == 1) &&
        (Read(72) == 1) && (Read(96) == 1) && (Read(120) == 0) &&
        (Read(144) == 0) && (Read(168) == 0) && (Read(216) == 1) &&
        (Read(240) == 0) && (Read(264) == 0) && (Read(288) == 1) &&
        (Read(312) == 1) && (Read(336) == 0) && (Read(360) == 1)) {
        firstFraseFl = true;
        pointCount = 383;
        fifo.rewind(1);      // откат на один бит (как dec(fifoLevelRead))
        iBit = 1;
        codStr = 0;
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
// Заполнение бита в слово
// ----------------------------------------------------------------------
void FrameDecoderM16::FillBitInWord() {
    uint8_t bit = Read();
    if (bit)
        codStr = (codStr << 1) | 1;
    else
        codStr = (codStr << 1);
}

// ----------------------------------------------------------------------
// Заполнение массива группы
// ----------------------------------------------------------------------
void FrameDecoderM16::FillArrayGroup() {
    // В Delphi: masGroup[groupWordCount] := codStr and 2047; masGroupAll[...] := codStr;
    if (groupWordCount <= 100) {
        //qDebug() << "Word" << groupWordCount << "codStr" << Qt::hex << codStr;
    }
    masGroup[groupWordCount - 1] = codStr & 0x7FF;
    masGroupAll[groupWordCount - 1] = codStr;
    groupWordCount++;
}

// ----------------------------------------------------------------------
// Сбор маркера номера группы (не используется в M16, но оставим)
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
// Сбор маркера группы (MG) и маркера цикла (MC)
// ----------------------------------------------------------------------
void FrameDecoderM16::CollectMarkGroup() {
    // В оригинале вызывается для чётных фраз, проверка (myFraseNum mod 2 = 0) делается снаружи
    if ((codStr & 0x800) != 0)
        markerGroup = (markerGroup << 1) | 1;
    else
        markerGroup = (markerGroup << 1);
}

// ----------------------------------------------------------------------
// Обработка собранного слова (вызывается только при iBit == 12)
// ----------------------------------------------------------------------
void FrameDecoderM16::WordProcessing() {
    // Мы вызываем метод только когда iBit == 12
    if (iBit != 12) return;

    if (wordNum == 1) {
        // Начало новой фразы
        if (flagOutFraseNum) {
            // Поиск маркера кадра (1 в 12-м бите слова 1, фраза 16, группа 1)
            if ((wordNum == 1) && (fraseNum == 16) && (groupNum == 1)) {
                if ((codStr & 0x800) != 0) {
                    ciklNum = 1;
                    flKadr = true;

                    LOG_DEBUG("[]: marker kadr found");
                }
            }
        }

        // Сбор маркеров группы/цикла на чётных фразах
        if ((myFraseNum & 1) == 0) {
            CollectMarkNumGroup();
            CollectMarkGroup();
            countEvenFraseMGToMG++;

            if ((markerGroup == 114) || (markerGroup == 141)) {
                if (fraseNum != 128) {
                    if (flagL) fraseNum = 128;
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

                        if (tlmWriter_ && flKadr) {
                            flBeg = true;
                        }

                        flagOutFraseNum = true;
                    }
                    markerGroup = 0;
                }
            }
        }

        myFraseNum++;
        if (myFraseNum == 129) myFraseNum = 1;
        fraseNum++;
        if (fraseNum == 129) fraseNum = 1;
    }


    // Запись в массив группы, если разрешено
    if (startWriteMasGroup) {
        FillArrayGroup();

        if (flSinxC) {
            FillArrayCircle();   // здесь накапливается цикл
        }

        if (groupWordCount == GROUP_SIZE + 1) {
            if (groupCallback) groupCallback(masGroup, masGroupAll, groupNum, ciklNum);
            hasNewGroup = true;
            groupWordCount = 1;
        }
    }

    // Сброс текущего слова и переход к следующему
    codStr = 0;
    wordNum++;
    if (wordNum == 17) wordNum = 1;
}

// ----------------------------------------------------------------------
// Анализ фразы – основной цикл сбора слов
// ----------------------------------------------------------------------
void FrameDecoderM16::AnalyseFrase() {
    // Блок flBeg (для синхронизации записи в цикл)
    if (flBeg) {
        if ((wordNum == 1) && (fraseNum == 1) && (groupNum == 1) && (ciklNum == 1)) {
            flSinxC = true;
        }
    }

    // Блок flKadr (разрешение записи в группу)
    if (flKadr) {
        if ((wordNum == 1) && (fraseNum == 1) && (groupNum == 1) && (ciklNum == 1)) {
            startWriteMasGroup = true;
        }
    }

    // Цикл сбора 12 бит слова
    while (iBit <= 12) {
        if (pointCount == -1) {
            firstFraseFl = false;
            iBit = 1;
            break;
        }
        pointCount--;
        FillBitInWord();    // читает бит, добавляет в codStr, но не меняет iBit
        // В оригинале WordProcessing вызывается на каждом бите, но внутри проверяет iBit == 12
        WordProcessing();
        iBit++;
        if (iBit == 13) iBit = 1;
    }
}

// ----------------------------------------------------------------------
// Основной цикл обработки (вызывается, когда в буфере есть данные)
// ----------------------------------------------------------------------
void FrameDecoderM16::process() {
    // В Delphi: while (fifoBufCount >= 100000) and (not flagEnd) do
    static int callCount = 0;

    while (fifo.available() > 383) {
        if (!firstFraseFl) {
            SearchFirstFraseMarker();
        } else {
            AnalyseFrase();
            if (!firstFraseFl) continue;
        }
        // При необходимости добавить проверку внешнего флага остановки
    }
}

void FrameDecoderM16::setGroupCallback(GroupCallback cb) {
    groupCallback = std::move(cb);
}

bool FrameDecoderM16::hasGroup() const {
    return hasNewGroup;
}

// Получить последнюю готовую группу (11-битные слова)
std::vector<uint16_t> FrameDecoderM16::getGroup() {
    hasNewGroup = false;
    return masGroup;   // возвращаем 11-битные слова
}

// Получить проценты ошибок (маркер фразы и маркер группы)
void FrameDecoderM16::getErrors(int& phrase_err, int& group_err) const {
    phrase_err = countErrorMF;
    group_err = countErrorMG;
}

//TLM

void FrameDecoderM16::startTlm(const std::string& filename) {
    tlmWriter_ = std::make_unique<TlmWriter>(filename, 16);
    tlmCycleBuffer_.clear();
    tlmCycleBuffer_.reserve(masCircleSize);
    tlmStartTimeMs_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch()).count();
    tlmBlockNum_ = 1;
    tlmOneGcCounter_ = 4;          // Delphi: iOneGC := 4
    // Сброс флагов (как в Delphi при старте записи, но не при остановке)
    flBeg = false;
    flSinxC = false;
    // flKadr, flagOutFraseNum остаются – они придут из сигнала
}

void FrameDecoderM16::stopTlm() {
    if (tlmWriter_) {
        // Неполный цикл не пишем – как в Delphi
        tlmWriter_.reset();
    }
    tlmCycleBuffer_.clear();
}

// ----------------------------------------------------------------------
// Заполнение кольцевого буфера цикла (аналог FillArrayCircle)
// ----------------------------------------------------------------------
void FrameDecoderM16::FillArrayCircle() {
    if (!tlmWriter_ || !flSinxC) return; // только при активной записи и разрешённой синхронизации

    uint16_t word = codStr;               // 12-битное слово
    // Установка метки 1 Гц (бит 4096) каждые 4 слова
    if (tlmOneGcCounter_ == 4) {
        word |= 0x1000;
        tlmOneGcCounter_ = 1;
    } else {
        tlmOneGcCounter_++;
    }
    // Бит начала цикла (32768) не устанавливаем (в Delphi закомментировано)

    if (tlmCycleBuffer_.size() < 50) {
       // qDebug() << "raw codStr:" << Qt::hex << codStr;
    }

    tlmCycleBuffer_.push_back(word);

    if (tlmCycleBuffer_.size() == masCircleSize ) { // полный цикл (masCircleSize)
        uint32_t elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch()).count() - tlmStartTimeMs_;
        tlmWriter_->writeBlock(tlmBlockNum_, elapsedMs, tlmCycleBuffer_);
        tlmBlockNum_++;
        tlmCycleBuffer_.clear();
    }
}

} // namespace orbita