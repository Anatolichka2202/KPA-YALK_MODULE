#include "registrar_page.h"
#include "replacement_policy.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QDialog>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include "registrar.h"
#include "ktma/ubsi/production_ledger.h"

namespace {

QString verdictText(ktma::registrar::Verdict verdict)
{
    using ktma::registrar::Verdict;
    switch (verdict) {
    case Verdict::Ok: return QStringLiteral("НОРМА");
    case Verdict::Fail: return QStringLiteral("НЕ НОРМА");
    case Verdict::Incomplete: return QStringLiteral("НЕ ЗАВЕРШЕНО");
    case Verdict::InProgress: return QStringLiteral("В РАБОТЕ");
    case Verdict::Cancelled: return QStringLiteral("ОТМЕНЕНО");
    }
    return QStringLiteral("—");
}

QString componentTypeText(const QString& type)
{
    if (type == QStringLiteral("YALK-96")) return QStringLiteral("ЯЛК");
    if (type == QStringLiteral("YTP")) return QStringLiteral("ЯТП");
    if (type == QStringLiteral("YVP")) return QStringLiteral("ЯВП");
    if (type == QStringLiteral("YP-P")) return QStringLiteral("ЯП-П");
    return type;
}

QString stageText(const QString& stage)
{
    if (stage == QStringLiteral("Primary")) return QStringLiteral("Первичное");
    if (stage == QStringLiteral("ClimateNormal")) return QStringLiteral("Климат НУ");
    if (stage == QStringLiteral("ClimatePlus")) return QStringLiteral("Климат +");
    if (stage == QStringLiteral("ClimateMinus")) return QStringLiteral("Климат −");
    if (stage == QStringLiteral("PottingClimateNormal")) return QStringLiteral("Заливка · климат НУ");
    if (stage == QStringLiteral("PottingClimatePlus")) return QStringLiteral("Заливка · климат +");
    if (stage == QStringLiteral("PottingClimateMinus")) return QStringLiteral("Заливка · климат −");
    if (stage == QStringLiteral("InitialElectrical")) return QStringLiteral("Legacy · InitialElectrical");
    if (stage == QStringLiteral("PostVibrationElectrical")) return QStringLiteral("Legacy · PostVibrationElectrical");
    if (stage == QStringLiteral("PostClimateElectrical")) return QStringLiteral("Legacy · PostClimateElectrical");
    if (stage == QStringLiteral("FinalElectrical")) return QStringLiteral("Legacy · FinalElectrical");
    return stage;
}

QString runVerdictText(const QString& verdict)
{
    if (verdict == QStringLiteral("OK") || verdict == QStringLiteral("NORM")) return QStringLiteral("НОРМА");
    if (verdict == QStringLiteral("FAIL") || verdict == QStringLiteral("NOT_NORM")) return QStringLiteral("НЕ НОРМА");
    if (verdict == QStringLiteral("ERROR") || verdict == QStringLiteral("STAND_ERROR")) return QStringLiteral("ОШИБКА СТЕНДА");
    if (verdict == QStringLiteral("ABORTED") || verdict == QStringLiteral("STOPPED")) return QStringLiteral("ОСТАНОВЛЕНО");
    if (verdict == QStringLiteral("INCOMPLETE")) return QStringLiteral("НЕПОЛНАЯ ПРОВЕРКА");
    if (verdict == QStringLiteral("IN_PROGRESS")) return QStringLiteral("В РАБОТЕ");
    return verdict;
}

} // namespace

