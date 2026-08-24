#include "test_page.h"

#include <QComboBox>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>

#include <iterator>

namespace {

constexpr int kStandMode = 0;
constexpr int kDemoMode = 1;

struct CellInfo {
    const char* code;
    const char* purpose;
};

const CellInfo kUbsiCells[] = {
    {"ЯП-П", "ячейка питания"},
    {"ЯТП", "температурные каналы"},
    {"ЯВП-8", "потенциометрические каналы"},
    {"ЯЛК-96", "аналоговые и контактные каналы"}
};

const CellInfo kBsiCells[] = {
    {"ЯП-А", "ячейка питания"},
    {"ЯГР", "состав параметров уточняется по картам"},
    {"ЯСМ", "цифровые параметры из приложения 2"},
    {"ЯТП", "температурные каналы"},
    {"ЯВП-8", "потенциометрические каналы"},
    {"ЯЛК-96", "аналоговые и контактные каналы"},
    {"ЯФК", "быстроменяющиеся параметры"}
};

class Plot : public QWidget
{
public:
    explicit Plot(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(210);
    }

    void clear()
    {
        reference_.clear();
        measured_.clear();
        update();
    }

    void addPoint(double reference, double measured)
    {
        reference_.push_back(reference);
        measured_.push_back(measured);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#0e1115"));

        const QRectF area = rect().adjusted(48, 18, -18, -34);
        painter.setPen(QColor("#34404d"));
        painter.drawRect(area);

        painter.setPen(QColor("#7e8a98"));
        painter.drawText(8, 28, QStringLiteral("6,2 В"));
        painter.drawText(18, static_cast<int>(area.bottom()), QStringLiteral("0 В"));
        painter.drawText(static_cast<int>(area.left()), height() - 10,
                         QStringLiteral("Точки воздействия: 0 · 3,1 · 6,2 В"));

        if (reference_.isEmpty()) {
            painter.setPen(QColor("#6f7a88"));
            painter.drawText(area, Qt::AlignCenter,
                             QStringLiteral("График появится во время проверки"));
            return;
        }

        auto drawSeries = [&](const QVector<double>& values, const QColor& color) {
            if (values.isEmpty()) return;
            QPainterPath path;
            for (int i = 0; i < values.size(); ++i) {
                const double x = area.left() + (area.width() * i / 2.0);
                const double normalized = qBound(0.0, values[i] / 6.2, 1.0);
                const double y = area.bottom() - normalized * area.height();
                if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
                painter.setBrush(color);
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(QPointF(x, y), 4.0, 4.0);
            }
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(color, 2.0));
            painter.drawPath(path);
        };

        drawSeries(reference_, QColor("#55b7ff"));
        drawSeries(measured_, QColor("#70d79b"));

        painter.setPen(QColor("#55b7ff"));
        painter.drawText(static_cast<int>(area.right()) - 175, 16,
                         QStringLiteral("— В7-78/1"));
        painter.setPen(QColor("#70d79b"));
        painter.drawText(static_cast<int>(area.right()) - 90, 16,
                         QStringLiteral("— ЯЛК"));
    }

private:
    QVector<double> reference_;
    QVector<double> measured_;
};

