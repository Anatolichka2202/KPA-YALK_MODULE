#include "test_page.h"
#include "equipment_control_widget.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QButtonGroup>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>

#include <iterator>
#include <functional>
#include <stdexcept>
#include <utility>

namespace {

constexpr int kStandMode = 0;
constexpr int kDemoMode = 1;

struct CellInfo {
    const char* code;
    const char* purpose;
};

const CellInfo kUbsiCells[] = {
    {"ЯТП", "30 каналов сопротивления, общий вход X123"},
    {"ЯЛК-96", "80 аналоговых и контактных каналов"},
    {"УЛК+ЯТП", "последовательная проверка ЯЛК и ЯТП"}
};

class Plot : public QWidget
{
public:
    explicit Plot(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(300);
    }

    void clear()
    {
        points_.clear();
        selectedChannel_.clear();
        selectedPoint_.clear();
        update();
    }

    void addPoint(double reference, double measured, const QString& label = {})
    {
        addMeasurement(label, QString(), reference, measured, 0.0, false, true, {});
    }

    void addMeasurement(const QString& channel, const QString& point,
                        double reference, double measured, double raw,
                        bool signal, bool passed, const QString& sampleText,
                        double sampleScale = 1.0)
    {
        Sample item{channel, point, reference, measured, raw, signal, passed, {}};
        for (const auto& token : sampleText.split(',', Qt::SkipEmptyParts))
            item.samples.push_back(token.toDouble() * sampleScale);
        points_.push_back(item);
        if (!channel.isEmpty()) selectedChannel_ = channel;
        if (!point.isEmpty()) selectedPoint_ = point;
        update();
    }

    void selectChannel(const QString& channel)
    {
        if (!channel.isEmpty()) selectedChannel_ = channel;
        update();
    }

    void configure(double minimum, double maximum, const QString& maximumLabel,
                   const QString& minimumLabel, const QString& axisText,
                   const QString& referenceLegend, const QString& measuredLegend)
    {
        minimum_ = minimum;
        maximum_ = maximum;
        maximumLabel_ = maximumLabel;
        minimumLabel_ = minimumLabel;
        axisText_ = axisText;
        referenceLegend_ = referenceLegend;
        measuredLegend_ = measuredLegend;
        clear();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#0e1115"));

        if (points_.isEmpty()) {
            painter.setPen(QColor("#7e8a98"));
            painter.drawText(rect(), Qt::AlignCenter,
                QStringLiteral("Во время проверки появятся:\n"
                               "динамика выбранного канала и обзор отклонений по каналам"));
            return;
        }

        const QRectF trace = QRectF(50, 28, width() - 70, height() * 0.52);
        const QRectF overview = QRectF(50, trace.bottom() + 42, width() - 70,
                                       height() - trace.bottom() - 68);
        painter.setPen(QColor("#2c333d"));
        painter.drawRect(trace);
        painter.drawRect(overview);

        QVector<const Sample*> selected;
        for (const auto& item : points_)
            if (item.channel == selectedChannel_) selected.push_back(&item);
        if (selected.isEmpty()) selected.push_back(&points_.back());

        QVector<double> referenceTrace;
        QVector<double> measuredTrace;
        QVector<int> boundaries;
        for (const auto* item : selected) {
            const int count = qMax(1, item->samples.size());
            for (int i = 0; i < count; ++i) {
                referenceTrace.push_back(item->reference);
                measuredTrace.push_back(item->samples.isEmpty() ? item->measured : item->samples[i]);
            }
            boundaries.push_back(measuredTrace.size());
        }

        auto yFor = [&](double value) {
            const double span = qMax(0.000001, maximum_ - minimum_);
            return trace.bottom() - qBound(0.0, (value - minimum_) / span, 1.0) * trace.height();
        };
        auto drawTrace = [&](const QVector<double>& values, const QColor& color, Qt::PenStyle style) {
            if (values.isEmpty()) return;
            QPainterPath path;
            for (int i = 0; i < values.size(); ++i) {
                const double x = trace.left() + (values.size() == 1 ? trace.width() / 2.0
                    : trace.width() * i / static_cast<double>(values.size() - 1));
                const double y = yFor(values[i]);
                if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
            }
            painter.setPen(QPen(color, 2.0, style));
            painter.drawPath(path);
        };
        drawTrace(referenceTrace, QColor("#55b7ff"), Qt::DashLine);
        drawTrace(measuredTrace, QColor("#70d79b"), Qt::SolidLine);

        painter.setPen(QColor("#e6eaf0"));
        painter.drawText(QRectF(trace.left(), 4, trace.width(), 20), Qt::AlignLeft,
            QStringLiteral("Канал %1 · динамика 16 свежих кадров на каждой точке")
                .arg(selectedChannel_.isEmpty() ? QStringLiteral("—") : selectedChannel_));
        painter.setPen(QColor("#8b95a3"));
        painter.drawText(5, static_cast<int>(trace.top()) + 5, maximumLabel_);
        painter.drawText(12, static_cast<int>(trace.bottom()), minimumLabel_);
        int previous = 0;
        for (int i = 0; i < selected.size(); ++i) {
            const int end = boundaries.value(i);
            const double centerIndex = (previous + end - 1) / 2.0;
            const double x = trace.left() + (referenceTrace.size() <= 1 ? trace.width() / 2.0
                : trace.width() * centerIndex / (referenceTrace.size() - 1));
            painter.drawText(QRectF(x - 42, trace.bottom() + 3, 84, 18),
                             Qt::AlignCenter, selected[i]->point);
            previous = end;
        }
        painter.setPen(QColor("#55b7ff"));
        painter.drawText(static_cast<int>(trace.right()) - 190, 20, referenceLegend_);
        painter.setPen(QColor("#70d79b"));
        painter.drawText(static_cast<int>(trace.right()) - 92, 20, measuredLegend_);

        QVector<const Sample*> overviewItems;
        for (const auto& item : points_)
            if (item.point == selectedPoint_) overviewItems.push_back(&item);
        const int count = overviewItems.size();
        double maxError = 0.001;
        for (const auto* item : overviewItems)
            maxError = qMax(maxError, qAbs(item->measured - item->reference));
        painter.setPen(QColor("#e6eaf0"));
        painter.drawText(QRectF(overview.left(), trace.bottom() + 23, overview.width(), 18),
            Qt::AlignLeft, QStringLiteral("Все каналы · точка %1 · отклонение от эталона")
                .arg(selectedPoint_.isEmpty() ? QStringLiteral("—") : selectedPoint_));
        if (count > 0) {
            const double barWidth = qMax(2.0, overview.width() / count - 2.0);
            for (int i = 0; i < count; ++i) {
                const auto* item = overviewItems[i];
                const double error = qAbs(item->measured - item->reference);
                const double h = qMax(2.0, error / maxError * (overview.height() - 16));
                const double x = overview.left() + i * overview.width() / count + 1;
                painter.fillRect(QRectF(x, overview.bottom() - h, barWidth, h),
                                 item->passed ? QColor("#20a567") : QColor("#e05252"));
                if (i % qMax(1, count / 10) == 0) {
                    painter.setPen(QColor("#8b95a3"));
                    painter.drawText(QRectF(x - 7, overview.bottom() + 2, 28, 16),
                                     Qt::AlignCenter, item->channel);
                }
            }
        }
        painter.setPen(QColor("#8b95a3"));
        painter.drawText(QRectF(overview.right() - 230, trace.bottom() + 23, 230, 18),
            Qt::AlignRight, QStringLiteral("зелёный — норма · красный — не норма"));
    }

private:
    struct Sample {
        QString channel;
        QString point;
        double reference = 0.0;
        double measured = 0.0;
        double raw = 0.0;
        bool signal = false;
        bool passed = true;
        QVector<double> samples;
    };
    QVector<Sample> points_;
    QString selectedChannel_;
    QString selectedPoint_;
    double minimum_ = 0.0;
    double maximum_ = 6.2;
    QString maximumLabel_ = QStringLiteral("6,2 В");
    QString minimumLabel_ = QStringLiteral("0 В");
    QString axisText_ = QStringLiteral("Точки воздействия: 0 · 3,1 · 6,2 В");
    QString referenceLegend_ = QStringLiteral("— В7-78/1");
    QString measuredLegend_ = QStringLiteral("— ЯЛК");
};

QLabel* makeSectionTitle(const QString& text)
{
    auto* label = new QLabel(text);
    label->setStyleSheet("font-size:15px; font-weight:700; color:#e6eaf0; margin-top:6px;");
    return label;
}

QString acceptanceText(orbita::stand::RunVerdict verdict)
{
    switch (verdict) {
    case orbita::stand::RunVerdict::Ok: return QStringLiteral("НОРМА");
    case orbita::stand::RunVerdict::Fail: return QStringLiteral("НЕ НОРМА");
    case orbita::stand::RunVerdict::Incomplete: return QStringLiteral("НЕПОЛНАЯ");
    case orbita::stand::RunVerdict::Aborted: return QStringLiteral("ОСТАНОВЛЕНО");
    case orbita::stand::RunVerdict::Error: return QStringLiteral("ОШИБКА");
    case orbita::stand::RunVerdict::NotRun: return QStringLiteral("НЕ ВЫПОЛНЯЛОСЬ");
    }
    return QStringLiteral("ОШИБКА");
}

} // namespace

