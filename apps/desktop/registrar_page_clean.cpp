#include "registrar_page.h"
#include "replacement_policy.h"

#include <QComboBox>
#include <QDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "registrar.h"

namespace {

QString componentTypeText(const QString& type)
{
    if (type == QStringLiteral("YALK-96")) return QStringLiteral("ЯЛК-96");
    if (type == QStringLiteral("YTP")) return QStringLiteral("ЯТП");
    if (type == QStringLiteral("YVP")) return QStringLiteral("ЯВП-8");
    if (type == QStringLiteral("YP-P")) return QStringLiteral("ЯП-П");
    return type;
}

QString verdictText(ktma::registrar::Verdict verdict)
{
    using V = ktma::registrar::Verdict;
    switch (verdict) {
    case V::Ok: return QStringLiteral("НОРМА");
    case V::Fail: return QStringLiteral("НЕ НОРМА");
    case V::Incomplete: return QStringLiteral("НЕ ЗАВЕРШЕНО");
    case V::InProgress: return QStringLiteral("В РАБОТЕ");
    case V::Cancelled: return QStringLiteral("ОТМЕНЕНО");
    }
    return QStringLiteral("—");
}

QString stageText(const QString& value)
{
    if (value == QStringLiteral("Primary")) return QStringLiteral("Первичная проверка");
    if (value == QStringLiteral("ClimateNormal")) return QStringLiteral("Климатические испытания — нормальные условия");
    if (value == QStringLiteral("ClimateMinus")) return QStringLiteral("Климатические испытания — отрицательная температура");
    if (value == QStringLiteral("ClimatePlus")) return QStringLiteral("Климатические испытания — повышенная температура");
    if (value == QStringLiteral("PottingClimateNormal")) return QStringLiteral("После заливки — нормальные условия");
    if (value == QStringLiteral("PottingClimatePlus")) return QStringLiteral("После заливки — повышенная температура");
    if (value == QStringLiteral("PottingClimateMinus")) return QStringLiteral("После заливки — отрицательная температура");
    return value;
}

QFrame* panel(QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setProperty("panel", true);
    return frame;
}

QLabel* caption(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("caption", true);
    return label;
}

QLabel* heading(const QString& text, int point, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setPointSize(point);
    font.setBold(true);
    label->setFont(font);
    return label;
}

} // namespace