RegistrarPage::RegistrarPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("registrarPage"));
    setStyleSheet(QStringLiteral(
        "#registrarPage { background:#0e1115; color:#e6edf5; }"
        "QLineEdit { background:#161d25; color:#e6edf5; border:1px solid #344557;"
        " border-radius:4px; padding:8px; }"
        "QPushButton { background:#243c54; color:#f2f6fa; border:1px solid #5d87ad;"
        " border-radius:4px; padding:8px 14px; font-weight:700; }"
        "QPushButton:hover { background:#2c4b68; }"
        "QTableWidget { background:#141a21; color:#dbe4ed; gridline-color:#2e3945;"
        " border:1px solid #2e3945; }"
        "QHeaderView::section { background:#1a222c; color:#9fb2c5; border:0; padding:7px; font-weight:700; }"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(42, 36, 42, 36);
    layout->setSpacing(14);

    auto* headerRow = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("УБСИ · ИЗДЕЛИЕ И СОСТАВ"), this);
    title->setStyleSheet(QStringLiteral("font-size:26px; font-weight:700; color:#f2f6fa;"));
    auto* homeButton = new QPushButton(QStringLiteral("НА ГЛАВНУЮ"), this);
    auto* historyButton = new QPushButton(QStringLiteral("ИСТОРИЯ ПРОГОНОВ"), this);
    headerRow->addWidget(title);
    headerRow->addStretch();
    auto* productionButton=new QPushButton(QStringLiteral("ВЫБРАТЬ ПРОВЕРКУ"),this);
    headerRow->addWidget(productionButton);
    connect(productionButton,&QPushButton::clicked,this,[this]{
        if(!selectedProductionProduct()) {statusLabel_->setText(QStringLiteral("Выберите изделие и этап."));return;}
        emit productionRequested();
    });
    headerRow->addWidget(historyButton);
    headerRow->addWidget(homeButton);
    layout->addLayout(headerRow);

    auto* subtitle = new QLabel(
        QStringLiteral("Поиск и регистрация изделий УБСИ. Данные сохраняются в registrar.db."), this);
    subtitle->setStyleSheet(QStringLiteral("font-size:14px; color:#9aa7b5;"));
    layout->addWidget(subtitle);

    auto* stageRow = new QHBoxLayout;
    auto* stageLabel = new QLabel(QStringLiteral("Этап производства"), this);
    stageLabel->setStyleSheet(QStringLiteral("font-weight:700; color:#c5d3e0;"));
    stageCombo_ = new QComboBox(this);
    for (const auto& stage : {QStringLiteral("Primary"), QStringLiteral("ClimateNormal"),
                              QStringLiteral("ClimatePlus"), QStringLiteral("ClimateMinus"),
                              QStringLiteral("PottingClimateNormal"), QStringLiteral("PottingClimatePlus"),
                              QStringLiteral("PottingClimateMinus")}) {
        stageCombo_->addItem(stageText(stage), stage);
    }
    stageCombo_->setToolTip(QStringLiteral(
        "Этап — metadata production-run; он не является шагом измерительной процедуры."));
    stageRow->addWidget(stageLabel);
    stageRow->addWidget(stageCombo_, 1);
    layout->addLayout(stageRow);

    auto* searchRow = new QHBoxLayout;
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText(QStringLiteral("Номер изделия"));
    auto* refreshButton = new QPushButton(QStringLiteral("НАЙТИ"), this);
    searchRow->addWidget(searchEdit_, 1);
    searchRow->addWidget(refreshButton);
    layout->addLayout(searchRow);

    productsTable_ = new QTableWidget(this);
    productsTable_->setColumnCount(3);
    productsTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Изделие"), QStringLiteral("Тип"), QStringLiteral("Состояние")});
    productsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    productsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    productsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    productsTable_->verticalHeader()->setVisible(false);
    productsTable_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(productsTable_);

    compositionLabel_ = new QLabel(QStringLiteral("СОСТАВ · выберите изделие"), this);
    compositionLabel_->setStyleSheet(QStringLiteral("font-weight:700; color:#c5d3e0;"));
    layout->addWidget(compositionLabel_);

    compositionTable_ = new QTableWidget(this);
    compositionTable_->setColumnCount(4);
    compositionTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Ячейка"), QStringLiteral("Серийный номер"), QStringLiteral("Статус"),
         QStringLiteral("Причина снятия")});
    compositionTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    compositionTable_->verticalHeader()->setVisible(false);
    compositionTable_->horizontalHeader()->setStretchLastSection(true);
    compositionTable_->setMaximumHeight(170);
    layout->addWidget(compositionTable_);

    auto* componentRow = new QHBoxLayout;
    componentTypeCombo_ = new QComboBox(this);
    componentTypeCombo_->addItem(QStringLiteral("ЯЛК"), QStringLiteral("YALK-96"));
    componentTypeCombo_->addItem(QStringLiteral("ЯТП"), QStringLiteral("YTP"));
    componentTypeCombo_->addItem(QStringLiteral("ЯВП"), QStringLiteral("YVP"));
    componentTypeCombo_->addItem(QStringLiteral("ЯП-П"), QStringLiteral("YP-P"));
    componentSerialEdit_ = new QLineEdit(this);
    componentSerialEdit_->setPlaceholderText(QStringLiteral("SN ячейки"));
    auto* addComponentButton = new QPushButton(QStringLiteral("ДОБАВИТЬ ЯЧЕЙКУ"), this);
    componentRow->addWidget(componentTypeCombo_);
    componentRow->addWidget(componentSerialEdit_, 1);
    componentRow->addWidget(addComponentButton);
    layout->addLayout(componentRow);

    auto* replacementCaption = new QLabel(
        QStringLiteral("Заменить выбранную активную ячейку"), this);
    replacementCaption->setStyleSheet(QStringLiteral("font-weight:700; color:#c5d3e0;"));
    layout->addWidget(replacementCaption);
    auto* replacementRow = new QHBoxLayout;
    replacementSerialEdit_ = new QLineEdit(this);
    replacementSerialEdit_->setPlaceholderText(QStringLiteral("Новый SN"));
    replacementReasonEdit_ = new QLineEdit(this);
    replacementReasonEdit_->setPlaceholderText(QStringLiteral("Причина замены"));
    auto* replaceButton = new QPushButton(QStringLiteral("ЗАМЕНИТЬ ЯЧЕЙКУ"), this);
    replacementRow->addWidget(replacementSerialEdit_);
    replacementRow->addWidget(replacementReasonEdit_, 1);
    replacementRow->addWidget(replaceButton);
    layout->addLayout(replacementRow);

    auto* createCaption = new QLabel(QStringLiteral("Зарегистрировать новое изделие УБСИ"), this);
    createCaption->setStyleSheet(QStringLiteral("font-weight:700; color:#c5d3e0;"));
    layout->addWidget(createCaption);

    auto* createRow = new QHBoxLayout;
    serialEdit_ = new QLineEdit(this);
    serialEdit_->setPlaceholderText(QStringLiteral("Блок / УБСИ №"));
    auto* createButton = new QPushButton(QStringLiteral("ЗАРЕГИСТРИРОВАТЬ"), this);
    createRow->addWidget(serialEdit_, 1);
    createRow->addWidget(createButton);
    layout->addLayout(createRow);

    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet(QStringLiteral("color:#9aa7b5;"));
    layout->addWidget(statusLabel_);

    connect(refreshButton, &QPushButton::clicked, this, &RegistrarPage::refreshProducts);
    connect(searchEdit_, &QLineEdit::textChanged, this, &RegistrarPage::refreshProducts);
    connect(createButton, &QPushButton::clicked, this, &RegistrarPage::createProduct);
    connect(serialEdit_, &QLineEdit::returnPressed, this, &RegistrarPage::createProduct);
    connect(homeButton, &QPushButton::clicked, this, &RegistrarPage::homeRequested);
    connect(productsTable_, &QTableWidget::itemSelectionChanged,
            this, &RegistrarPage::refreshComposition);
    connect(addComponentButton, &QPushButton::clicked, this, &RegistrarPage::addComponent);
    connect(componentSerialEdit_, &QLineEdit::returnPressed, this, &RegistrarPage::addComponent);
    connect(replaceButton, &QPushButton::clicked, this, &RegistrarPage::replaceComponent);
    connect(historyButton, &QPushButton::clicked, this, &RegistrarPage::showStageHistory);
}