class TestPlotWidget final : public Plot
{
public:
    using Plot::Plot;
};

TestPage::TestPage(QWidget* parent) : QWidget(parent)
{
    setStyleSheet(
        "QWidget { background:#14171c; color:#e6eaf0; }"
        "QComboBox, QLineEdit, QTableWidget { background:#0e1115; color:#e6eaf0; border:1px solid #2c333d; }"
        "QComboBox { padding:7px; min-height:22px; }"
        "QHeaderView::section { background:#0e1115; color:#8b95a3; padding:6px; border:1px solid #232a33; }"
        "QPushButton { background:#1b2129; color:#c2ccd8; border:1px solid #2c333d; padding:9px 14px; border-radius:6px; }"
        "QPushButton:hover { background:#2a313b; border-color:#5e93b8; }"
        "QPushButton:disabled { color:#5b6573; background:#1c2128; border-color:#232a33; }"
        "QProgressBar { background:#1c222a; border:0; text-align:center; min-height:20px; border-radius:5px; }"
        "QProgressBar::chunk { background:#2f80ed; border-radius:5px; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 18, 14);
    root->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("Проверка УБСИ · ЯЛК-96 + ЯТП"));
    title->setStyleSheet("font-size:25px; font-weight:700; color:#f1f5f9;");
    root->addWidget(title);

    auto* subtitle = new QLabel(QStringLiteral(
        "Выберите ЯЛК или ЯТП. Во время проверки видны значения каждого канала, "
        "состояние тракта и итоговый отчёт."));
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet("color:#8b95a3; font-size:13px;");
    root->addWidget(subtitle);

    auto* selectors = new QHBoxLayout;
    auto addSelector = [&](const QString& caption, QComboBox*& combo, int stretch) {
        auto* box = new QWidget;
        auto* layout = new QVBoxLayout(box);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        auto* label = new QLabel(caption);
        label->setStyleSheet("color:#7e8a98; font-size:11px;");
        combo = new QComboBox;
        layout->addWidget(label);
        layout->addWidget(combo);
        selectors->addWidget(box, stretch);
    };

    addSelector(QStringLiteral("Объект испытания"), objectCombo_, 2);
    addSelector(QStringLiteral("Состав проверки"), scopeCombo_, 2);
    addSelector(QStringLiteral("Вид испытания"), testCombo_, 4);
    addSelector(QStringLiteral("Режим запуска"), modeCombo_, 2);
    objectCombo_->setObjectName(QStringLiteral("testObject"));
    scopeCombo_->setObjectName(QStringLiteral("testScope"));
    testCombo_->setObjectName(QStringLiteral("testType"));
    modeCombo_->setObjectName(QStringLiteral("testMode"));
    objectCombo_->addItem(QStringLiteral("УЛК · ЯЛК-96 + ЯТП"), QStringLiteral("UBSI-7"));
    objectCombo_->parentWidget()->setVisible(false);
    modeCombo_->addItem(QStringLiteral("Стенд — реальное оборудование"));
    modeCombo_->addItem(QStringLiteral("Демонстрация интерфейса — имитация"));
    root->addLayout(selectors);
    scopeCombo_->parentWidget()->setVisible(false);
    testCombo_->parentWidget()->setVisible(false);

    auto* modeCards = new QHBoxLayout;
    modeCards->setSpacing(12);
    auto* modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    auto addModeCard = [&](const QString& titleText, const QString& detail,
                           const QString& scopeCode, const QString& accent,
                           const QString& iconPath) {
        auto* button = new QPushButton(QStringLiteral("%1\n%2").arg(titleText, detail));
        button->setIcon(QIcon(iconPath));
        button->setIconSize(QSize(30, 30));
        button->setCheckable(true);
        button->setMinimumHeight(86);
        button->setObjectName(QStringLiteral("modeCard_%1").arg(scopeCode));
        button->setStyleSheet(QStringLiteral(
            "QPushButton { background:#12161c; color:#dfe6ee; border:1px solid #2c333d; "
            "font-size:13px; font-weight:600; text-align:left; padding:14px; border-radius:9px; }"
            "QPushButton:hover { border:2px solid %1; background:#1b2129; }"
            "QPushButton:checked { border:2px solid %1; color:%1; background:#18212c; }")
            .arg(accent));
        modeGroup->addButton(button);
        modeCards->addWidget(button, 1);
        connect(button, &QPushButton::clicked, this, [this, scopeCode] {
            const int scopeIndex = scopeCombo_->findData(scopeCode);
            if (scopeIndex >= 0) scopeCombo_->setCurrentIndex(scopeIndex);
        });
        return button;
    };
    auto* yalkCard = addModeCard(QStringLiteral("ЯЛК-96"),
        QStringLiteral("Поток, адреса и каналы"), QStringLiteral("ЯЛК-96"),
        QStringLiteral("#2f80ed"), QStringLiteral(":/icons/collect.svg"));
    auto* ytpCard = addModeCard(QStringLiteral("ЯТП"),
        QStringLiteral("30 каналов · 0 / 120 / 240 Ом"), QStringLiteral("ЯТП"),
        QStringLiteral("#8247d6"), QStringLiteral(":/icons/detail.svg"));
    addModeCard(QStringLiteral("УБСИ по ТУ"),
        QStringLiteral("ЯЛК + ЯТП + питание"), QStringLiteral("УЛК+ЯТП"),
        QStringLiteral("#079b9d"), QStringLiteral(":/icons/scenario.svg"));
    ytpCard->setChecked(true);
    Q_UNUSED(yalkCard);
    root->addLayout(modeCards);

    auto* runOptions = new QHBoxLayout;
    auto* serialLabel = new QLabel(QStringLiteral("Заводской номер (необязательно):"));
    serialLabel->setStyleSheet("color:#8b95a3;");
    serialEdit_ = new QLineEdit;
    serialEdit_->setObjectName(QStringLiteral("objectSerial"));
    serialEdit_->setPlaceholderText(QStringLiteral("например, УБСИ-007"));
    serialEdit_->setMaximumWidth(230);
    serialEdit_->setStyleSheet("background:#0e1115; color:#e6eaf0; border:1px solid #2c333d; padding:7px;");
    partialCheck_ = new QCheckBox(QStringLiteral("Диагностический запуск без части оборудования"));
    partialCheck_->setObjectName(QStringLiteral("allowPartial"));
    partialCheck_->setToolTip(QStringLiteral("Такой запуск никогда не получает итог ОК"));
    runOptions->addWidget(serialLabel);
    runOptions->addWidget(serialEdit_);
    runOptions->addSpacing(16);
    runOptions->addWidget(partialCheck_);
    contactThresholdCheck_ = new QCheckBox(
        QStringLiteral("Опция ЯЛК: пороги контактов 1,0 / 2,4 В"));
    contactThresholdCheck_->setObjectName(QStringLiteral("contactThresholdOption"));
    contactThresholdCheck_->setToolTip(QStringLiteral(
        "Запускает отдельную проверку всех 80 адресов; не входит в обязательный прогон УБСИ"));
    contactThresholdCheck_->setVisible(false);
    runOptions->addSpacing(16);
    runOptions->addWidget(contactThresholdCheck_);
    runOptions->addStretch(1);
    root->addLayout(runOptions);

    scopeLabel_ = new QLabel;
    scopeLabel_->setWordWrap(true);
    scopeLabel_->setStyleSheet(
        "background:#132033; color:#9ac7ff; border:1px solid #27466c; "
        "padding:8px 10px; border-radius:4px;");
    root->addWidget(scopeLabel_);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);

    auto* left = new QVBoxLayout;
    left->addWidget(makeSectionTitle(QStringLiteral("Готовность выбранной процедуры")));
    equipmentTable_ = new QTableWidget(0, 5);
    equipmentTable_->setObjectName(QStringLiteral("equipmentTable"));
    equipmentTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Устройство"), QStringLiteral("Связь с ПЭВМ"),
         QStringLiteral("Контроль"), QStringLiteral("Состояние"),
         QStringLiteral("Диагностика")});
    equipmentTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    equipmentTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    equipmentTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    equipmentTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    equipmentTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    equipmentTable_->verticalHeader()->hide();
    equipmentTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    equipmentTable_->setSelectionMode(QAbstractItemView::NoSelection);
    addEquipment("RS485", QStringLiteral("Адаптер УЛК"), QStringLiteral("192.168.0.115:1113 / UDP"),
                 QStringLiteral("нажмите «Проверить оборудование»"));
    addEquipment("ISD", QStringLiteral("ИСД"), QStringLiteral("192.168.0.101 / HTTP"),
                 QStringLiteral("нажмите «Проверить оборудование»"));
    addEquipment("V7", QStringLiteral("В7-78/1"), QStringLiteral("USB / NI-VISA"),
                 QStringLiteral("нажмите «Проверить оборудование»"));
    addEquipment("AKIP", QStringLiteral("АКИП-1160/6"), QStringLiteral("USB / COM"),
                 QStringLiteral("контроль U/I; выход отключается после проверки"));
    addEquipment("R4831", QStringLiteral("Магазин Р4831"), QStringLiteral("общий X123 / ручной"),
                 QStringLiteral("подключён к X123; оператор переключает 0 / 120 / 240 Ом"), true);
    equipmentTable_->setMinimumHeight(135);
    equipmentTable_->setMaximumHeight(220);
    left->addWidget(equipmentTable_);

    auto* readinessBar = new QHBoxLayout;
    checkButton_ = new QPushButton(QStringLiteral("Проверить оборудование"));
    detailsButton_ = new QPushButton(QStringLiteral("Открыть подробности"));
    detailsButton_->setObjectName(QStringLiteral("equipmentDetails"));
    readinessLabel_ = new QLabel(QStringLiteral("Стенд ещё не проверен"));
    readinessLabel_->setWordWrap(true);
    readinessLabel_->setStyleSheet("color:#d7a95b;");
    readinessBar->addWidget(checkButton_);
    readinessBar->addWidget(readinessLabel_, 1);
    readinessBar->addWidget(detailsButton_);
    left->addLayout(readinessBar);

    diagnosticLabel_ = new QLabel(QStringLiteral(
        "Нажмите «Проверить оборудование» — здесь появится полный текст ошибки."));
    diagnosticLabel_->setWordWrap(true);
    diagnosticLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    diagnosticLabel_->setStyleSheet(
        "background:#0e1115; color:#8b95a3; border:1px solid #2c333d; "
        "padding:7px 9px; border-radius:4px;");
    left->addWidget(diagnosticLabel_);

    auto* right = new QVBoxLayout;
    right->addWidget(makeSectionTitle(QStringLiteral("Ход и результат")));
    plot_ = new TestPlotWidget;
    right->addWidget(plot_);
    summaryTable_ = new QTableWidget(0, 4);
    summaryTable_->setObjectName(QStringLiteral("cellSummaryTable"));
    summaryTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Этап / ячейка"), QStringLiteral("Норма"),
         QStringLiteral("Не норма"), QStringLiteral("Итог")});
    summaryTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    summaryTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    summaryTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    summaryTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    summaryTable_->verticalHeader()->hide();
    summaryTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    summaryTable_->setSelectionMode(QAbstractItemView::NoSelection);
    summaryTable_->setMinimumHeight(180);
    left->addWidget(summaryTable_, 1);
    resultTable_ = new QTableWidget(0, 12);
    resultTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Адрес"), QStringLiteral("Точка"), QStringLiteral("Код ИСД"),
         QStringLiteral("Raw"), QStringLiteral("Код ЯЛК"), QStringLiteral("Сигнал"),
         QStringLiteral("В7, В"), QStringLiteral("ЯЛК, В"), QStringLiteral("ΔU, В"),
         QStringLiteral("γ, % шкалы"), QStringLiteral("δ, %"), QStringLiteral("Итог")});
    resultTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    resultTable_->verticalHeader()->hide();
    resultTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultTable_->setSelectionMode(QAbstractItemView::NoSelection);
    right->addWidget(resultTable_, 1);
    connect(resultTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row < 0 || !resultTable_->item(row, 0)) return;
        QString channel = resultTable_->item(row, 0)->text();
        if (selectedTestCode() == QStringLiteral("ULK_COMBINED_CHECK")
            && resultTable_->item(row, 1)) channel = resultTable_->item(row, 1)->text();
        plot_->selectChannel(channel);
    });

    body->addLayout(left, 3);
    body->addLayout(right, 7);
    root->addLayout(body, 1);

    auto* advancedScroll = new QScrollArea;
    advancedScroll->setWidgetResizable(true);
    advancedScroll->setMaximumHeight(360);
    advancedControl_ = new EquipmentControlWidget([this](
        const std::string& capability, const std::string& operation,
        const std::map<std::string, std::string>& arguments) {
            if (!equipmentInvoke_) throw std::runtime_error(
                "Сначала выполните проверку оборудования");
            return equipmentInvoke_(capability, operation, arguments);
        });
    advancedScroll->setWidget(advancedControl_);
    advancedScroll->setVisible(false);
    advancedScroll->setObjectName(QStringLiteral("advancedEquipmentPanel"));
    advancedContainer_ = advancedScroll;
    root->addWidget(advancedScroll);

    progress_ = new QProgressBar;
    progress_->setRange(0, 3);
    progress_->setValue(0);
    progress_->setFormat(QStringLiteral("Проверка не запущена"));
    root->addWidget(progress_);

    auto* actionBar = new QHBoxLayout;
    verdictLabel_ = new QLabel(QStringLiteral("ИТОГ НЕ СФОРМИРОВАН"));
    verdictLabel_->setStyleSheet(
        "font-size:16px; font-weight:700; color:#8e9aa8; padding:8px 12px;"
        "border:1px solid #3a424d; border-radius:4px;");
    startButton_ = new QPushButton(QStringLiteral("Запустить проверку"));
    startButton_->setMinimumWidth(210);
    startButton_->setStyleSheet(
        "QPushButton { background:#286a49; border:1px solid #3d9a6b; font-size:14px; font-weight:700; padding:11px 18px; }"
        "QPushButton:hover { background:#327e58; }"
        "QPushButton:disabled { background:#1c2128; border-color:#232a33; color:#5b6573; }");
    actionBar->addWidget(verdictLabel_, 1);
    tuReportButton_ = new QPushButton(QStringLiteral("Краткий отчёт ТУ"));
    tuReportButton_->setObjectName(QStringLiteral("openTuReport"));
    tuReportButton_->setEnabled(false);
    actionBar->addWidget(tuReportButton_);
    productionReportButton_ = new QPushButton(QStringLiteral("Ведомость каналов"));
    productionReportButton_->setObjectName(QStringLiteral("openProductionReport"));
    productionReportButton_->setEnabled(false);
    actionBar->addWidget(productionReportButton_);
    stopButton_ = new QPushButton(QStringLiteral("Безопасно остановить"));
    stopButton_->setEnabled(false);
    stopButton_->setStyleSheet(
        "QPushButton { background:#5d2d31; border:1px solid #9a4d55; font-weight:600; padding:11px 18px; }"
        "QPushButton:disabled { background:#1c2128; border-color:#232a33; color:#5b6573; }");
    actionBar->addWidget(stopButton_);
    actionBar->addWidget(startButton_);
    root->addLayout(actionBar);

    demoTimer_ = new QTimer(this);
    demoTimer_->setInterval(450);
    connect(demoTimer_, &QTimer::timeout, this, &TestPage::advanceDemo);
    connect(checkButton_, &QPushButton::clicked, this, &TestPage::equipmentCheckRequested);
    connect(detailsButton_, &QPushButton::clicked, this, [this]() {
        const bool show = equipmentTable_->isColumnHidden(1);
        for (const int column : {1, 2, 4}) equipmentTable_->setColumnHidden(column, !show);
        diagnosticLabel_->setVisible(show || engineerMode_);
        detailsButton_->setText(show ? QStringLiteral("Скрыть подробности")
                                     : QStringLiteral("Открыть подробности"));
    });
    connect(startButton_, &QPushButton::clicked, this, &TestPage::startSelectedTest);
    connect(stopButton_, &QPushButton::clicked, this, &TestPage::stopRequested);
    connect(tuReportButton_, &QPushButton::clicked, this, [this] {
        if (!tuReportPath_.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(tuReportPath_));
    });
    connect(productionReportButton_, &QPushButton::clicked, this, [this] {
        if (!productionReportPath_.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(productionReportPath_));
    });
    connect(partialCheck_, &QCheckBox::toggled, this, &TestPage::updateStartAvailability);
    connect(contactThresholdCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (selectedScopeCode() != QStringLiteral("ЯЛК-96")) return;
        const QString code = enabled
            ? QStringLiteral("YALK_CONTACT_THRESHOLDS")
            : QStringLiteral("YALK_FULL_5_6");
        const int index = testCombo_->findData(code);
        if (index >= 0) testCombo_->setCurrentIndex(index);
    });
    connect(objectCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TestPage::rebuildScopes);
    connect(scopeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TestPage::rebuildTests);
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TestPage::updateStartAvailability);
    connect(testCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TestPage::updateSelectionSummary);
    connect(equipmentTable_, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem* item) {
        if (!item || item->column() != 3) return;
        for (auto it = equipmentRows_.begin(); it != equipmentRows_.end(); ++it) {
            if (it->row != item->row() || !it->operatorConfirmation) continue;
            it->ready = item->checkState() == Qt::Checked;
            const QSignalBlocker blocker(equipmentTable_);
            item->setText(it->ready ? QStringLiteral("ПОДТВЕРЖДЕНО")
                                    : QStringLiteral("ПОДТВЕРДИТЬ"));
            item->setForeground(it->ready ? QColor("#70d79b") : QColor("#d7a95b"));
            updateStartAvailability();
            break;
        }
    });
    rebuildScopes();
    scopeCombo_->setCurrentIndex(scopeCombo_->findData(QStringLiteral("ЯТП")));
    setEngineerMode(false);
}