RegistrarPage::RegistrarPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("registrarPage"));
    setStyleSheet(QStringLiteral(
        "#registrarPage{background:#08131d;color:#eaf4fb;font-family:'Segoe UI';}"
        "QFrame[panel='true']{background:#102333;border:1px solid #264257;border-radius:8px;}"
        "QLabel[caption='true']{color:#8ea6b7;font-size:11px;font-weight:700;}"
        "QPushButton{background:#132a3d;color:#eaf4fb;border:1px solid #264257;border-radius:7px;padding:8px 13px;}"
        "QPushButton:hover{border-color:#58a5ff;}"
        "QPushButton#primary{background:#2e7de9;border-color:#58a5ff;font-weight:700;}"
        "QLineEdit,QComboBox{background:#0e1e2c;color:#eaf4fb;border:1px solid #264257;border-radius:6px;padding:8px;}"
        "QTableWidget{background:#0e1e2c;color:#eaf4fb;border:1px solid #1a3346;gridline-color:#1a3346;selection-background-color:#123b58;}"
        "QHeaderView::section{background:#0c1b28;color:#8ea6b7;border:0;border-bottom:1px solid #1a3346;padding:7px;font-weight:700;}"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(42, 30, 42, 30);
    root->setSpacing(14);

    auto* top = new QHBoxLayout;
    auto* back = new QPushButton(QStringLiteral("← КТМА"), this);
    auto* title = heading(QStringLiteral("Администрирование УБСИ"), 24, this);
    auto* history = new QPushButton(QStringLiteral("История"), this);
    auto* production = new QPushButton(QStringLiteral("Выбрать проверку"), this);
    production->setObjectName(QStringLiteral("primary"));
    top->addWidget(back);
    top->addWidget(title);
    top->addStretch();
    top->addWidget(history);
    top->addWidget(production);
    root->addLayout(top);

    auto* body = new QHBoxLayout;
    body->setSpacing(18);

    auto* master = panel(this);
    master->setMinimumWidth(310);
    master->setMaximumWidth(420);
    auto* masterLayout = new QVBoxLayout(master);
    masterLayout->setContentsMargins(16, 16, 16, 16);
    masterLayout->setSpacing(10);
    masterLayout->addWidget(caption(QStringLiteral("УБСИ"), master));

    searchEdit_ = new QLineEdit(master);
    searchEdit_->setPlaceholderText(QStringLiteral("Поиск по SN"));
    masterLayout->addWidget(searchEdit_);

    productsTable_ = new QTableWidget(master);
    productsTable_->setColumnCount(1);
    productsTable_->horizontalHeader()->hide();
    productsTable_->verticalHeader()->hide();
    productsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    productsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    productsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    productsTable_->setShowGrid(false);
    productsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    productsTable_->verticalHeader()->setDefaultSectionSize(42);
    masterLayout->addWidget(productsTable_, 1);

    auto* createRow = new QHBoxLayout;
    serialEdit_ = new QLineEdit(master);
    serialEdit_->setPlaceholderText(QStringLiteral("Новый SN УБСИ"));
    auto* create = new QPushButton(QStringLiteral("+"), master);
    create->setFixedWidth(42);
    createRow->addWidget(serialEdit_, 1);
    createRow->addWidget(create);
    masterLayout->addLayout(createRow);
    body->addWidget(master, 1);

    auto* detail = panel(this);
    auto* detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(18, 18, 18, 18);
    detailLayout->setSpacing(12);

    compositionLabel_ = heading(QStringLiteral("Выберите УБСИ"), 20, detail);
    detailLayout->addWidget(compositionLabel_);

    auto* stageRow = new QHBoxLayout;
    stageRow->addWidget(caption(QStringLiteral("ПРОИЗВОДСТВЕННЫЙ ЭТАП"), detail));
    stageCombo_ = new QComboBox(detail);
    for (const auto& value : {
             QStringLiteral("Primary"), QStringLiteral("ClimateNormal"),
             QStringLiteral("ClimateMinus"), QStringLiteral("ClimatePlus"),
             QStringLiteral("PottingClimateNormal"), QStringLiteral("PottingClimatePlus"),
             QStringLiteral("PottingClimateMinus")}) {
        stageCombo_->addItem(stageText(value), value);
    }
    stageRow->addWidget(stageCombo_, 1);
    detailLayout->addLayout(stageRow);

    detailLayout->addWidget(caption(QStringLiteral("СОСТАВ"), detail));
    compositionTable_ = new QTableWidget(detail);
    compositionTable_->setColumnCount(4);
    compositionTable_->setHorizontalHeaderLabels({
        QStringLiteral("Ячейка"), QStringLiteral("SN"),
        QStringLiteral("Состояние"), QStringLiteral("Причина снятия")});
    compositionTable_->verticalHeader()->hide();
    compositionTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    compositionTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    compositionTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    compositionTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    compositionTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    compositionTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    compositionTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    detailLayout->addWidget(compositionTable_, 1);

    auto* addPanel = panel(detail);
    auto* addGrid = new QGridLayout(addPanel);
    addGrid->setContentsMargins(12, 12, 12, 12);
    addGrid->addWidget(caption(QStringLiteral("ДОБАВИТЬ ЯЧЕЙКУ"), addPanel), 0, 0, 1, 3);
    componentTypeCombo_ = new QComboBox(addPanel);
    componentTypeCombo_->addItem(QStringLiteral("ЯЛК-96"), QStringLiteral("YALK-96"));
    componentTypeCombo_->addItem(QStringLiteral("ЯТП"), QStringLiteral("YTP"));
    componentTypeCombo_->addItem(QStringLiteral("ЯВП-8"), QStringLiteral("YVP"));
    componentTypeCombo_->addItem(QStringLiteral("ЯП-П"), QStringLiteral("YP-P"));
    componentSerialEdit_ = new QLineEdit(addPanel);
    componentSerialEdit_->setPlaceholderText(QStringLiteral("SN ячейки"));
    auto* addComponentButton = new QPushButton(QStringLiteral("Добавить"), addPanel);
    addGrid->addWidget(componentTypeCombo_, 1, 0);
    addGrid->addWidget(componentSerialEdit_, 1, 1);
    addGrid->addWidget(addComponentButton, 1, 2);
    detailLayout->addWidget(addPanel);

    auto* replacePanel = panel(detail);
    auto* replaceGrid = new QGridLayout(replacePanel);
    replaceGrid->setContentsMargins(12, 12, 12, 12);
    replaceGrid->addWidget(caption(QStringLiteral("ЗАМЕНИТЬ ВЫБРАННУЮ АКТИВНУЮ ЯЧЕЙКУ"), replacePanel), 0, 0, 1, 3);
    replacementSerialEdit_ = new QLineEdit(replacePanel);
    replacementSerialEdit_->setPlaceholderText(QStringLiteral("Новый SN"));
    replacementReasonEdit_ = new QLineEdit(replacePanel);
    replacementReasonEdit_->setPlaceholderText(QStringLiteral("Причина замены"));
    auto* replace = new QPushButton(QStringLiteral("Заменить"), replacePanel);
    replaceGrid->addWidget(replacementSerialEdit_, 1, 0);
    replaceGrid->addWidget(replacementReasonEdit_, 1, 1);
    replaceGrid->addWidget(replace, 1, 2);
    detailLayout->addWidget(replacePanel);

    statusLabel_ = new QLabel(detail);
    statusLabel_->setProperty("muted", true);
    statusLabel_->setWordWrap(true);
    detailLayout->addWidget(statusLabel_);

    body->addWidget(detail, 3);
    root->addLayout(body, 1);

    connect(back, &QPushButton::clicked, this, &RegistrarPage::homeRequested);
    connect(production, &QPushButton::clicked, this, [this] {
        if (!selectedProductionProduct()) {
            statusLabel_->setText(QStringLiteral("Выберите изделие и этап."));
            return;
        }
        emit productionRequested();
    });
    connect(history, &QPushButton::clicked, this, &RegistrarPage::showStageHistory);
    connect(searchEdit_, &QLineEdit::textChanged, this, &RegistrarPage::refreshProducts);
    connect(create, &QPushButton::clicked, this, &RegistrarPage::createProduct);
    connect(serialEdit_, &QLineEdit::returnPressed, this, &RegistrarPage::createProduct);
    connect(productsTable_, &QTableWidget::itemSelectionChanged, this, &RegistrarPage::refreshComposition);
    connect(addComponentButton, &QPushButton::clicked, this, &RegistrarPage::addComponent);
    connect(componentSerialEdit_, &QLineEdit::returnPressed, this, &RegistrarPage::addComponent);
    connect(replace, &QPushButton::clicked, this, &RegistrarPage::replaceComponent);
}

void RegistrarPage::setRegistrar(ktma::registrar::Registrar* registrar)
{
    registrar_ = registrar;
    refreshProducts();
}

QString RegistrarPage::selectedProductId() const
{
    const int row = productsTable_ ? productsTable_->currentRow() : -1;
    if (row < 0) return {};
    const auto* item = productsTable_->item(row, 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

std::optional<RegistrarPage::ProductionSelection> RegistrarPage::selectedProductionSelection() const
{
    const QString productId = selectedProductId();
    const int row = compositionTable_ ? compositionTable_->currentRow() : -1;
    const int productRow = productsTable_ ? productsTable_->currentRow() : -1;
    const auto* typeItem = row < 0 ? nullptr : compositionTable_->item(row, 0);
    const auto* productItem = productRow < 0 ? nullptr : productsTable_->item(productRow, 0);
    if (productId.isEmpty() || !typeItem || !productItem
        || !typeItem->data(Qt::UserRole + 2).toBool()) {
        return std::nullopt;
    }

    try {
        return ProductionSelection{
            productId,
            productItem->text(),
            typeItem->data(Qt::UserRole).toString(),
            typeItem->data(Qt::UserRole + 1).toString(),
            compositionTable_->item(row, 1)->text(),
            ktma::registrar::stageFromString(stageCombo_->currentData().toString().toStdString())};
    } catch (...) {
        return std::nullopt;
    }
}

void RegistrarPage::refreshProducts()
{
    const QString selected = selectedProductId();
    productsTable_->setRowCount(0);
    compositionTable_->setRowCount(0);
    compositionLabel_->setText(QStringLiteral("Выберите УБСИ"));

    if (!registrar_) {
        statusLabel_->setText(QStringLiteral("Регистратор недоступен."));
        return;
    }

    try {
        const QString query = searchEdit_->text().trimmed();
        int row = 0;
        for (const auto& product : registrar_->listProducts()) {
            const QString serial = QString::fromStdString(product.serialNumber);
            if (!query.isEmpty() && !serial.contains(query, Qt::CaseInsensitive)) continue;

            productsTable_->insertRow(row);
            auto* item = new QTableWidgetItem(serial);
            item->setData(Qt::UserRole, QString::fromStdString(product.id));
            item->setToolTip(QStringLiteral("%1 · %2")
                .arg(QString::fromStdString(product.productType),
                     verdictText(registrar_->productVerdict(product.id))));
            productsTable_->setItem(row, 0, item);
            if (QString::fromStdString(product.id) == selected) productsTable_->selectRow(row);
            ++row;
        }
        statusLabel_->setText(row ? QStringLiteral("Изделий: %1").arg(row)
                                  : QStringLiteral("Изделия не найдены."));
    } catch (const std::exception& error) {
        statusLabel_->setText(QStringLiteral("Не удалось прочитать registrar.db"));
        QMessageBox::warning(this, QStringLiteral("Регистратор"), QString::fromUtf8(error.what()));
    }
}

void RegistrarPage::refreshComposition()
{
    compositionTable_->setRowCount(0);
    const QString productId = selectedProductId();
    if (!registrar_ || productId.isEmpty()) {
        compositionLabel_->setText(QStringLiteral("Выберите УБСИ"));
        return;
    }

    try {
        const auto report = registrar_->productReport(productId.toStdString());
        compositionLabel_->setText(QStringLiteral("УБСИ %1")
            .arg(QString::fromStdString(report.product.serialNumber)));
        int row = 0;
        for (const auto& binding : report.components) {
            compositionTable_->insertRow(row);
            auto* type = new QTableWidgetItem(componentTypeText(QString::fromStdString(binding.componentType)));
            type->setData(Qt::UserRole, QString::fromStdString(binding.componentId));
            type->setData(Qt::UserRole + 1, QString::fromStdString(binding.componentType));
            type->setData(Qt::UserRole + 2, binding.active);
            compositionTable_->setItem(row, 0, type);
            compositionTable_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(binding.serialNumber)));
            compositionTable_->setItem(row, 2, new QTableWidgetItem(
                binding.active ? QStringLiteral("УСТАНОВЛЕНА") : QStringLiteral("СНЯТА")));
            compositionTable_->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(binding.removalReason)));
            ++row;
        }
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("Состав"), QString::fromUtf8(error.what()));
    }
}