QLabel* makeSectionTitle(const QString& text)
{
    auto* label = new QLabel(text);
    label->setStyleSheet("font-size:15px; font-weight:600; color:#dfe6ee; margin-top:6px;");
    return label;
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
        "QWidget { background:#14171c; color:#dfe6ee; }"
        "QComboBox, QTableWidget { background:#0e1115; color:#dfe6ee; border:1px solid #2a313b; }"
        "QComboBox { padding:7px; min-height:22px; }"
        "QHeaderView::section { background:#1e2430; color:#aab4c0; padding:5px; border:1px solid #2a313b; }"
        "QPushButton { background:#1e2430; color:#dfe6ee; border:1px solid #35404d; padding:8px 14px; border-radius:4px; }"
        "QPushButton:hover { background:#293445; }"
        "QPushButton:disabled { color:#626d79; background:#171b21; border-color:#252b33; }"
        "QProgressBar { background:#0e1115; border:1px solid #2a313b; text-align:center; min-height:20px; }"
        "QProgressBar::chunk { background:#3d8f65; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 18, 14);
    root->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("Испытания оборудования КТМА"));
    title->setStyleSheet("font-size:22px; font-weight:700; color:#f0f4f8;");
    root->addWidget(title);

    auto* subtitle = new QLabel(QStringLiteral(
        "Выберите объект и вид испытания. Программа сначала проверит готовность стенда, "
        "затем выполнит измерения и сформирует итог ОК/НЕ ОК."));
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet("color:#aab4c0; font-size:12px;");
    root->addWidget(subtitle);

    auto* selectors = new QHBoxLayout;
    auto addSelector = [&](const QString& caption, QComboBox*& combo, int stretch) {
        auto* box = new QWidget;
        auto* layout = new QVBoxLayout(box);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        auto* label = new QLabel(caption);
        label->setStyleSheet("color:#8e9aa8; font-size:11px;");
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
    objectCombo_->addItem(QStringLiteral("УБСИ № 7 · ЛВРМ.468157.002"), QStringLiteral("UBSI-7"));
    objectCombo_->addItem(QStringLiteral("БСИ · ЛВРМ.468157.001"), QStringLiteral("BSI"));
    modeCombo_->addItem(QStringLiteral("Стенд — реальное оборудование"));
    modeCombo_->addItem(QStringLiteral("Демонстрация интерфейса — имитация"));
    root->addLayout(selectors);

    scopeLabel_ = new QLabel;
    scopeLabel_->setWordWrap(true);
    scopeLabel_->setStyleSheet(
        "background:#172333; color:#b9d7f5; border:1px solid #284765; "
        "padding:8px 10px; border-radius:4px;");
    root->addWidget(scopeLabel_);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);

    auto* left = new QVBoxLayout;
    left->addWidget(makeSectionTitle(QStringLiteral("Готовность оборудования")));
    equipmentTable_ = new QTableWidget(0, 4);
    equipmentTable_->setObjectName(QStringLiteral("equipmentTable"));
    equipmentTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Устройство"), QStringLiteral("Подключение"),
         QStringLiteral("Состояние"), QStringLiteral("Диагностика")});
    equipmentTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    equipmentTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    equipmentTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    equipmentTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    equipmentTable_->verticalHeader()->hide();
    equipmentTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    equipmentTable_->setSelectionMode(QAbstractItemView::NoSelection);
    addEquipment("E20", QStringLiteral("E20-10 + Орбита"), QStringLiteral("USB / Lusbapi"),
                 QStringLiteral("ещё не проверено"));
    addEquipment("RS485", QStringLiteral("Адаптер RS-485 ЛВРМ.424349.001"), QStringLiteral("Ethernet"),
                 QStringLiteral("драйвер обмена не подключён"));
    addEquipment("ISD", QStringLiteral("ИСД ЛВРМ.468173.001"), QStringLiteral("Ethernet / HTTP"),
                 QStringLiteral("рабочий IP и команды не перенесены"));
    addEquipment("V7", QStringLiteral("В7-78/1 (PV1 / PV2)"), QStringLiteral("USB / NI-VISA"),
                 QStringLiteral("нажмите «Проверить оборудование»"));
    addEquipment("AKIP", QStringLiteral("АКИП-1160/6, G1/G4"), QStringLiteral("USB"),
                 QStringLiteral("драйвер управления не подключён"));
    addEquipment("RIGOL", QStringLiteral("Rigol DG-1022Z"), QStringLiteral("USB"),
                 QStringLiteral("драйвер управления не подключён"));
    addEquipment("G3", QStringLiteral("Осциллограф АКИП-4113/2, G3"), QStringLiteral("USB"),
                 QStringLiteral("драйвер управления не подключён"));
    addEquipment("R4831", QStringLiteral("Магазин сопротивлений Р4831"), QStringLiteral("USB"),
                 QStringLiteral("драйвер управления не подключён"));
    addEquipment("THERMO_SIM", QStringLiteral("Имитатор датчика «термопара»"), QStringLiteral("стендовый интерфейс"),
                 QStringLiteral("интерфейс управления не подтверждён"));
    equipmentTable_->setMinimumHeight(240);
    left->addWidget(equipmentTable_, 1);

    auto* readinessBar = new QHBoxLayout;
    checkButton_ = new QPushButton(QStringLiteral("Проверить оборудование"));
    readinessLabel_ = new QLabel(QStringLiteral("Стенд ещё не проверен"));
    readinessLabel_->setWordWrap(true);
    readinessLabel_->setStyleSheet("color:#d7a95b;");
    readinessBar->addWidget(checkButton_);
    readinessBar->addWidget(readinessLabel_, 1);
    left->addLayout(readinessBar);

    auto* right = new QVBoxLayout;
    right->addWidget(makeSectionTitle(QStringLiteral("Ход и результат")));
    plot_ = new TestPlotWidget;
    right->addWidget(plot_);
    resultTable_ = new QTableWidget(0, 6);
    resultTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Тракт"), QStringLiteral("Точка"), QStringLiteral("В7"),
         QStringLiteral("Измерено"), QStringLiteral("Допуск"), QStringLiteral("Итог")});
    resultTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    resultTable_->verticalHeader()->hide();
    resultTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultTable_->setSelectionMode(QAbstractItemView::NoSelection);
    right->addWidget(resultTable_, 1);

    body->addLayout(left, 5);
    body->addLayout(right, 5);
    root->addLayout(body, 1);

    progress_ = new QProgressBar;
    progress_->setRange(0, 3);
    progress_->setValue(0);
    progress_->setFormat(QStringLiteral("Проверка не запущена"));
    root->addWidget(progress_);

    auto* actionBar = new QHBoxLayout;
    verdictLabel_ = new QLabel(QStringLiteral("ИТОГ НЕ СФОРМИРОВАН"));
    verdictLabel_->setStyleSheet(
        "font-size:16px; font-weight:700; color:#8e9aa8; padding:8px 12px;"
        "border:1px solid #35404d; border-radius:4px;");
    startButton_ = new QPushButton(QStringLiteral("Запустить проверку"));
    startButton_->setMinimumWidth(210);
    startButton_->setStyleSheet(
        "QPushButton { background:#286a49; border:1px solid #3d9a6b; font-size:14px; font-weight:700; padding:11px 18px; }"
        "QPushButton:hover { background:#327e58; }"
        "QPushButton:disabled { background:#171b21; border-color:#252b33; color:#626d79; }");
    actionBar->addWidget(verdictLabel_, 1);
    actionBar->addWidget(startButton_);
    root->addLayout(actionBar);

    demoTimer_ = new QTimer(this);
    demoTimer_->setInterval(450);
    connect(demoTimer_, &QTimer::timeout, this, &TestPage::advanceDemo);
    connect(checkButton_, &QPushButton::clicked, this, &TestPage::equipmentCheckRequested);
    connect(startButton_, &QPushButton::clicked, this, &TestPage::startSelectedTest);
    connect(objectCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TestPage::rebuildScopes);
    connect(scopeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TestPage::rebuildTests);
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TestPage::updateStartAvailability);
    connect(testCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TestPage::updateSelectionSummary);
    rebuildScopes();
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