void TestPage::setEquipmentInvoker(EquipmentInvoke invoke)
{
    equipmentInvoke_ = std::move(invoke);
}

QString TestPage::selectedObjectCode() const
{
    return objectCombo_->currentData().toString();
}

QString TestPage::selectedScopeCode() const
{
    return scopeCombo_->currentData().toString();
}

QString TestPage::selectedTestCode() const
{
    return testCombo_->currentData().toString();
}

QString TestPage::currentScenarioCode() const
{
    return selectedTestCode();
}

void TestPage::rebuildScopes()
{
    const QString previous = selectedScopeCode();
    scopeCombo_->blockSignals(true);
    scopeCombo_->clear();
    const CellInfo* cells = kUbsiCells;
    const int count = int(std::size(kUbsiCells));
    for (int i = 0; i < count; ++i) {
        scopeCombo_->addItem(QString::fromUtf8(cells[i].code), QString::fromUtf8(cells[i].code));
        scopeCombo_->setItemData(scopeCombo_->count() - 1, QString::fromUtf8(cells[i].purpose), Qt::ToolTipRole);
    }
    const int oldIndex = scopeCombo_->findData(previous);
    scopeCombo_->setCurrentIndex(oldIndex >= 0 ? oldIndex : 0);
    scopeCombo_->blockSignals(false);
    rebuildTests();
}