void RegistrarPage::createProduct()
{
    if (!registrar_) return;
    const QString serial = serialEdit_->text().trimmed();
    if (serial.isEmpty()) {
        statusLabel_->setText(QStringLiteral("Введите SN УБСИ."));
        return;
    }

    try {
        if (registrar_->findProductBySerial(serial.toStdString())) {
            statusLabel_->setText(QStringLiteral("УБСИ %1 уже зарегистрировано.").arg(serial));
            return;
        }
        registrar_->createProduct("UBSI", serial.toStdString());
        serialEdit_->clear();
        searchEdit_->setText(serial);
        refreshProducts();
        for (int row = 0; row < productsTable_->rowCount(); ++row) {
            if (productsTable_->item(row, 0)->text() == serial) {
                productsTable_->selectRow(row);
                break;
            }
        }
        refreshComposition();
        statusLabel_->setText(QStringLiteral("УБСИ %1 зарегистрировано. Заполните состав справа.").arg(serial));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("Регистрация"), QString::fromUtf8(error.what()));
    }
}

void RegistrarPage::addComponent()
{
    if (!registrar_) return;
    const QString productId = selectedProductId();
    const QString serial = componentSerialEdit_->text().trimmed();
    const QString type = componentTypeCombo_->currentData().toString();
    if (productId.isEmpty()) {
        statusLabel_->setText(QStringLiteral("Сначала выберите УБСИ."));
        return;
    }
    if (serial.isEmpty()) {
        statusLabel_->setText(QStringLiteral("Введите SN ячейки."));
        return;
    }

    try {
        for (const auto& binding : registrar_->listInstalledComponents(productId.toStdString())) {
            if (binding.active && QString::fromStdString(binding.componentType) == type) {
                statusLabel_->setText(QStringLiteral("Активная ячейка этого типа уже установлена."));
                return;
            }
        }
        const auto id = registrar_->createComponent(type.toStdString(), serial.toStdString());
        registrar_->installComponent(productId.toStdString(), id);
        componentSerialEdit_->clear();
        refreshComposition();
        refreshProducts();
        statusLabel_->setText(QStringLiteral("%1 добавлена.").arg(componentTypeText(type)));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("Добавление ячейки"), QString::fromUtf8(error.what()));
    }
}