void RegistrarPage::setRegistrar(ktma::registrar::Registrar* registrar)
{
    registrar_ = registrar;
    refreshProducts();
}

std::optional<RegistrarPage::ProductionSelection>
RegistrarPage::selectedProductionSelection() const
{
    const QString productId = selectedProductId();
    const int row = compositionTable_->currentRow();
    const int productRow = productsTable_->currentRow();
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
            ktma::registrar::stageFromString(
                stageCombo_->currentData().toString().toStdString())};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void RegistrarPage::refreshProducts()
{
    const QString selectedId = selectedProductId();
    productsTable_->setRowCount(0);
    compositionTable_->setRowCount(0);
    compositionLabel_->setText(QStringLiteral("СОСТАВ · выберите изделие"));
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
            auto* serialItem = new QTableWidgetItem(serial);
            serialItem->setData(Qt::UserRole, QString::fromStdString(product.id));
            productsTable_->setItem(row, 0, serialItem);
            productsTable_->setItem(row, 1, new QTableWidgetItem(
                QString::fromStdString(product.productType)));
            productsTable_->setItem(row, 2, new QTableWidgetItem(
                verdictText(registrar_->productVerdict(product.id))));
            if (QString::fromStdString(product.id) == selectedId)
                productsTable_->selectRow(row);
            ++row;
        }
        statusLabel_->setText(row == 0
            ? QStringLiteral("Изделия не найдены.")
            : QStringLiteral("Найдено изделий: %1").arg(row));
        productsTable_->resizeColumnsToContents();
    } catch (const std::exception& error) {
        statusLabel_->setText(QStringLiteral("Не удалось прочитать registrar.db."));
        QMessageBox::warning(this, QStringLiteral("Регистратор КТМА"),
            QString::fromUtf8(error.what()));
    }
}