void TestPage::rebuildTests()
{
    const QString previous = selectedTestCode();
    testCombo_->blockSignals(true);
    testCombo_->clear();
    if (selectedScopeCode() == QStringLiteral("ЯЛК-96")) {
        testCombo_->addItem(QStringLiteral("Полная проверка ЯЛК · 80 адресов · ТУ 5.6"),
                            QStringLiteral("YALK_FULL_5_6"));
        testCombo_->addItem(QStringLiteral("Опция · контактные пороги 1,0 / 2,4 В"),
                            QStringLiteral("YALK_CONTACT_THRESHOLDS"));
    } else if (selectedScopeCode() == QStringLiteral("ЯТП")) {
        testCombo_->addItem(QStringLiteral("Полная проверка ЯТП · 0 / 120 / 240 Ом"),
                            QStringLiteral("YTP_FULL_5_6"));
        testCombo_->addItem(QStringLiteral("Быстрый контроль ЯТП · только 120 Ом (не ТУ)"),
                            QStringLiteral("YTP_120_CHECK"));
    } else if (selectedScopeCode() == QStringLiteral("УЛК+ЯТП")) {
        testCombo_->addItem(QStringLiteral("Совмещённая проверка ЯЛК + ЯТП · ТУ 5.6"),
                            QStringLiteral("ULK_COMBINED_CHECK"));
    }
    const bool yalkScope = selectedScopeCode() == QStringLiteral("ЯЛК-96");
    contactThresholdCheck_->setVisible(yalkScope);
    if (!yalkScope) {
        const QSignalBlocker blocker(contactThresholdCheck_);
        contactThresholdCheck_->setChecked(false);
    }
    const QString requested = yalkScope && contactThresholdCheck_->isChecked()
        ? QStringLiteral("YALK_CONTACT_THRESHOLDS") : previous;
    const int oldIndex = testCombo_->findData(requested);
    testCombo_->setCurrentIndex(oldIndex >= 0 ? oldIndex : 0);
    testCombo_->blockSignals(false);
    updateSelectionSummary();
}

