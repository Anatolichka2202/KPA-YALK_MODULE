#include "adapter_monitor_widget.h"

#include "qcustomplot.h"

#include <QComboBox>
#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace {
QString valueFor(const std::string& response, const char* key)
{
    const std::string prefix = std::string(key) + "=";
    const auto begin = response.find(prefix);
    if (begin == std::string::npos) return {};
    const auto valueBegin = begin + prefix.size();
    const auto end = response.find('\n', valueBegin);
    return QString::fromStdString(response.substr(valueBegin, end - valueBegin));
}
}

AdapterMonitorWidget::AdapterMonitorWidget(ReadFrame readFrame, QWidget* parent)
    : QWidget(parent), readFrame_(std::move(readFrame))
{
    setWindowTitle(QStringLiteral("ЯЛК / УЛК — живой поток адаптера"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1000, 690);
    setStyleSheet("QWidget{background:#14171c;color:#dfe6ee;}"
                  "QTableWidget{background:#0e1115; gridline-color:#29313b;}"
                  "QComboBox{background:#1b1f26;border:1px solid #2a313b;padding:3px;}");

    auto* layout = new QVBoxLayout(this);
    auto* top = new QHBoxLayout;
    status_ = new QLabel(QStringLiteral("Ожидание UDP-пакета…"));
    status_->setStyleSheet("color:#d99a4a;font-weight:bold;");
    value_ = new QLabel(QStringLiteral("Слово: —"));
    wordSelector_ = new QComboBox;
    for (int index = 0; index < 100; ++index)
        wordSelector_->addItem(QStringLiteral("Слово %1").arg(index), index);
    top->addWidget(status_, 1);
    top->addWidget(new QLabel(QStringLiteral("График:")));
    top->addWidget(wordSelector_);
    top->addWidget(value_);
    layout->addLayout(top);

    auto* note = new QLabel(QStringLiteral(
        "Пассивное чтение: команды ИСД и адаптеру не посылаются. "
        "Пока показаны коды; перевод в В выполняется после привязки канала и калибровки."));
    note->setStyleSheet("color:#8b95a3;");
    note->setWordWrap(true);
    layout->addWidget(note);

    plot_ = new QCustomPlot(this);
    plot_->addGraph();
    plot_->graph(0)->setPen(QPen(QColor("#4aa3df"), 2));
    plot_->xAxis->setLabel(QStringLiteral("Время, с"));
    plot_->yAxis->setLabel(QStringLiteral("Код слова"));
    plot_->yAxis->setRange(0, 65535);
    plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    layout->addWidget(plot_, 2);

    table_ = new QTableWidget(10, 10, this);
    for (int row = 0; row < 10; ++row)
        for (int column = 0; column < 10; ++column) {
            const int index = row * 10 + column;
            table_->setItem(row, column, new QTableWidgetItem(QStringLiteral("—")));
            table_->item(row, column)->setToolTip(QStringLiteral("Слово %1").arg(index));
        }
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setHorizontalHeaderLabels(QStringList{"0","1","2","3","4","5","6","7","8","9"});
    table_->setVerticalHeaderLabels(QStringList{"0–9","10–19","20–29","30–39","40–49","50–59","60–69","70–79","80–89","90–99"});
    layout->addWidget(table_, 1);

    connect(&timer_, &QTimer::timeout, this, &AdapterMonitorWidget::readNextFrame);
    connect(wordSelector_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AdapterMonitorWidget::selectWord);
    startSeconds_ = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    timer_.start(80);
}

void AdapterMonitorWidget::selectWord(int index)
{
    selectedWord_ = index;
    x_.clear();
    y_.clear();
    plot_->graph(0)->data()->clear();
    plot_->replot();
}

std::vector<unsigned> AdapterMonitorWidget::parseWords(const std::string& response)
{
    const auto text = valueFor(response, "words").toStdString();
    if (text.empty()) throw std::runtime_error("Ответ адаптера не содержит слов");
    std::vector<unsigned> values;
    std::istringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) values.push_back(static_cast<unsigned>(std::stoul(token)));
    if (values.size() != 100) throw std::runtime_error("Ожидалось 100 слов, получено " + std::to_string(values.size()));
    return values;
}

void AdapterMonitorWidget::readNextFrame()
{
    try {
        showFrame(readFrame_());
    } catch (const std::exception& error) {
        status_->setText(QStringLiteral("Нет данных: %1").arg(QString::fromUtf8(error.what())));
        status_->setStyleSheet("color:#cf5b52;font-weight:bold;");
    }
}

void AdapterMonitorWidget::showFrame(const std::string& response)
{
    const auto words = parseWords(response);
    status_->setText(QStringLiteral("Поток принят: %1 байта, заголовок %2")
        .arg(valueFor(response, "size"), valueFor(response, "header")));
    status_->setStyleSheet("color:#5fc58a;font-weight:bold;");
    for (int index = 0; index < 100; ++index) {
        auto* item = table_->item(index / 10, index % 10);
        item->setText(QString::number(words[static_cast<std::size_t>(index)]));
        item->setBackground(index == selectedWord_ ? QColor("#263b50") : QColor("#0e1115"));
    }
    const unsigned current = words[static_cast<std::size_t>(selectedWord_)];
    value_->setText(QStringLiteral("Слово %1: %2 (10 бит: %3)")
        .arg(selectedWord_).arg(current).arg(current & 0x03ff));
    const double seconds = QDateTime::currentMSecsSinceEpoch() / 1000.0 - startSeconds_;
    x_.push_back(seconds);
    y_.push_back(current);
    constexpr std::size_t maxSamples = 500;
    if (x_.size() > maxSamples) { x_.erase(x_.begin()); y_.erase(y_.begin()); }
    QVector<double> xs(x_.begin(), x_.end());
    QVector<double> ys(y_.begin(), y_.end());
    plot_->graph(0)->setData(xs, ys);
    plot_->xAxis->setRange(std::max(0.0, seconds - 20.0), std::max(20.0, seconds));
    const auto [min, max] = std::minmax_element(y_.begin(), y_.end());
    const double padding = std::max(2.0, (*max - *min) * 0.15);
    plot_->yAxis->setRange(std::max(0.0, *min - padding), std::min(65535.0, *max + padding));
    plot_->replot(QCustomPlot::rpQueuedReplot);
}
