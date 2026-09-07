#include "registrar_page.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "registrar.h"

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
    auto* title = new QLabel(QStringLiteral("АДМИНИСТРИРОВАНИЕ · ИЗДЕЛИЯ"), this);
    title->setStyleSheet(QStringLiteral("font-size:26px; font-weight:700; color:#f2f6fa;"));
    auto* homeButton = new QPushButton(QStringLiteral("НА ГЛАВНУЮ"), this);
    headerRow->addWidget(title);
    headerRow->addStretch();
    headerRow->addWidget(homeButton);
    layout->addLayout(headerRow);

    auto* subtitle = new QLabel(
        QStringLiteral("Поиск и регистрация изделий УБСИ. Данные сохраняются в registrar.db."), this);
    subtitle->setStyleSheet(QStringLiteral("font-size:14px; color:#9aa7b5;"));
    layout->addWidget(subtitle);

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
    compositionTable_->setColumnCount(3);
    compositionTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Ячейка"), QStringLiteral("Серийный номер"), QStringLiteral("Статус")});
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
}

void RegistrarPage::setRegistrar(ktma::registrar::Registrar* registrar)
{
    registrar_ = registrar;
    refreshProducts();
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
            compositionTable_->setItem(row, 0, new QTableWidgetItem(
                componentTypeText(QString::fromStdString(component.componentType))));
            compositionTable_->setItem(row, 1, new QTableWidgetItem(
                QString::fromStdString(component.serialNumber)));
            compositionTable_->setItem(row, 2, new QTableWidgetItem(
                component.active ? QStringLiteral("УСТАНОВЛЕНА") : QStringLiteral("СНЯТА")));
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
        statusLabel_->setText(QStringLiteral("Изделие %1 зарегистрировано.").arg(serial));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("Регистрация изделия"),
            QString::fromUtf8(error.what()));
    }
}