void TestPage::updateSelectionSummary()
{
    const QString object = selectedObjectCode();
    const QString scope = selectedScopeCode();
    const QString test = selectedTestCode();
    if (test == QStringLiteral("UBSI_NORMAL_5_6")) {
        scopeLabel_->setText(QStringLiteral(
            "УБСИ по ТУ 5.6: Орбита контролирует выходной поток блока, Ethernet-адаптер читает внутренние коды ячеек, ИСД выполняет коммутацию, а приборы формируют и измеряют воздействия."));
    } else if (test == QStringLiteral("BSI_DIAGNOSTIC")) {
        scopeLabel_->setText(QStringLiteral(
            "Подключённый БСИ используется для обкатки общей цепочки Орбита → сценарий → журнал → интерфейс. Результат всегда НЕПОЛНАЯ: БСИ по ТУ 5.6 не оценивается."));
    } else if (test == QStringLiteral("YALK_FULL_5_6")) {
        scopeLabel_->setText(object == QStringLiteral("BSI")
            ? QStringLiteral("Полный сценарий ЯЛК в этом релизе предназначен для УБСИ, а не для БСИ.")
            : QStringLiteral("ЯЛК-96 УБСИ: ИСД задаёт 0 / 3,1 / 6,2 В, В7 измеряет эталон, адаптер УЛК читает 16 свежих кадров. Орбита и E20 не участвуют."));
    } else if (test == QStringLiteral("YALK_CONTACT_THRESHOLDS")) {
        scopeLabel_->setText(QStringLiteral(
            "Опциональная проверка контактных порогов ЯЛК-96: для каждого из 80 адресов ИСД задаёт 1,0 и 2,4 В, В7 подтверждает фактическое воздействие, поток адаптера должен показать соответственно 0 и 1."));
    } else if (test == QStringLiteral("YTP_120_CHECK")) {
        scopeLabel_->setText(QStringLiteral(
            "Быстрый контроль: Р4831 остаётся на 120 Ом, оператор подтверждает фактическое значение один раз, затем проверяются все 30 каналов. Результаты каналов оцениваются по ±1,2 Ом; общий итог помечается НЕПОЛНАЯ, потому что крайние точки диапазона не проверялись."));
    } else if (test == QStringLiteral("YTP_FULL_5_6")) {
        scopeLabel_->setText(QStringLiteral(
            "ЯТП УБСИ: магазин Р4831 подключён к общему X123. Оператор вручную выставляет 0 / 120 / 240 Ом и вводит фактическое значение; ЯТП сама опрашивает 30 каналов, адаптер читает 16 свежих кадров. ИСД, Орбита и E20 не участвуют."));
    } else if (test == QStringLiteral("ULK_COMBINED_CHECK")) {
        scopeLabel_->setText(QStringLiteral(
            "Проверяемый объём ТУ: холодная готовность до 30 с; питание 24 / 27 / 35 В и ток до 400 мА; "
            "выдержки 19 В — 5 минут и 37 В — 1 минута; ЯЛК по 80 адресам, обрыв и ±12 В; затем ЯТП по 30 каналам. "
            "ЯВП-8 зачтена по производственному контролю. Формируются краткий протокол ТУ и ведомость каналов."));
    } else {
        scopeLabel_->setText(QStringLiteral("%1 · %2: диагностический прогон проверяет наличие источника данных, адресной привязки и стабильной выборки. Он не выдаётся за приёмочное испытание по ТУ.")
            .arg(object == QStringLiteral("BSI") ? QStringLiteral("БСИ") : QStringLiteral("УБСИ № 7"), scope));
    }
    const bool ytpPlot = test == QStringLiteral("YTP_FULL_5_6")
        || test == QStringLiteral("YTP_120_CHECK");
    if (test == QStringLiteral("ULK_COMBINED_CHECK")) {
        plot_->configure(-2.0, 102.0, QStringLiteral("102 % FS"), QStringLiteral("−2 % FS"),
            QStringLiteral("Каналы ЯЛК и ЯТП в общей приведённой шкале"),
            QStringLiteral("— эталон"), QStringLiteral("— измерено"));
    } else if (ytpPlot) {
        const bool quick120 = test == QStringLiteral("YTP_120_CHECK");
        plot_->configure(quick120 ? 116.0 : 0.0, quick120 ? 124.0 : 240.0,
            quick120 ? QStringLiteral("124 Ом") : QStringLiteral("240 Ом"),
            quick120 ? QStringLiteral("116 Ом") : QStringLiteral("0 Ом"),
            quick120 ? QStringLiteral("Каналы ЯТП 1…30 при 120 Ом")
                     : QStringLiteral("Каналы ЯТП 1…30; точки 0 · 120 · 240 Ом"),
            QStringLiteral("— Р4831"), QStringLiteral("— ЯТП"));
    } else if (test == QStringLiteral("YALK_CONTACT_THRESHOLDS")) {
        plot_->configure(0.8, 2.6, QStringLiteral("2,6 В"), QStringLiteral("0,8 В"),
            QStringLiteral("Контактные пороги ЯЛК; точки 1,0 · 2,4 В"),
            QStringLiteral("— В7-78/1"), QStringLiteral("— ЯЛК"));
    } else {
        plot_->configure(-0.1, 6.3, QStringLiteral("6,3 В"), QStringLiteral("−0,1 В"),
            QStringLiteral("Адреса ЯЛК 1…80; точки 0 · 3,1 · 6,2 В"),
            QStringLiteral("— В7-78/1"), QStringLiteral("— ЯЛК"));
    }
    plot_->setVisible(test == QStringLiteral("YALK_FULL_5_6")
        || test == QStringLiteral("YALK_CONTACT_THRESHOLDS")
        || test == QStringLiteral("ULK_COMBINED_CHECK")
        || ytpPlot || test.endsWith(QStringLiteral("NORMAL_5_6")));
    if (test == QStringLiteral("ULK_COMBINED_CHECK")) {
        resultTable_->setHorizontalHeaderLabels({
            QStringLiteral("Ячейка"), QStringLiteral("Канал"),
            QStringLiteral("Точка"), QStringLiteral("Raw"),
            QStringLiteral("Эталон"), QStringLiteral("Измерено"),
            QStringLiteral("Ошибка"), QStringLiteral("γ, % FS"),
            QStringLiteral("Сигнал"), QStringLiteral("Выборка"),
            QStringLiteral("Примечание"), QStringLiteral("Итог")});
    } else if (ytpPlot) {
        resultTable_->setHorizontalHeaderLabels({
            QStringLiteral("Канал"), QStringLiteral("Точка"),
            QStringLiteral("Задано, Ом"), QStringLiteral("Raw"),
            QStringLiteral("Калибр. ноль"), QStringLiteral("Калибр. шкала"),
            QStringLiteral("Эталон, Ом"), QStringLiteral("ЯТП, Ом"),
            QStringLiteral("Ошибка, Ом"), QStringLiteral("γ, %"),
            QStringLiteral("Режим"), QStringLiteral("Итог")});
    } else {
        resultTable_->setHorizontalHeaderLabels({
            QStringLiteral("Адрес"), QStringLiteral("Точка"), QStringLiteral("Код ИСД"),
            QStringLiteral("Raw"), QStringLiteral("Код ЯЛК"), QStringLiteral("Сигнал"),
            QStringLiteral("В7, В"), QStringLiteral("ЯЛК, В"), QStringLiteral("ΔU, В"),
            QStringLiteral("γ, % шкалы"), QStringLiteral("δ, %"), QStringLiteral("Итог")});
    }
    const QStringList required = requiredEquipment();
    for (auto it = equipmentRows_.cbegin(); it != equipmentRows_.cend(); ++it) {
        const bool alwaysStatus = it.key() == QStringLiteral("RS485")
            || it.key() == QStringLiteral("ISD");
        equipmentTable_->setRowHidden(it->row, !alwaysStatus && !required.contains(it.key()));
    }
    resultTable_->setRowCount(0);
    summaryTable_->setRowCount(0);
    plot_->clear();
    progress_->setValue(0);
    progress_->setFormat(QStringLiteral("Проверка не запущена"));
    verdictLabel_->setText(QStringLiteral("ИТОГ НЕ СФОРМИРОВАН"));
    verdictLabel_->setStyleSheet(
        "font-size:16px; font-weight:700; color:#8e9aa8; padding:8px 12px;"
        "border:1px solid #3a424d; border-radius:4px;");
    updateStartAvailability();
}