QString RegistrarPage::selectedProductId() const
{
    const int row = productsTable_->currentRow();
    if (row < 0) return {};
    const auto* item = productsTable_->item(row, 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void RegistrarPage::refreshComposition()
{
    compositionTable_->setRowCount(0);
    const QString productId = selectedProductId();
    if (!registrar_ || productId.isEmpty()) {
        compositionLabel_->setText(QStringLiteral("СОСТАВ · выберите изделие"));
        return;
    }

    try {
        const auto report = registrar_->productReport(productId.toStdString());
        compositionLabel_->setText(QStringLiteral("СОСТАВ · УБСИ № %1")
            .arg(QString::fromStdString(report.product.serialNumber)));
        int row = 0;
        for (const auto& component : report.components) {
            compositionTable_->insertRow(row);
            auto* typeItem = new QTableWidgetItem(
                componentTypeText(QString::fromStdString(component.componentType)));
            typeItem->setData(Qt::UserRole, QString::fromStdString(component.componentId));
            typeItem->setData(Qt::UserRole + 1, QString::fromStdString(component.componentType));
            typeItem->setData(Qt::UserRole + 2, component.active);
            compositionTable_->setItem(row, 0, typeItem);
            compositionTable_->setItem(row, 1, new QTableWidgetItem(
                QString::fromStdString(component.serialNumber)));
            compositionTable_->setItem(row, 2, new QTableWidgetItem(
                component.active ? QStringLiteral("УСТАНОВЛЕНА") : QStringLiteral("СНЯТА")));
            compositionTable_->setItem(row, 3, new QTableWidgetItem(
                QString::fromStdString(component.removalReason)));
            ++row;
        }
        compositionTable_->resizeColumnsToContents();
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("Состав изделия"),
            QString::fromUtf8(error.what()));
    }
}

void RegistrarPage::addComponent()
{
    if (!registrar_) return;
    const QString productId = selectedProductId();
    const QString serial = componentSerialEdit_->text().trimmed();
    if (productId.isEmpty()) {
        statusLabel_->setText(QStringLiteral("Сначала выберите изделие."));
        return;
    }
    if (serial.isEmpty()) {
        statusLabel_->setText(QStringLiteral("Введите SN ячейки."));
        return;
    }

    const QString type = componentTypeCombo_->currentData().toString();
    try {
        for (const auto& component : registrar_->listInstalledComponents(productId.toStdString())) {
            if (component.active && QString::fromStdString(component.componentType) == type) {
                statusLabel_->setText(QStringLiteral("Активная ячейка этого типа уже установлена."));
                return;
            }
        }
        const auto componentId = registrar_->createComponent(type.toStdString(), serial.toStdString());
        registrar_->installComponent(productId.toStdString(), componentId);
        componentSerialEdit_->clear();
        refreshComposition();
        refreshProducts();
        statusLabel_->setText(QStringLiteral("Ячейка %1 добавлена.")
            .arg(componentTypeText(type)));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("Добавление ячейки"),
            QString::fromUtf8(error.what()));
    }
}

void RegistrarPage::createProduct()
{
    if (!registrar_) return;
    const QString serial = serialEdit_->text().trimmed();
    if (serial.isEmpty()) {
        statusLabel_->setText(QStringLiteral("Введите номер изделия."));
        return;
    }

    try {
        registrar_->createProduct("UBSI", serial.toStdString());
        serialEdit_->clear();
        searchEdit_->setText(serial);
        for (int row = 0; row < productsTable_->rowCount(); ++row) {
            if (productsTable_->item(row, 0)->text() == serial) {
                productsTable_->selectRow(row);
                break;
            }
        }
        statusLabel_->setText(QStringLiteral("Изделие %1 зарегистрировано.").arg(serial));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("Регистрация изделия"),
            QString::fromUtf8(error.what()));
    }
}