void TestPage::rebuildScopes()
{
    const QString previous = selectedScopeCode();
    scopeCombo_->blockSignals(true);
    scopeCombo_->clear();
    scopeCombo_->addItem(QStringLiteral("Весь блок"), QStringLiteral("BLOCK"));
    const bool bsi = selectedObjectCode() == QStringLiteral("BSI");
    const CellInfo* cells = bsi ? kBsiCells : kUbsiCells;
    const int count = bsi ? int(std::size(kBsiCells)) : int(std::size(kUbsiCells));
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
    if (selectedScopeCode() == QStringLiteral("BLOCK")) {
        if (selectedObjectCode() == QStringLiteral("BSI")) {
            testCombo_->addItem(QStringLiteral("Функционирование в нормальных условиях · ТУ 5.6"),
                                QStringLiteral("BSI_NORMAL_5_6"));
        } else {
            testCombo_->addItem(QStringLiteral("Функционирование в нормальных условиях · ТУ 5.6"),
                                QStringLiteral("UBSI_NORMAL_5_6"));
        }
    } else {
        testCombo_->addItem(QStringLiteral("Связь и чтение параметров · диагностика"),
                            QStringLiteral("CELL_DIAGNOSTIC"));
        if (selectedScopeCode() == QStringLiteral("ЯЛК-96")) {
            testCombo_->addItem(QStringLiteral("Аналоговые каналы · 0 / 3,1 / 6,2 В"),
                                QStringLiteral("YALK_ANALOG"));
        }
    }
    const int oldIndex = testCombo_->findData(previous);
    testCombo_->setCurrentIndex(oldIndex >= 0 ? oldIndex : 0);
    testCombo_->blockSignals(false);
    updateSelectionSummary();
}