void TestPage::addEquipment(const QString& code, const QString& name,
                            const QString& connection, const QString& initialDetail,
                            bool operatorConfirmation)
{
    const int row = equipmentTable_->rowCount();
    equipmentTable_->insertRow(row);
    equipmentTable_->setItem(row, 0, new QTableWidgetItem(name));
    equipmentTable_->setItem(row, 1, new QTableWidgetItem(connection));
    equipmentTable_->setItem(row, 2, new QTableWidgetItem(
        operatorConfirmation ? QStringLiteral("Оператор") : QStringLiteral("Автоматически")));
    auto* state = new QTableWidgetItem(operatorConfirmation
        ? QStringLiteral("ПОДТВЕРДИТЬ") : QStringLiteral("НЕ ПРОВЕРЕНО"));
    if (operatorConfirmation) {
        state->setFlags((state->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        state->setCheckState(Qt::Unchecked);
        state->setForeground(QColor("#d7a95b"));
    }
    equipmentTable_->setItem(row, 3, state);
    equipmentTable_->setItem(row, 4, new QTableWidgetItem(initialDetail));
    equipmentTable_->item(row, 4)->setToolTip(initialDetail);
    equipmentRows_.insert(code, EquipmentRow{row, false, operatorConfirmation});
}

void TestPage::setEquipmentStatus(const QString& code, bool ready, const QString& detail)
{
    auto it = equipmentRows_.find(code);
    if (it == equipmentRows_.end()) return;
    if (it->operatorConfirmation) return;
    it->ready = ready;
    auto* state = equipmentTable_->item(it->row, 3);
    state->setText(ready ? QStringLiteral("ГОТОВО") : QStringLiteral("НЕ ГОТОВО"));
    state->setForeground(ready ? QColor("#70d79b") : QColor("#e1766d"));
    equipmentTable_->item(it->row, 4)->setText(detail);
    equipmentTable_->item(it->row, 4)->setToolTip(detail);
    if (!ready) {
        diagnosticLabel_->setText(QStringLiteral("%1: %2").arg(code, detail));
        diagnosticLabel_->setStyleSheet(
            "background:#2a1718; color:#e1766d; border:1px solid #6b3434; "
            "padding:7px 9px; border-radius:4px;");
    } else if (code == QStringLiteral("V7")) {
        diagnosticLabel_->setText(QStringLiteral("В7-78/1: %1").arg(detail));
        diagnosticLabel_->setStyleSheet(
            "background:#14251c; color:#70d79b; border:1px solid #315c43; "
            "padding:7px 9px; border-radius:4px;");
    }
    updateStartAvailability();
}

void TestPage::setEquipmentMissingPlugin(const QString& code, const QString& detail)
{
    auto it = equipmentRows_.find(code);
    if (it == equipmentRows_.end() || it->operatorConfirmation) return;
    it->ready = false;
    auto* state = equipmentTable_->item(it->row, 3);
    state->setText(QStringLiteral("НЕТ ПЛАГИНА"));
    state->setForeground(QColor("#e1766d"));
    equipmentTable_->item(it->row, 4)->setText(detail);
    equipmentTable_->item(it->row, 4)->setToolTip(detail);
    diagnosticLabel_->setText(QStringLiteral("%1: %2").arg(code, detail));
    diagnosticLabel_->setStyleSheet(
        "background:#2a1718; color:#e1766d; border:1px solid #6b3434; "
        "padding:7px 9px; border-radius:4px;");
    updateStartAvailability();
}

void TestPage::setEquipmentChecking(const QString& code, const QString& detail)
{
    auto it = equipmentRows_.find(code);
    if (it == equipmentRows_.end()) return;
    if (it->operatorConfirmation) return;
    it->ready = false;
    equipmentTable_->item(it->row, 3)->setText(QStringLiteral("ПРОВЕРКА…"));
    equipmentTable_->item(it->row, 3)->setForeground(QColor("#d7a95b"));
    equipmentTable_->item(it->row, 4)->setText(detail);
    equipmentTable_->item(it->row, 4)->setToolTip(detail);
    diagnosticLabel_->setText(QStringLiteral("%1: %2").arg(code, detail));
}

QStringList TestPage::requiredEquipment() const
{
    const QString test = selectedTestCode();
    if (test == QStringLiteral("YALK_FULL_5_6")
        || test == QStringLiteral("YALK_CONTACT_THRESHOLDS")) {
        return {"RS485", "ISD", "V7", "AKIP"};
    }
    if (test == QStringLiteral("YTP_FULL_5_6")
        || test == QStringLiteral("YTP_120_CHECK")) {
        return {"RS485", "ISD", "AKIP", "R4831"};
    }
    if (test == QStringLiteral("ULK_COMBINED_CHECK")) {
        return {"RS485", "ISD", "V7", "AKIP", "R4831"};
    }
    const auto scenario = scenarios_.constFind(test);
    if (scenario != scenarios_.cend()) {
        return scenario->requiredEquipment;
    }
    return {"RS485"};
}

void TestPage::updateStartAvailability()
{
    if (runInProgress_) {
        startButton_->setEnabled(false);
        checkButton_->setEnabled(false);
        stopButton_->setEnabled(true);
        return;
    }
    checkButton_->setEnabled(true);
    stopButton_->setEnabled(false);
    const bool demo = modeCombo_->currentIndex() == kDemoMode;
    if (demo) {
        startButton_->setText(QStringLiteral("Запустить демонстрацию"));
        startButton_->setEnabled(!demoTimer_->isActive());
        readinessLabel_->setText(QStringLiteral(
            "Демонстрация не обращается к оборудованию и не является результатом испытания."));
        readinessLabel_->setStyleSheet("color:#69aee6;");
        return;
    }

    startButton_->setText(QStringLiteral("Запустить проверку"));

    const auto scenario = scenarios_.constFind(selectedTestCode());
    if (scenario == scenarios_.cend() || !scenario->available) {
        startButton_->setEnabled(false);
        readinessLabel_->setText(scenario == scenarios_.cend()
            ? QStringLiteral("Для выбранной процедуры нет исполняемого сценария")
            : scenario->detail);
        readinessLabel_->setStyleSheet("color:#e1766d;");
        return;
    }

    const QStringList required = requiredEquipment();
    QStringList missing;
    for (const auto& code : required) {
        const auto it = equipmentRows_.constFind(code);
        if (it == equipmentRows_.cend() || !it->ready) missing << code;
    }
    const bool partial = partialCheck_->isChecked();
    startButton_->setEnabled((missing.isEmpty() || partial) && !demoTimer_->isActive());
    if (missing.isEmpty()) {
        readinessLabel_->setText(scenario->diagnostic
            ? QStringLiteral("Диагностический запуск готов. Итог будет НЕПОЛНАЯ.")
            : QStringLiteral("Все возможности сценария готовы. Можно запускать проверку."));
        readinessLabel_->setStyleSheet(scenario->diagnostic ? "color:#69aee6;" : "color:#70d79b;");
    } else if (partial) {
        readinessLabel_->setText(QStringLiteral(
            "Диагностический запуск разрешён. Не готовы: %1. Итог будет НЕПОЛНАЯ или ОШИБКА.")
            .arg(missing.join(", ")));
        readinessLabel_->setStyleSheet("color:#69aee6;");
    } else {
        readinessLabel_->setText(QStringLiteral("Запуск заблокирован. Не готовы: %1").arg(missing.join(", ")));
        readinessLabel_->setStyleSheet("color:#d7a95b;");
    }
}

void TestPage::resetResults()
{
    tuReportPath_.clear();
    productionReportPath_.clear();
    tuReportButton_->setEnabled(false);
    productionReportButton_->setEnabled(false);
    resultTable_->setRowCount(0);
    summaryTable_->setRowCount(0);
    plot_->clear();
    progress_->setValue(0);
    verdictLabel_->setText(QStringLiteral("ВЫПОЛНЯЕТСЯ…"));
    verdictLabel_->setStyleSheet(
        "font-size:16px; font-weight:700; color:#d7a95b; padding:8px 12px;"
        "border:1px solid #765d32; border-radius:4px;");
}

void TestPage::startSelectedTest()
{
    if (modeCombo_->currentIndex() != kDemoMode) {
        resetResults();
        setRunInProgress(true, QStringLiteral("Подготовка и безопасная проверка оборудования"));
        const auto scenario = scenarios_.constFind(selectedTestCode());
        const bool diagnostic = scenario != scenarios_.cend() && scenario->diagnostic;
        emit runRequested(selectedTestCode(), serialEdit_->text().trimmed(),
                          partialCheck_->isChecked() || diagnostic);
        return;
    }
    resetResults();
    demoStep_ = 0;
    startButton_->setEnabled(false);
    progress_->setFormat(QStringLiteral("ДЕМО: подготовка %1").arg(selectedScopeCode()));
    demoTimer_->start();
}

void TestPage::advanceDemo()
{
    const bool ytp = selectedTestCode() == QStringLiteral("YTP_FULL_5_6")
        || selectedTestCode() == QStringLiteral("YTP_120_CHECK");
    if (ytp) {
        static const double measured[] = {120.26, 120.33, 120.39};
        if (demoStep_ >= 3) {
            finishDemo();
            return;
        }
        const int row = resultTable_->rowCount();
        resultTable_->insertRow(row);
        const QString channel = QString::number(demoStep_ + 1);
        const QStringList values = {
            channel, QStringLiteral("120 Ом"), QStringLiteral("120"),
            QString::number(2169 + demoStep_), QStringLiteral("330"),
            QStringLiteral("4000"), QStringLiteral("120.000"),
            QString::number(measured[demoStep_], 'f', 3),
            QString::number(measured[demoStep_] - 120.0, 'f', 3),
            QString::number((measured[demoStep_] - 120.0) / 2.4, 'f', 3),
            QStringLiteral("сопротивление"), QStringLiteral("OK")};
        for (int column = 0; column < values.size(); ++column)
            resultTable_->setItem(row, column, new QTableWidgetItem(values[column]));
        plot_->addPoint(120.0, measured[demoStep_], channel);
        ++demoStep_;
        progress_->setValue(demoStep_);
        progress_->setFormat(QStringLiteral("ДЕМО: ЯТП, показано %1 из 30 каналов")
            .arg(demoStep_));
        return;
    }

    if (selectedTestCode() != QStringLiteral("YALK_FULL_5_6")) {
        static const QString stages[] = {
            QStringLiteral("Источник данных"),
            QStringLiteral("Адресная привязка"),
            QStringLiteral("Стабильная выборка")
        };
        if (demoStep_ >= 3) {
            finishDemo();
            return;
        }
        const int row = resultTable_->rowCount();
        resultTable_->insertRow(row);
        const QString values[] = {
            selectedScopeCode() == QStringLiteral("BLOCK") ? QStringLiteral("Весь блок") : selectedScopeCode(),
            stages[demoStep_], QStringLiteral("—"), QStringLiteral("имитация"),
            QStringLiteral("не нормативный"), QStringLiteral("ГОТОВО")
        };
        for (int column = 0; column < 6; ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            if (column == 5) item->setForeground(QColor("#70d79b"));
            resultTable_->setItem(row, column, item);
        }
        ++demoStep_;
        progress_->setValue(demoStep_);
        progress_->setFormat(QStringLiteral("ДЕМО: выполнено %1 из 3 этапов").arg(demoStep_));
        return;
    }

    static const double references[] = {0.0021, 3.107138, 6.1984};
    static const double measured[] = {0.0030, 3.1060, 6.1970};
    static const QString points[] = {
        QStringLiteral("0 В"), QStringLiteral("3,1 В"), QStringLiteral("6,2 В")};
    if (demoStep_ >= 3) {
        finishDemo();
        return;
    }

    const int row = resultTable_->rowCount();
    resultTable_->insertRow(row);
    const double error = qAbs(measured[demoStep_] - references[demoStep_]);
    const QString values[] = {
        selectedObjectCode() == QStringLiteral("BSI")
            ? QStringLiteral("БСИ · ЯЛК-96 (демо)")
            : QStringLiteral("УБСИ · ЯЛК-96 (демо)"),
        points[demoStep_],
        QString::number(references[demoStep_], 'f', 6) + QStringLiteral(" В"),
        QString::number(measured[demoStep_], 'f', 6) + QStringLiteral(" В"),
        QStringLiteral("±0,031 В"),
        error <= 0.031 ? QStringLiteral("ОК") : QStringLiteral("НЕ ОК")
    };
    for (int column = 0; column < 6; ++column) {
        auto* item = new QTableWidgetItem(values[column]);
        if (column == 5) item->setForeground(QColor("#70d79b"));
        resultTable_->setItem(row, column, item);
    }
    plot_->addPoint(references[demoStep_], measured[demoStep_]);
    ++demoStep_;
    progress_->setValue(demoStep_);
    progress_->setFormat(QStringLiteral("ДЕМО: выполнено %1 из 3 точек").arg(demoStep_));
}

void TestPage::finishDemo()
{
    demoTimer_->stop();
    const QString target = selectedScopeCode() == QStringLiteral("BLOCK")
        ? (selectedObjectCode() == QStringLiteral("BSI") ? QStringLiteral("БСИ") : QStringLiteral("УБСИ № 7"))
        : selectedScopeCode();
    verdictLabel_->setText(selectedTestCode() == QStringLiteral("YALK_FULL_5_6")
        ? QStringLiteral("ДЕМО: %1 — ОК · БЛОК ЦЕЛИКОМ НЕ ОЦЕНИВАЛСЯ").arg(target)
        : QStringLiteral("ДЕМО: %1 · ДИАГНОСТИКА ГОТОВА · НЕ РЕЗУЛЬТАТ ТУ").arg(target));
    verdictLabel_->setStyleSheet(
        "font-size:16px; font-weight:700; color:#70d79b; padding:8px 12px;"
        "border:1px solid #3d8f65; border-radius:4px;");
    progress_->setFormat(QStringLiteral("Демонстрационный прогон завершён"));
    updateStartAvailability();
}

void TestPage::setScenarioInfo(
    const QString& code, bool available, bool diagnostic,
    const QStringList& requiredEquipment, const QString& detail)
{
    scenarios_.insert(code, ScenarioInfo{available, diagnostic, requiredEquipment, detail});
    diagnosticLabel_->setText(detail);
    diagnosticLabel_->setStyleSheet(available
        ? QStringLiteral("background:#14251c; color:#70d79b; border:1px solid #315c43; padding:7px 9px; border-radius:4px;")
        : QStringLiteral("background:#2a1718; color:#e1766d; border:1px solid #6b3434; padding:7px 9px; border-radius:4px;"));
    updateSelectionSummary();
}

void TestPage::setEngineerMode(bool enabled)
{
    engineerMode_ = enabled;
    if (modeCombo_ && modeCombo_->parentWidget()) modeCombo_->parentWidget()->setVisible(enabled);
    partialCheck_->setVisible(enabled);
    for (const int column : {1, 2, 4}) equipmentTable_->setColumnHidden(column, !enabled);
    diagnosticLabel_->setVisible(enabled);
    if (advancedContainer_) advancedContainer_->setVisible(enabled);
    detailsButton_->setText(enabled ? QStringLiteral("Скрыть подробности")
                                    : QStringLiteral("Открыть подробности"));
}

void TestPage::setRunInProgress(bool running, const QString& stage)
{
    runInProgress_ = running;
    if (running) {
        completedSteps_ = 0;
        progress_->setRange(0, 0);
        progress_->setFormat(stage.isEmpty() ? QStringLiteral("Выполняется…") : stage);
    } else {
        progress_->setRange(0, 100);
        progress_->setValue(100);
    }
    updateStartAvailability();
}

void TestPage::setRunEvent(const orbita::stand::RunEvent& event)
{
    if (!runInProgress_) return;
    if (event.stage == "START") {
        const int row = summaryTable_->rowCount();
        summaryTable_->insertRow(row);
        summaryTable_->setItem(row, 0, new QTableWidgetItem(
            QString::fromStdString(event.message)));
        summaryTable_->setItem(row, 1, new QTableWidgetItem(QStringLiteral("—")));
        summaryTable_->setItem(row, 2, new QTableWidgetItem(QStringLiteral("—")));
        auto* status = new QTableWidgetItem(QStringLiteral("ВЫПОЛНЯЕТСЯ"));
        status->setForeground(QColor("#2f80ed"));
        summaryTable_->setItem(row, 3, status);
        progress_->setFormat(QStringLiteral("Выполняется: %1")
            .arg(QString::fromStdString(event.message)));
    } else if (event.stage == "FINISH") {
        ++completedSteps_;
        if (summaryTable_->rowCount() > 0) {
            auto* status = summaryTable_->item(summaryTable_->rowCount() - 1, 3);
            if (status) {
                status->setText(QStringLiteral("ЗАВЕРШЕНО"));
                status->setForeground(QColor("#20a567"));
            }
        }
        progress_->setFormat(QStringLiteral("Завершено этапов: %1 · %2")
            .arg(completedSteps_)
            .arg(QString::fromStdString(event.message)));
    } else if (event.stage == "OPERATOR") {
        const auto found = event.data.find("target_resistance_ohm");
        const QString target = found == event.data.end()
            ? QStringLiteral("—") : QString::fromStdString(found->second);
        progress_->setFormat(QStringLiteral(
            "РУЧНОЙ ЭТАП: установите Р4831 на %1 Ом и подтвердите значение")
            .arg(target));
    } else if (event.stage == "MEASUREMENT") {
        const auto value = [&event](const char* key) -> QString {
            const auto found = event.data.find(key);
            return found == event.data.end() ? QString() : QString::fromStdString(found->second);
        };
        if (!value("ytp_channel").isEmpty()) {
            const bool combined = selectedTestCode() == QStringLiteral("ULK_COMBINED_CHECK");
            const double scale = combined ? 100.0 / 240.0 : 1.0;
            plot_->addMeasurement(value("ytp_channel"),
                value("actual_reference_ohm") + QStringLiteral(" Ом"),
                value("actual_reference_ohm").toDouble() * scale,
                value("measured_resistance_ohm").toDouble() * scale,
                value("raw").toDouble(), false,
                event.verdict == orbita::stand::RunVerdict::Ok,
                value("value_samples"), scale);
            progress_->setFormat(QStringLiteral(
                "ЯТП: канал %1 · эталон %2 Ом · raw %3 · ЯТП %4 Ом")
                .arg(value("ytp_channel"), value("actual_reference_ohm"),
                     value("raw"), value("measured_resistance_ohm")));
        } else {
            const double reference = value("v7_v").toDouble();
            const double measured = value("yalk_v").toDouble();
            const double scale = selectedTestCode() == QStringLiteral("ULK_COMBINED_CHECK")
                ? 100.0 / 6.2 : 1.0;
            plot_->addMeasurement(value("ulk_address"),
                value("command_v") + QStringLiteral(" В"), reference * scale,
                measured * scale, value("analog_code").toDouble(),
                value("signal") == QStringLiteral("1"),
                event.verdict == orbita::stand::RunVerdict::Ok,
                value("value_samples"), scale);
            progress_->setFormat(QStringLiteral("ЯЛК: адрес %1 · %2 В · В7 %3 В · ЯЛК %4 В")
                .arg(value("ulk_address"), value("command_v"), value("v7_v"), value("yalk_v")));
        }
    }
}

void TestPage::setRunResult(const orbita::stand::ScenarioRunResult& result,
                            const QString& tuReportPath,
                            const QString& productionReportPath)
{
    runInProgress_ = false;
    resultTable_->setRowCount(0);
    summaryTable_->setRowCount(0);
    plot_->clear();

    std::function<void(const orbita::stand::StepRunResult&)> appendStep;
    appendStep = [this, &appendStep](const orbita::stand::StepRunResult& step) {
        for (const auto& measurement : step.measurements) {
            const int row = resultTable_->rowCount();
            resultTable_->insertRow(row);
            const auto attribute = [&measurement](const char* key) -> QString {
                const auto found = measurement.attributes.find(key);
                return found == measurement.attributes.end()
                    ? QString() : QString::fromStdString(found->second);
            };
            const bool ytp = !attribute("ytp_channel").isEmpty();
            const bool yalk = !ytp && !attribute("ulk_address").isEmpty();
            // The large table is the production channel sheet.  Power,
            // cleanup and calibration summaries remain visible in the stage
            // table and in the concise TU protocol.
            if (!ytp && !yalk) continue;
            QStringList values;
            if (selectedTestCode() == QStringLiteral("ULK_COMBINED_CHECK")) {
                values = ytp ? QStringList{
                    QStringLiteral("ЯТП"), attribute("ytp_channel"),
                    attribute("actual_reference_ohm") + QStringLiteral(" Ом"), attribute("raw"),
                    attribute("actual_reference_ohm") + QStringLiteral(" Ом"),
                    attribute("measured_resistance_ohm") + QStringLiteral(" Ом"),
                    attribute("absolute_error_ohm") + QStringLiteral(" Ом"),
                    attribute("reduced_error_percent"), QStringLiteral("—"),
                    attribute("sample_count"), attribute("temperature_mode"),
                    acceptanceText(measurement.verdict)
                } : QStringList{
                    QStringLiteral("ЯЛК"), attribute("ulk_address"),
                    attribute("command_v") + QStringLiteral(" В"), attribute("raw"),
                    attribute("v7_v") + QStringLiteral(" В"),
                    attribute("yalk_v") + QStringLiteral(" В"),
                    attribute("absolute_error_v") + QStringLiteral(" В"),
                    attribute("reduced_error_percent"), attribute("signal"),
                    attribute("sample_count"), QStringLiteral("—"),
                    acceptanceText(measurement.verdict)};
            } else values = ytp ? QStringList{
                attribute("ytp_channel"),
                attribute("actual_reference_ohm") + QStringLiteral(" Ом"),
                attribute("target_resistance_ohm"), attribute("raw"),
                attribute("calibration_zero_raw"), attribute("calibration_full_raw"),
                attribute("actual_reference_ohm"), attribute("measured_resistance_ohm"),
                attribute("absolute_error_ohm"), attribute("reduced_error_percent"),
                attribute("temperature_mode"),
                acceptanceText(measurement.verdict)
            } : QStringList{
                yalk ? attribute("ulk_address") : QString::fromStdString(step.title),
                QString::fromStdString(measurement.title.empty() ? measurement.parameterKey : measurement.title),
                attribute("isd_code"), attribute("raw"), attribute("analog_code"),
                attribute("signal"),
                yalk ? attribute("v7_v") : QString::number(measurement.reference, 'g', 9),
                yalk ? attribute("yalk_v") : QString::number(measurement.measured, 'g', 9),
                attribute("absolute_error_v"), attribute("reduced_error_percent"),
                attribute("relative_error_percent").isEmpty() ? QStringLiteral("—")
                                                               : attribute("relative_error_percent"),
                acceptanceText(measurement.verdict)
            };
            for (int column = 0; column < values.size(); ++column) {
                auto* item = new QTableWidgetItem(values[column]);
                if (column == 11) {
                    item->setForeground(measurement.verdict == orbita::stand::RunVerdict::Ok
                        ? QColor("#70d79b") : QColor("#e1766d"));
                }
                resultTable_->setItem(row, column, item);
            }
            if (measurement.unit == "V" || measurement.unit == "Ом") {
                double scale = 1.0;
                if (selectedTestCode() == QStringLiteral("ULK_COMBINED_CHECK"))
                    scale = measurement.unit == "Ом" ? 100.0 / 240.0 : 100.0 / 6.2;
                const QString channel = ytp ? attribute("ytp_channel") : attribute("ulk_address");
                const QString point = ytp
                    ? attribute("actual_reference_ohm") + QStringLiteral(" Ом")
                    : attribute("command_v") + QStringLiteral(" В");
                plot_->addMeasurement(channel, point, measurement.reference * scale,
                    measurement.measured * scale,
                    ytp ? attribute("raw").toDouble() : attribute("analog_code").toDouble(),
                    attribute("signal") == QStringLiteral("1"),
                    measurement.verdict == orbita::stand::RunVerdict::Ok,
                    attribute("value_samples"), scale);
            }
        }
        if (step.measurements.empty() && step.children.empty()) {
            const int row = resultTable_->rowCount();
            resultTable_->insertRow(row);
            const QStringList values = {
                QStringLiteral("—"), QString::fromStdString(step.title), QStringLiteral("—"),
                QStringLiteral("—"), QStringLiteral("—"), QStringLiteral("—"),
                QStringLiteral("—"), QString::fromStdString(step.message), QStringLiteral("—"),
                QStringLiteral("—"), QStringLiteral("—"),
                acceptanceText(step.verdict)};
            for (int column = 0; column < values.size(); ++column)
                resultTable_->setItem(row, column, new QTableWidgetItem(values[column]));
        }
        for (const auto& child : step.children) appendStep(child);
    };
    for (const auto& step : result.steps) {
        int ok = 0;
        int failed = 0;
        std::function<void(const orbita::stand::StepRunResult&)> count;
        count = [&](const orbita::stand::StepRunResult& item) {
            for (const auto& measurement : item.measurements) {
                if (measurement.verdict == orbita::stand::RunVerdict::Ok) ++ok;
                else if (measurement.verdict == orbita::stand::RunVerdict::Fail) ++failed;
            }
            for (const auto& child : item.children) count(child);
        };
        count(step);
        const int row = summaryTable_->rowCount();
        summaryTable_->insertRow(row);
        const QStringList values = {
            QString::fromStdString(step.title), QString::number(ok),
            QString::number(failed),
            acceptanceText(step.verdict)};
        for (int column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            if (column == 3) {
                item->setForeground(step.verdict == orbita::stand::RunVerdict::Ok
                    ? QColor("#70d79b")
                    : step.verdict == orbita::stand::RunVerdict::Incomplete
                        ? QColor("#69aee6") : QColor("#e1766d"));
            }
            summaryTable_->setItem(row, column, item);
        }
        appendStep(step);
    }

    QString verdict;
    QColor color;
    switch (result.verdict) {
    case orbita::stand::RunVerdict::Ok: verdict = QStringLiteral("НОРМА"); color = QColor("#70d79b"); break;
    case orbita::stand::RunVerdict::Fail: verdict = QStringLiteral("НЕ НОРМА"); color = QColor("#e1766d"); break;
    case orbita::stand::RunVerdict::Incomplete: verdict = QStringLiteral("НЕПОЛНАЯ"); color = QColor("#69aee6"); break;
    case orbita::stand::RunVerdict::Aborted: verdict = QStringLiteral("ОСТАНОВЛЕНО"); color = QColor("#d7a95b"); break;
    default: verdict = QStringLiteral("ОШИБКА"); color = QColor("#e1766d"); break;
    }
    verdictLabel_->setText(QStringLiteral("ИТОГ: %1 · запуск %2").arg(verdict, QString::fromStdString(result.runId)));
    verdictLabel_->setStyleSheet(QStringLiteral(
        "font-size:16px; font-weight:700; color:%1; padding:8px 12px; border:1px solid %1; border-radius:4px;")
        .arg(color.name()));
    progress_->setRange(0, 100);
    progress_->setValue(100);
    progress_->setFormat(QStringLiteral("Проверка завершена: %1").arg(verdict));
    if (!tuReportPath.isEmpty()) {
        tuReportPath_ = tuReportPath;
        productionReportPath_ = productionReportPath;
        tuReportButton_->setEnabled(true);
        productionReportButton_->setEnabled(!productionReportPath_.isEmpty());
        diagnosticLabel_->setText(QStringLiteral("Протокол ТУ: %1\nВедомость каналов: %2")
            .arg(tuReportPath_, productionReportPath_));
        diagnosticLabel_->setToolTip(tuReportPath_);
    }
    updateStartAvailability();
}