void RegistrarPage::replaceComponent()
{
    if (!registrar_) return;
    const QString productId = selectedProductId();
    const int row = compositionTable_ ? compositionTable_->currentRow() : -1;
    const auto* typeItem = row < 0 ? nullptr : compositionTable_->item(row, 0);
    const QString serial = replacementSerialEdit_->text().trimmed();
    const QString reason = replacementReasonEdit_->text().trimmed();

    if (productId.isEmpty() || !typeItem) {
        statusLabel_->setText(QStringLiteral("Выберите активную ячейку."));
        return;
    }
    if (!typeItem->data(Qt::UserRole + 2).toBool()) {
        statusLabel_->setText(QStringLiteral("Заменять можно только активную ячейку."));
        return;
    }
    if (serial.isEmpty() || reason.isEmpty()) {
        statusLabel_->setText(QStringLiteral("Введите новый SN и причину."));
        return;
    }

    try {
        const QString componentId = typeItem->data(Qt::UserRole).toString();
        const QString type = typeItem->data(Qt::UserRole + 1).toString();
        registrar_->replaceComponent(productId.toStdString(), componentId.toStdString(),
                                     type.toStdString(), serial.toStdString(), reason.toStdString());
        replacementSerialEdit_->clear();
        replacementReasonEdit_->clear();
        refreshComposition();
        refreshProducts();

        const auto packages = ktma::registrar::replacementVerificationPackages(type.toStdString());
        QStringList packageNames;
        for (const auto& package : packages) packageNames << QString::fromStdString(package);
        statusLabel_->setText(packageNames.isEmpty()
            ? QStringLiteral("Ячейка %1 заменена; история сохранена.").arg(componentTypeText(type))
            : QStringLiteral("Ячейка %1 заменена. Повторная проверка: %2")
                  .arg(componentTypeText(type), packageNames.join(QStringLiteral(", "))));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("Замена"), QString::fromUtf8(error.what()));
    }
}

