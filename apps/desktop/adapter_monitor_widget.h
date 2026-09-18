#ifndef ADAPTER_MONITOR_WIDGET_H
#define ADAPTER_MONITOR_WIDGET_H

#include <QWidget>
#include <QTimer>
#include <functional>
#include <vector>

class QLabel;
class QComboBox;
class QTableWidget;
class QCustomPlot;

// Инженерский монитор живого потока адаптера ЯЛК/УЛК. Его единственная
// команда к плагину — read_frame, то есть чтение уже поступающего UDP-кадра.
class AdapterMonitorWidget final : public QWidget
{
    Q_OBJECT
public:
    using ReadFrame = std::function<std::string()>;
    explicit AdapterMonitorWidget(ReadFrame readFrame, QWidget* parent = nullptr);

private slots:
    void readNextFrame();
    void selectWord(int index);

private:
    void showFrame(const std::string& response);
    static std::vector<unsigned> parseWords(const std::string& response);

    ReadFrame readFrame_;
    QTimer timer_;
    QLabel* status_ = nullptr;
    QLabel* value_ = nullptr;
    QComboBox* wordSelector_ = nullptr;
    QTableWidget* table_ = nullptr;
    QCustomPlot* plot_ = nullptr;
    int selectedWord_ = 0;
    double startSeconds_ = 0.0;
    std::vector<double> x_;
    std::vector<double> y_;
};

#endif
