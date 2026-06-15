#ifndef FRAME_DECODER_M16_H
#define FRAME_DECODER_M16_H

#include <cstdint>
#include <vector>
#include <functional>
#include <string>
#include <memory>

#include "fifo_buffer.h"
#include "frame_decoder.h"
#include "../tlm/tlm_writer.h"
namespace orbita {

class FrameDecoderM16 : public FrameDecoder {
public:
    explicit FrameDecoderM16(FifoBuffer& fifo);

    // Основной метод обработки – вызывать в цикле, пока есть данные
    void process() override;

    // Сброс состояния (как при reinit)
    void reset() override;

    // Callback при заполнении группы (2048 слов)
    using GroupCallback = std::function<void(const std::vector<uint16_t>& group_11bit,
                                             const std::vector<uint16_t>& group_12bit,
                                             int groupNum,  // 1..32
                                             int ciklNum)>; //..4
    void setGroupCallback(GroupCallback cb);

    // Статистика ошибок (количество ошибок, проценты можно вычислить отдельно)
    int getPhraseErrors() const { return countErrorMF; }
    int getGroupErrors() const { return countErrorMG; }
    int getTotalPhrases() const { return totalPhrases; }
    int getTotalGroups() const { return totalGroups; }

    //Управление записью tlm
    void startTlm(const std::string& filename);
    void stopTlm();
    bool isTlmActive() const {return tlmWriter_!=nullptr;}

    //полчуение бортового времени
    uint32_t getCurrentTimeSeconds() const { return current_time_seconds_; }
    bool hasNewTime() const { return time_updated_; }
    void clearTimeFlag() { time_updated_ = false; }

    // Получить проценты ошибок (маркер фразы и маркер группы)
    void getErrors(int& phrase_err, int& group_err) const override;
private:
    // ---------- Аналоги полей Delphi ----------
    FifoBuffer& fifo;

    //маркеры синхронизации
    bool firstFraseFl;      // маркер синхронизации (первое нахождение маркера фразы)
    int  pointCount;
    int  wordNum;           // 1..16
    int  fraseNum;          // 1..128
    int  groupNum;          // 1..32
    int  ciklNum;           // 1..4
    int  groupWordCount;    // 1..2048
    int  myFraseNum;        // 1..128
    uint16_t codStr;

    bool startWriteMasGroup;
    bool flKadr;            // макрер КАДРА орбиты
    bool flagOutFraseNum;

    uint8_t markerNumGroup;
    uint8_t markerGroup;
    int countEvenFraseMGToMG;
    int countForMF;
    int countForMG;
    int countErrorMF;
    int countErrorMG;
    int totalPhrases;
    int totalGroups;

    bool flagL;
    int  iBit;              // 1..12

    // Константы
    static const int GROUP_SIZE = 2048;
    static const int masCircleSize = 65536; //кол-во слов в блоке
    std::vector<uint16_t> masGroup;     // 11 бит (0..2047)
    std::vector<uint16_t> masGroupAll;  // 12 бит (0..4095)

    GroupCallback groupCallback;
    bool hasNewGroup;

    // ---------- Методы (прямой перевод Delphi) ----------
    uint8_t Read();                 // pop() бит и сдвиг
    uint8_t Read(int offset);       // peek без сдвига (offset 1..360)
    void OutMF(int err);
    void OutMG(int err);

    void SearchFirstFraseMarker();
    void AnalyseFrase();
    void FillBitInWord();
    void WordProcessing();
    void FillArrayGroup();
    void CollectMarkNumGroup();
    void CollectMarkGroup();
    bool hasGroup() const override;

    //TLM-связные полня
    std::unique_ptr<TlmWriter> tlmWriter_; // tlm писатель
    std::vector<uint16_t> tlmCycleBuffer_; // буфер тлм записи
    int tlmOneGcCounter_;      // 1..4, аналог iOneGC
    uint32_t tlmStartTimeMs_;
    uint32_t tlmBlockNum_;
    void FillArrayCircle();        //  для TLM-записи
        //тлм маркеры
        bool flBeg;
        bool flSinxC;
       // bool needFlBeg;

    // Получить последнюю готовую группу (11-битные слова)
    std::vector<uint16_t> getGroup() override;




    //поля бортового времени
    uint32_t current_time_seconds_;
    bool time_updated_;
    uint8_t time_decode_buf_[4]; // 4 декады
    int  time_decode_index_;     // 0..3
};

} // namespace orbita

#endif