void RegistrarPage::showStageHistory()
{
    if (!registrar_) return;
    const QString productId = selectedProductId();
    if (productId.isEmpty()) {
        statusLabel_->setText(QStringLiteral("Выберите УБСИ."));
        return;
    }

    try {
        const auto report = registrar_->productReport(productId.toStdString());
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("История · УБСИ %1")
            .arg(QString::fromStdString(report.product.serialNumber)));
        dialog.resize(980, 560);

        auto* layout = new QVBoxLayout(&dialog);
        auto* table = new QTableWidget(&dialog);
        table->setColumnCount(6);
        table->setHorizontalHeaderLabels({
            QStringLiteral("Ячейка"), QStringLiteral("Этап"), QStringLiteral("Итог"),
            QStringLiteral("Run ID"), QStringLiteral("Начало"), QStringLiteral("Завершение")});
        table->verticalHeader()->hide();
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);

        int row = 0;
        for (const auto& attempt : report.stageAttempts) {
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(attempt.componentId)));
            table->setItem(row, 1, new QTableWidgetItem(stageText(
                QString::fromUtf8(ktma::registrar::toString(attempt.stage)))));
            table->setItem(row, 2, new QTableWidgetItem(verdictText(attempt.verdict)));
            table->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(attempt.runId)));
            table->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(attempt.openedAt)));
            table->setItem(row, 5, new QTableWidgetItem(QString::fromStdString(attempt.finishedAt)));
            ++row;
        }
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
        layout->addWidget(table);

        auto* close = new QPushButton(QStringLiteral("Закрыть"), &dialog);
        connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
        layout->addWidget(close, 0, Qt::AlignRight);
        dialog.exec();
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("История"), QString::fromUtf8(error.what()));
    }
}