void RegistrarPage::replaceComponent()
{
    if (!registrar_) return;
    const QString productId = selectedProductId();
    const int row = compositionTable_->currentRow();
    const auto* typeItem = row < 0 ? nullptr : compositionTable_->item(row, 0);
    const QString serial = replacementSerialEdit_->text().trimmed();
    const QString reason = replacementReasonEdit_->text().trimmed();
    if (productId.isEmpty() || !typeItem) {
        statusLabel_->setText(QStringLiteral("Выберите изделие и активную ячейку для замены."));
        return;
    }
    if (!typeItem->data(Qt::UserRole + 2).toBool()) {
        statusLabel_->setText(QStringLiteral("Заменять можно только активную ячейку."));
        return;
    }
    if (serial.isEmpty() || reason.isEmpty()) {
        statusLabel_->setText(QStringLiteral("Введите новый SN и причину замены."));
        return;
    }

    const QString componentId = typeItem->data(Qt::UserRole).toString();
    const QString type = typeItem->data(Qt::UserRole + 1).toString();
    try {
        // Create first: an error such as a duplicate component SN must not
        // remove a currently working cell from the product.
        registrar_->replaceComponent(productId.toStdString(), componentId.toStdString(),
            type.toStdString(), serial.toStdString(), reason.toStdString());
        replacementSerialEdit_->clear();
        replacementReasonEdit_->clear();
        refreshComposition();
        refreshProducts();
        const auto packages = ktma::registrar::replacementVerificationPackages(type.toStdString());
        QStringList packageText;
        for (const auto& package : packages) {
            if (package == "PROD_YALK_FULL") packageText << QStringLiteral("полная ЯЛК");
            else if (package == "PROD_YTP_FULL") packageText << QStringLiteral("полная ЯТП");
            else if (package == "PROD_YVP_FULL") packageText << QStringLiteral("ЯВП");
            else if (package == "PROD_YALK_89_96") packageText << QStringLiteral("ЯЛК 89–96");
            else if (package == "PROD_POWER_CONSUMPTION") packageText << QStringLiteral("питание / потребление");
        }
        statusLabel_->setText(QStringLiteral("Ячейка %1 заменена; прежняя SN сохранена в истории. Рекомендуемая проверка: %2.")
            .arg(componentTypeText(type), packageText.join(QStringLiteral(" + "))));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("Замена ячейки"),
            QString::fromUtf8(error.what()));
    }
}

