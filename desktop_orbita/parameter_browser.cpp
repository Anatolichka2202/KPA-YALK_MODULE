#include "parameter_browser.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDebug>
#include <QMessageBox>
#include <QGroupBox>

ParameterBrowser::ParameterBrowser(MetadataService* db, QWidget *parent)
    : QWidget(parent), db_(db), currentAddress_("")
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Верхняя часть: дерево параметров
    tree_ = new QTreeWidget;
    tree_->setHeaderLabels({"Параметр", "Адрес", "Категория"});
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mainLayout->addWidget(tree_);

    // Кнопка добавления в конфиг
    addButton_ = new QPushButton("Добавить выбранные в конфигурацию");
    mainLayout->addWidget(addButton_);

    // Панель редактора допусков
    QGroupBox* editorBox = new QGroupBox("Редактор допусков");
    QVBoxLayout* editorLayout = new QVBoxLayout(editorBox);

    // Показываем адрес текущего параметра
    addressLabel_ = new QLabel("Нет выбранного параметра");
    addressLabel_->setStyleSheet("color: #aab4c0; font-weight: bold;");
    editorLayout->addWidget(addressLabel_);

    // Три spinbox'а для lo, nominal, hi
    QHBoxLayout* spinLayout = new QHBoxLayout;

    spinLayout->addWidget(new QLabel("Нижний (lo):"));
    loSpinBox_ = new QDoubleSpinBox;
    loSpinBox_->setRange(0, 65535);
    loSpinBox_->setDecimals(2);
    spinLayout->addWidget(loSpinBox_);

    spinLayout->addWidget(new QLabel("Номинал:"));
    nominalSpinBox_ = new QDoubleSpinBox;
    nominalSpinBox_->setRange(0, 65535);
    nominalSpinBox_->setDecimals(2);
    spinLayout->addWidget(nominalSpinBox_);

    spinLayout->addWidget(new QLabel("Верхний (hi):"));
    hiSpinBox_ = new QDoubleSpinBox;
    hiSpinBox_->setRange(0, 65535);
    hiSpinBox_->setDecimals(2);
    spinLayout->addWidget(hiSpinBox_);

    editorLayout->addLayout(spinLayout);

    // Две кнопки сохранения
    QHBoxLayout* buttonLayout = new QHBoxLayout;
    saveSessionBtn_ = new QPushButton("Сохранить в сессию");
    saveDbBtn_ = new QPushButton("Сохранить в БД");
    buttonLayout->addWidget(saveSessionBtn_);
    buttonLayout->addWidget(saveDbBtn_);
    editorLayout->addLayout(buttonLayout);

    mainLayout->addWidget(editorBox);

    // Подключаем сигналы
    connect(addButton_, &QPushButton::clicked, this, &ParameterBrowser::onAddSelected);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, &ParameterBrowser::onItemDoubleClicked);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &ParameterBrowser::onItemSelectionChanged);
    connect(saveSessionBtn_, &QPushButton::clicked, this, &ParameterBrowser::onSaveToSession);
    connect(saveDbBtn_, &QPushButton::clicked, this, &ParameterBrowser::onSaveToDb);

    rebuildTree();
}

void ParameterBrowser::rebuildTree() {
    if (!db_) return;
    tree_->clear();
    QStringList categories = db_->getAllCategories();
    for (const QString& cat : categories) {
        QTreeWidgetItem* catItem = new QTreeWidgetItem(tree_);
        catItem->setText(0, cat);
        catItem->setFlags(catItem->flags() & ~Qt::ItemIsSelectable);
        QHash<QString, QString> params = db_->getParametersByCategory(cat);
        for (auto it = params.begin(); it != params.end(); ++it) {
            QTreeWidgetItem* paramItem = new QTreeWidgetItem(catItem);
            paramItem->setText(0, it.value());              // имя параметра
            paramItem->setText(1, it.key());                // нормализованный адрес
            paramItem->setText(2, cat);
            paramItem->setData(0, Qt::UserRole, it.key());  // сохраняем адрес
        }
        catItem->setExpanded(true);
    }
    tree_->resizeColumnToContents(0);
    tree_->resizeColumnToContents(1);
}

