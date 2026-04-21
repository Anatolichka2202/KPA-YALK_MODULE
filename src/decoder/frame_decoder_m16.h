#ifndef FRAME_DECODER_M16_H
#define FRAME_DECODER_M16_H

#include <vector>
#include <cstdint>
#include <functional>
#include "fifo_buffer.h"
#include "frame_decoder.h"
namespace orbita {

class FrameDecoderM16 : public FrameDecoder {
public:
    explicit FrameDecoderM16(FifoBuffer& input_fifo);
    void process();                 // вызывать циклически
    void reset();

    // Callback при заполнении группы (2048 слов)
    using GroupCallback = std::function<void(const std::vector<uint16_t>& group_words_11bit,
                                             const std::vector<uint16_t>& group_words_12bit)>;
    void setGroupCallback(GroupCallback cb);

    // Статистика ошибок (проценты)
    void getErrors(int& phrase_err_percent, int& group_err_percent);

#ifdef TESTING
public:
    // Тестовые геттеры для проверки внутреннего состояния
    bool isFirstFraseFound() const { return firstFraseFl; }
    int  getPointCount() const { return pointCount; }
    int  getWordNum() const { return wordNum; }
    int  getFraseNum() const { return fraseNum; }
    int  getGroupNum() const { return groupNum; }
    int  getCiklNum() const { return ciklNum; }
    bool isStartWriteMasGroup() const { return startWriteMasGroup; }
    bool isFlKadr() const { return flKadr; }
    bool isFlagOutFraseNum() const { return flagOutFraseNum; }
    uint8_t getMarkerGroup() const { return markerGroup; }
    int getCountEvenFraseMGToMG() const { return countEvenFraseMGToMG; }
    bool getFlagL() const { return flagL; }
    int getMyFraseNum() const { return myFraseNum; }
    uint16_t getCodStr() const { return codStr; }
    void publicSearchFirstFraseMarker() { SearchFirstFraseMarker(); }
    void publicAnalyseFrase()        { AnalyseFrase(); }
#endif

    // Реализация чисто виртуальных методов базового класса
    bool hasGroup() const override;
    std::vector<uint16_t> getGroup() override;
    void getErrors(int& phrase_err, int& group_err) const override;

private:
    // ---------- Прямые аналоги переменных из Delphi ----------
    FifoBuffer& fifo;               // кольцевой буфер битов

    bool firstFraseFl;             // firstFraseFl
    int pointCount;                // pointCount
    int wordNum;                   // wordNum (1..16)
    int fraseNum;                  // fraseNum (1..128)
    int groupNum;                  // groupNum (1..32)
    int ciklNum;                   // ciklNum (1..4)
    int groupWordCount;            // groupWordCount (1..2048)
    int myFraseNum;                // myFraseNum (1..128)
    uint16_t codStr;               // codStr (текущее 12-битное слово)

    bool startWriteMasGroup;       // startWriteMasGroup
    bool flSinxC;                  // flSinxC
    bool flKadr;                   // flKadr
    bool flBeg;                    // flBeg
    bool flagOutFraseNum;          // flagOutFraseNum

    uint8_t markerNumGroup;        // markerNumGroup
    uint8_t markerGroup;           // markerGroup
    int countEvenFraseMGToMG;      // countEvenFraseMGToMG
    int countForMF;                // countForMF
    int countForMG;                // countForMG
    int countErrorMF;              // countErrorMF
    int countErrorMG;              // countErrorMG
    int totalPhrases;              // для подсчёта процентов
    int totalGroups;               // для подсчёта процентов

    bool flagL;                    // flagL (из Delphi, изначально true)
    int iBit;                      // iBit (1..12)

    // Буферы группы (2048 слов)
    static const int GROUP_SIZE = 2048;
    std::vector<uint16_t> masGroup;      // 11 младших бит
    std::vector<uint16_t> masGroupAll;   // полные 12 бит

    GroupCallback groupCallback;
    bool hasNewGroup;

    // ---------- Вспомогательные методы (прямой перевод Delphi) ----------
    uint8_t Read();                 // чтение одного бита из fifo и сдвиг указателя
    uint8_t Read(int offset);      // чтение бита со смещением (без сдвига)
    void OutMF(int err);           // вывод ошибок фраз (можно логировать)
    void OutMG(int err);           // вывод ошибок групп

    // Прямые аналоги процедур Delphi
    void SearchFirstFraseMarker();
    void AnalyseFrase();
    void FillBitInWord();
    void WordProcessing();
    void FillArrayGroup();
    void FillArrayCircle();        // заглушка для TLM
    void CollectMarkNumGroup();
    void CollectMarkGroup();
};

} // namespace orbita

#endif