void TestPage::updateSelectionSummary()
{
    const QString object = selectedObjectCode();
    const QString scope = selectedScopeCode();
    const QString test = selectedTestCode();
    if (test.endsWith(QStringLiteral("NORMAL_5_6"))) {
        scopeLabel_->setText(object == QStringLiteral("BSI")
            ? QStringLiteral("БСИ · ТУ 5.6: проверка блока по штатной схеме В.1. Источники результата выбираются по тракту: Орбита, В7-78/1 и прямой RS-485. Это нормативный объём, аппаратная процедура ещё переносится.")
            : QStringLiteral("УБСИ · ТУ 5.6: потенциометрические, контактные, температурные и пьезоэлектрические каналы; питание, эталон 6,2 В, готовность и защиты. Аппаратная процедура ещё переносится."));
    } else if (test == QStringLiteral("YALK_ANALOG")) {
        scopeLabel_->setText(object == QStringLiteral("BSI")
            ? QStringLiteral("ЯЛК-96 БСИ: ИСД задаёт воздействие → В7-78/1 измеряет эталон → значение читается из телеметрии Орбита → допуск ±0,5 % шкалы 6,2 В.")
            : QStringLiteral("ЯЛК-96 УБСИ: ИСД задаёт воздействие → В7-78/1 измеряет эталон → Ethernet-адаптер читает 16 кодов → допуск ±0,5 % шкалы 6,2 В."));
    } else {
        scopeLabel_->setText(QStringLiteral("%1 · %2: диагностический прогон проверяет наличие источника данных, адресной привязки и стабильной выборки. Он не выдаётся за приёмочное испытание по ТУ.")
            .arg(object == QStringLiteral("BSI") ? QStringLiteral("БСИ") : QStringLiteral("УБСИ № 7"), scope));
    }
    plot_->setVisible(test == QStringLiteral("YALK_ANALOG") || test.endsWith(QStringLiteral("NORMAL_5_6")));
    const QStringList required = requiredEquipment();
    for (auto it = equipmentRows_.cbegin(); it != equipmentRows_.cend(); ++it) {
        equipmentTable_->setRowHidden(it->row, !required.contains(it.key()));
    }
    resultTable_->setRowCount(0);
    plot_->clear();
    progress_->setValue(0);
    progress_->setFormat(QStringLiteral("Проверка не запущена"));
    verdictLabel_->setText(QStringLiteral("ИТОГ НЕ СФОРМИРОВАН"));
    verdictLabel_->setStyleSheet(
        "font-size:16px; font-weight:700; color:#8e9aa8; padding:8px 12px;"
        "border:1px solid #35404d; border-radius:4px;");
    updateStartAvailability();
}

void TestPage::addEquipment(const QString& code, const QString& name,
                            const QString& connection, const QString& initialDetail)
{
    const int row = equipmentTable_->rowCount();
    equipmentTable_->insertRow(row);
    equipmentTable_->setItem(row, 0, new QTableWidgetItem(name));
    equipmentTable_->setItem(row, 1, new QTableWidgetItem(connection));
    equipmentTable_->setItem(row, 2, new QTableWidgetItem(QStringLiteral("НЕ ПРОВЕРЕНО")));
    equipmentTable_->setItem(row, 3, new QTableWidgetItem(initialDetail));
    equipmentRows_.insert(code, EquipmentRow{row, false});
}

void TestPage::setEquipmentStatus(const QString& code, bool ready, const QString& detail)
{
    auto it = equipmentRows_.find(code);
    if (it == equipmentRows_.end()) return;
    it->ready = ready;
    auto* state = equipmentTable_->item(it->row, 2);
    state->setText(ready ? QStringLiteral("ГОТОВО") : QStringLiteral("НЕ ГОТОВО"));
    state->setForeground(ready ? QColor("#70d79b") : QColor("#e1766d"));
    equipmentTable_->item(it->row, 3)->setText(detail);
    updateStartAvailability();
}

