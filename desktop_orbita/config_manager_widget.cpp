#include "config_manager_widget.h"
#include "metadata_service.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QListWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QHeaderView>
#include <QListWidgetItem>
#include <QColor>
#include "encoding_utils.h"

static QString signalTypeLabel(const QString& st) {
    if (st == "analog10")    return "аналог";
    if (st == "contact")     return "контакт";
    if (st.startsWith("fast")) return "быстрый";
    if (st == "temperature") return "темп.";
    if (st == "bus")         return "БУС";
    return "?";
}

ConfigManagerWidget::ConfigManagerWidget(MetadataService* db, QWidget* parent)
    : QWidget(parent), db_(db)
{
    auto* mainLayout = new QVBoxLayout(this);

    auto* splitter = new QSplitter(Qt::Horizontal);
    fileList_     = new QListWidget;
    previewTable_ = new QTableWidget;
    previewTable_->setColumnCount(4);
    previewTable_->setHorizontalHeaderLabels({"Адрес", "Параметр", "Категория", "Тип"});
    previewTable_->horizontalHeader()->setStretchLastSection(true);

    splitter->addWidget(fileList_);
    splitter->addWidget(previewTable_);
    splitter->setSizes({200, 600});

    auto* btnLayout     = new QHBoxLayout;
    auto* refreshFilesBtn = new QPushButton("Обновить список");
    auto* applyBtn        = new QPushButton("Применить");
    auto* refreshMetaBtn  = new QPushButton("Обновить метаданные (БД)");

    btnLayout->addWidget(refreshFilesBtn);
    btnLayout->addWidget(applyBtn);
    btnLayout->addWidget(refreshMetaBtn);
    btnLayout->addStretch();

    mainLayout->addWidget(splitter);
    mainLayout->addLayout(btnLayout);

    connect(fileList_, &QListWidget::itemClicked,
            [this](QListWidgetItem* item) { loadFilePreview(item->text()); });
    connect(applyBtn,        &QPushButton::clicked, this, &ConfigManagerWidget::onApplyClicked);
    connect(refreshFilesBtn, &QPushButton::clicked, this, &ConfigManagerWidget::refreshFileList);
    connect(refreshMetaBtn,  &QPushButton::clicked, this, &ConfigManagerWidget::refreshMetadataRequested);

    refreshFileList();
}

void ConfigManagerWidget::refreshFileList() {
    QDir configDir(QCoreApplication::applicationDirPath() + "/address");
    QStringList files = configDir.entryList({"*.txt"}, QDir::Files);
    fileList_->clear();
    fileList_->addItems(files);
}

void ConfigManagerWidget::updateMetadata() {
    if (fileList_->currentItem())
        loadFilePreview(fileList_->currentItem()->text());
}

void ConfigManagerWidget::loadFilePreview(const QString& fileName) {
    QString path = QCoreApplication::applicationDirPath() + "/address/" + fileName;
    std::string content = encoding::readFileToUtf8(path.toStdString());
    if (content.empty()) return;

    QString qcontent = QString::fromStdString(content);
    // Сохраняем все строки, включая пустые: оператор разделял ими группы
    QStringList lines = qcontent.split('\n');

    previewTable_->setRowCount(lines.size());
    int row = 0;
    for (const QString& rawLine : lines) {
        QString addr = rawLine.trimmed();

        if (addr.isEmpty()) {
            // Пустая строка-разделитель — показываем как визуальный разрыв
            for (int col = 0; col < 4; ++col) {
                auto* sep = new QTableWidgetItem(QString());
                sep->setBackground(QColor(0, 0, 0, 20));
                sep->setFlags(Qt::NoItemFlags);
                previewTable_->setItem(row, col, sep);
            }
            ++row;
            continue;
        }

        auto info = db_ ? db_->lookup(addr) : std::nullopt;
        QString name, category, typeLabel;
        if (info) {
            name      = info->name;
            category  = info->category;
            typeLabel = signalTypeLabel(info->signalType);
            if (info->isZu) typeLabel += " · ЗУ";
        }

        previewTable_->setItem(row, 0, new QTableWidgetItem(addr));
        previewTable_->setItem(row, 1, new QTableWidgetItem(name));
        previewTable_->setItem(row, 2, new QTableWidgetItem(category));
        previewTable_->setItem(row, 3, new QTableWidgetItem(typeLabel));
        ++row;
    }
    previewTable_->setRowCount(row);
    previewTable_->resizeColumnsToContents();
}

void ConfigManagerWidget::onApplyClicked() {
    auto* current = fileList_->currentItem();
    if (current) emit applyConfigRequested(current->text());
}