void RegistrarPage::showStageHistory()
{
    if (!registrar_) return;
    const QString productId = selectedProductId();
    if (productId.isEmpty()) {
        statusLabel_->setText(QStringLiteral("Сначала выберите изделие."));
        return;
    }

    try {
        const auto report = registrar_->productReport(productId.toStdString());
        QHash<QString, QString> componentSerials;
        for (const auto& component : report.components) {
            componentSerials.insert(QString::fromStdString(component.componentId),
                                    QString::fromStdString(component.serialNumber));
        }

        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("История проверок · УБСИ № %1")
            .arg(QString::fromStdString(report.product.serialNumber)));
        dialog.resize(1040, 420);
        auto* layout = new QVBoxLayout(&dialog);
        auto* caption = new QLabel(
            QStringLiteral("Production, ПСИ по ТУ и legacy-этапы, сохранённые в registrar.db."),
            &dialog);
        caption->setStyleSheet(QStringLiteral("color:#9aa7b5;"));
        layout->addWidget(caption);

        auto* table = new QTableWidget(&dialog);
        table->setColumnCount(6);
        table->setHorizontalHeaderLabels({QStringLiteral("Этап"), QStringLiteral("Ячейка"),
            QStringLiteral("SN"), QStringLiteral("Итог"), QStringLiteral("run_id"),
            QStringLiteral("Завершено")});
        const auto tuRuns = registrar_->listTuRuns(productId.toStdString());
        const auto productionRuns = productionLedger_
            ? productionLedger_->listForProduct(productId.toStdString())
            : std::vector<ktma::ubsi::ProductionRunRecord>{};
        table->setRowCount(static_cast<int>(report.stageAttempts.size() + tuRuns.size()
                                            + productionRuns.size()));
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->verticalHeader()->setVisible(false);
        int row = 0;
        for (const auto& attempt : report.stageAttempts) {
            const QString componentId = QString::fromStdString(attempt.componentId);
            const QStringList values = {
                QStringLiteral("Legacy / ячейка · %1").arg(
                    stageText(QString::fromUtf8(ktma::registrar::toString(attempt.stage)))),
                componentId.isEmpty() ? QStringLiteral("Весь блок") : componentId,
                componentSerials.value(componentId, QStringLiteral("—")),
                verdictText(attempt.verdict),
                QString::fromStdString(attempt.runId),
                QString::fromStdString(attempt.finishedAt)};
            for (int column = 0; column < values.size(); ++column)
                table->setItem(row, column, new QTableWidgetItem(values[column]));
            ++row;
        }
        for (const auto& production : productionRuns) {
            const QStringList values = {
                QStringLiteral("Production · %1 · %2")
                    .arg(QString::fromUtf8(ktma::ubsi::toString(production.context.package)),
                         stageText(QString::fromUtf8(ktma::registrar::toString(production.context.stage)))),
                QStringLiteral("Весь блок"), QStringLiteral("—"),
                runVerdictText(QString::fromUtf8(ktma::ubsi::toString(production.status))),
                QString::fromStdString(production.runId),
                QString::fromStdString(production.finishedAt)};
            for (int column = 0; column < values.size(); ++column)
                table->setItem(row, column, new QTableWidgetItem(values[column]));
            ++row;
        }
        for (const auto& tu : tuRuns) {
            const QStringList values = {QStringLiteral("ПСИ по ТУ"), QStringLiteral("Весь блок"),
                QStringLiteral("—"), runVerdictText(QString::fromStdString(tu.verdict)),
                QString::fromStdString(tu.runId), QString::fromStdString(tu.finishedAt)};
            for (int column = 0; column < values.size(); ++column)
                table->setItem(row, column, new QTableWidgetItem(values[column]));
            ++row;
        }
        table->resizeColumnsToContents();
        table->horizontalHeader()->setStretchLastSection(true);
        layout->addWidget(table);

        auto* actions = new QHBoxLayout;
        actions->addStretch();
        auto* openReportButton = new QPushButton(QStringLiteral("ОТКРЫТЬ ОТЧЁТ"), &dialog);
        auto* closeButton = new QPushButton(QStringLiteral("ЗАКРЫТЬ"), &dialog);
        actions->addWidget(openReportButton);
        actions->addWidget(closeButton);
        connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
        connect(openReportButton, &QPushButton::clicked, &dialog, [table, &dialog] {
            const int row = table->currentRow();
            const auto* runItem = row < 0 ? nullptr : table->item(row, 4);
            const QString runId = runItem ? runItem->text() : QString();
            if (runId.isEmpty()) {
                QMessageBox::information(&dialog, QStringLiteral("Отчёт"),
                    QStringLiteral("У выбранной попытки нет сохранённого run_id."));
                return;
            }
            const QDir root(QCoreApplication::applicationDirPath());
            const QDir runDirectory(root.filePath(QStringLiteral("runs/") + runId));
            const QString productionReport = runDirectory.filePath(
                QStringLiteral("Производственный_отчет_%1.html").arg(runId));
            const QString channelReport = runDirectory.filePath(
                QStringLiteral("Ведомость_каналов_%1.html").arg(runId));
            const QString tuReport = runDirectory.filePath(
                QStringLiteral("Протокол_ТУ_%1.html").arg(runId));
            const QString reportPath = QFileInfo::exists(productionReport) ? productionReport
                : QFileInfo::exists(tuReport) ? tuReport
                : QFileInfo::exists(channelReport) ? channelReport : QString();
            if (reportPath.isEmpty()) {
                QMessageBox::information(&dialog, QStringLiteral("Отчёт"),
                    QStringLiteral("Файл отчёта для run %1 не найден.").arg(runId));
                return;
            }
            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(reportPath))) {
                QMessageBox::warning(&dialog, QStringLiteral("Отчёт"),
                    QStringLiteral("Не удалось открыть %1").arg(reportPath));
            }
        });
        layout->addLayout(actions);
        dialog.exec();
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("История прогонов"),
            QString::fromUtf8(error.what()));
    }
}