void TestPage::setEquipmentChecking(const QString& code, const QString& detail)
{
    auto it = equipmentRows_.find(code);
    if (it == equipmentRows_.end()) return;
    it->ready = false;
    equipmentTable_->item(it->row, 2)->setText(QStringLiteral("ПРОВЕРКА…"));
    equipmentTable_->item(it->row, 2)->setForeground(QColor("#d7a95b"));
    equipmentTable_->item(it->row, 3)->setText(detail);
}

QStringList TestPage::requiredEquipment() const
{
    const QString object = selectedObjectCode();
    const QString test = selectedTestCode();
    if (test == QStringLiteral("CELL_DIAGNOSTIC")) {
        return object == QStringLiteral("BSI") ? QStringList{"E20"} : QStringList{"RS485"};
    }
    if (test == QStringLiteral("YALK_ANALOG")) {
        return object == QStringLiteral("BSI")
            ? QStringList{"E20", "ISD", "V7"}
            : QStringList{"RS485", "ISD", "V7"};
    }
    if (test == QStringLiteral("BSI_NORMAL_5_6")) {
        return {"E20", "RS485", "ISD", "V7", "AKIP", "RIGOL", "R4831", "THERMO_SIM"};
    }
    return {"E20", "RS485", "ISD", "V7", "AKIP", "RIGOL", "G3", "R4831", "THERMO_SIM"};
}

void TestPage::updateStartAvailability()
{
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

    const QStringList required = requiredEquipment();
    QStringList missing;
    for (const auto& code : required) {
        const auto it = equipmentRows_.constFind(code);
        if (it == equipmentRows_.cend() || !it->ready) missing << code;
    }
    startButton_->setEnabled(missing.isEmpty() && !demoTimer_->isActive());
    if (missing.isEmpty()) {
        startButton_->setEnabled(false);
        readinessLabel_->setText(QStringLiteral(
            "Обязательные устройства готовы, но аппаратный исполнитель этой процедуры ещё не подключён"));
        readinessLabel_->setStyleSheet("color:#d7a95b;");
    } else {
        readinessLabel_->setText(QStringLiteral("Запуск заблокирован. Не готовы: %1").arg(missing.join(", ")));
        readinessLabel_->setStyleSheet("color:#d7a95b;");
    }
}

void TestPage::resetResults()
{
    resultTable_->setRowCount(0);
    plot_->clear();
    progress_->setValue(0);
    verdictLabel_->setText(QStringLiteral("ВЫПОЛНЯЕТСЯ…"));
    verdictLabel_->setStyleSheet(
        "font-size:16px; font-weight:700; color:#d7a95b; padding:8px 12px;"
        "border:1px solid #765d32; border-radius:4px;");
}

void TestPage::startSelectedTest()
{
    if (modeCombo_->currentIndex() != kDemoMode) return;
    resetResults();
    demoStep_ = 0;
    startButton_->setEnabled(false);
    progress_->setFormat(QStringLiteral("ДЕМО: подготовка %1").arg(selectedScopeCode()));
    demoTimer_->start();
}

void TestPage::advanceDemo()
{
    if (selectedTestCode() != QStringLiteral("YALK_ANALOG")) {
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
    verdictLabel_->setText(selectedTestCode() == QStringLiteral("YALK_ANALOG")
        ? QStringLiteral("ДЕМО: %1 — ОК · БЛОК ЦЕЛИКОМ НЕ ОЦЕНИВАЛСЯ").arg(target)
        : QStringLiteral("ДЕМО: %1 · ДИАГНОСТИКА ГОТОВА · НЕ РЕЗУЛЬТАТ ТУ").arg(target));
    verdictLabel_->setStyleSheet(
        "font-size:16px; font-weight:700; color:#70d79b; padding:8px 12px;"
        "border:1px solid #3d8f65; border-radius:4px;");
    progress_->setFormat(QStringLiteral("Демонстрационный прогон завершён"));
    updateStartAvailability();
}