void ParameterBrowser::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    if (!item->parent()) return; // не категория
    QString addr = item->data(0, Qt::UserRole).toString();
    QString name = item->text(0);
    if (!addr.isEmpty())
        emit parametersSelected({addr}, {name});
}

void ParameterBrowser::onAddSelected() {
    QList<QString> addresses, names;
    QList<QTreeWidgetItem*> selected = tree_->selectedItems();
    for (QTreeWidgetItem* item : selected) {
        if (item->parent()) {
            addresses << item->data(0, Qt::UserRole).toString();
            names << item->text(0);
        }
    }
    if (!addresses.isEmpty())
        emit parametersSelected(addresses, names);
}

void ParameterBrowser::onItemSelectionChanged() {
    updateEditorPanel();
}

void ParameterBrowser::updateEditorPanel() {
    QList<QTreeWidgetItem*> selected = tree_->selectedItems();
    if (selected.isEmpty() || !selected[0]->parent()) {
        // Ничего не выбрано или выбрана категория
        currentAddress_ = "";
        addressLabel_->setText("Нет выбранного параметра");
        loSpinBox_->setValue(0);
        nominalSpinBox_->setValue(0);
        hiSpinBox_->setValue(0);
        saveSessionBtn_->setEnabled(false);
        saveDbBtn_->setEnabled(false);
        return;
    }

    QTreeWidgetItem* item = selected[0];
    currentAddress_ = item->data(0, Qt::UserRole).toString();

    if (!db_ || currentAddress_.isEmpty()) {
        addressLabel_->setText("Ошибка адреса");
        saveSessionBtn_->setEnabled(false);
        saveDbBtn_->setEnabled(false);
        return;
    }

    // Получаем информацию из БД
    auto info = db_->lookup(currentAddress_);
    if (!info) {
        addressLabel_->setText(QString("Адрес: %1 (не найден в БД)").arg(currentAddress_));
        loSpinBox_->setValue(0);
        nominalSpinBox_->setValue(0);
        hiSpinBox_->setValue(0);
        saveSessionBtn_->setEnabled(true);
        saveDbBtn_->setEnabled(false); // нельзя сохранить в БД если нет записи
        return;
    }

    addressLabel_->setText(QString("Адрес: %1").arg(currentAddress_));

    if (info->hasTolerance) {
        loSpinBox_->setValue(info->lo);
        nominalSpinBox_->setValue(info->nominal);
        hiSpinBox_->setValue(info->hi);
    } else {
        loSpinBox_->setValue(0);
        nominalSpinBox_->setValue(0);
        hiSpinBox_->setValue(0);
    }

    saveSessionBtn_->setEnabled(true);
    saveDbBtn_->setEnabled(info->id >= 0); // можно сохранить в БД если есть id
}

void ParameterBrowser::onSaveToSession() {
    if (currentAddress_.isEmpty()) {
        QMessageBox::warning(this, "Предупреждение", "Параметр не выбран");
        return;
    }

    double lo = loSpinBox_->value();
    double nominal = nominalSpinBox_->value();
    double hi = hiSpinBox_->value();

    if (lo > hi) {
        QMessageBox::warning(this, "Ошибка", "Нижний допуск не должен быть больше верхнего");
        return;
    }

    emit overrideTolerance(currentAddress_, lo, nominal, hi);
    QMessageBox::information(this, "Успех", "Допуск сохранён в сессию");
}

void ParameterBrowser::onSaveToDb() {
    if (currentAddress_.isEmpty()) {
        QMessageBox::warning(this, "Предупреждение", "Параметр не выбран");
        return;
    }

    if (!db_) {
        QMessageBox::critical(this, "Ошибка", "БД не инициализирована");
        return;
    }

    double lo = loSpinBox_->value();
    double hi = hiSpinBox_->value();

    if (lo > hi) {
        QMessageBox::warning(this, "Ошибка", "Нижний допуск не должен быть больше верхнего");
        return;
    }

    bool success = db_->setTolerance(currentAddress_, lo, hi);
    if (success) {
        QMessageBox::information(this, "Успех", "Допуск сохранён в базу данных");
        updateEditorPanel(); // обновляем панель после сохранения
    } else {
        QMessageBox::critical(this, "Ошибка", "Не удалось сохранить допуск в БД");
    }
}
