#include "registrar_page.h"
#include "replacement_policy.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include "registrar.h"
#include "ktma/ubsi/production_ledger.h"

namespace {
QString componentTypeText(const QString& type)
{
    if(type==QStringLiteral("YALK-96"))return QStringLiteral("ЯЛК-96");
    if(type==QStringLiteral("YTP"))return QStringLiteral("ЯТП");
    if(type==QStringLiteral("YVP"))return QStringLiteral("ЯВП-8");
    if(type==QStringLiteral("YP-P"))return QStringLiteral("ЯП-П");
    return type;
}
QString verdictText(ktma::registrar::Verdict verdict)
{
    using V=ktma::registrar::Verdict;
    switch(verdict){case V::Ok:return QStringLiteral("НОРМА");case V::Fail:return QStringLiteral("НЕ НОРМА");case V::Incomplete:return QStringLiteral("НЕ ЗАВЕРШЕНО");case V::InProgress:return QStringLiteral("В РАБОТЕ");case V::Cancelled:return QStringLiteral("ОТМЕНЕНО");}
    return QStringLiteral("—");
}
QString stageText(const QString& s)
{
    if(s==QStringLiteral("Primary"))return QStringLiteral("Первичная проверка");
    if(s==QStringLiteral("ClimateNormal"))return QStringLiteral("Климатические испытания — нормальные условия");
    if(s==QStringLiteral("ClimateMinus"))return QStringLiteral("Климатические испытания — отрицательная температура");
    if(s==QStringLiteral("ClimatePlus"))return QStringLiteral("Климатические испытания — повышенная температура");
    if(s==QStringLiteral("PottingClimateNormal"))return QStringLiteral("После заливки — нормальные условия");
    if(s==QStringLiteral("PottingClimatePlus"))return QStringLiteral("После заливки — повышенная температура");
    if(s==QStringLiteral("PottingClimateMinus"))return QStringLiteral("После заливки — отрицательная температура");
    return s;
}
QFrame* panel(QWidget* parent){auto* p=new QFrame(parent);p->setProperty("panel",true);return p;}
QLabel* cap(const QString& text,QWidget* parent){auto*l=new QLabel(text,parent);l->setProperty("caption",true);return l;}
QLabel* heading(const QString& text,int pt,QWidget* parent){auto*l=new QLabel(text,parent);QFont f=l->font();f.setPointSize(pt);f.setBold(true);l->setFont(f);return l;}
}

RegistrarPage::RegistrarPage(QWidget* parent):QWidget(parent)
{
    setObjectName(QStringLiteral("registrarPage"));
    setStyleSheet(QStringLiteral(
        "#registrarPage{background:#08131d;color:#eaf4fb;font-family:'Segoe UI';}"
        "QFrame[panel='true']{background:#102333;border:1px solid #264257;border-radius:8px;}"
        "QLabel[caption='true']{color:#8ea6b7;font-size:11px;font-weight:700;}"
        "QPushButton{background:#132a3d;color:#eaf4fb;border:1px solid #264257;border-radius:7px;padding:8px 13px;}"
        "QPushButton:hover{border-color:#58a5ff;}QPushButton#primary{background:#2e7de9;border-color:#58a5ff;font-weight:700;}"
        "QLineEdit,QComboBox{background:#0e1e2c;color:#eaf4fb;border:1px solid #264257;border-radius:6px;padding:8px;}"
        "QTableWidget{background:#0e1e2c;color:#eaf4fb;border:1px solid #1a3346;gridline-color:#1a3346;selection-background-color:#123b58;}"
        "QHeaderView::section{background:#0c1b28;color:#8ea6b7;border:0;border-bottom:1px solid #1a3346;padding:7px;font-weight:700;}"));

    auto* root=new QVBoxLayout(this);root->setContentsMargins(42,30,42,30);root->setSpacing(14);
    auto* top=new QHBoxLayout;auto* back=new QPushButton(QStringLiteral("← КТМА"),this);auto* title=heading(QStringLiteral("Администрирование УБСИ"),24,this);auto* production=new QPushButton(QStringLiteral("Выбрать проверку"),this);production->setObjectName(QStringLiteral("primary"));auto* history=new QPushButton(QStringLiteral("История"),this);top->addWidget(back);top->addWidget(title);top->addStretch();top->addWidget(history);top->addWidget(production);root->addLayout(top);

    auto* body=new QHBoxLayout;body->setSpacing(18);
    auto* master=panel(this);master->setMinimumWidth(310);master->setMaximumWidth(420);auto* ml=new QVBoxLayout(master);ml->setContentsMargins(16,16,16,16);ml->setSpacing(10);ml->addWidget(cap(QStringLiteral("УБСИ"),master));searchEdit_=new QLineEdit(master);searchEdit_->setPlaceholderText(QStringLiteral("Поиск по SN"));ml->addWidget(searchEdit_);productsTable_=new QTableWidget(master);productsTable_->setColumnCount(1);productsTable_->horizontalHeader()->hide();productsTable_->verticalHeader()->hide();productsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);productsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);productsTable_->setSelectionMode(QAbstractItemView::SingleSelection);productsTable_->setShowGrid(false);productsTable_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);productsTable_->verticalHeader()->setDefaultSectionSize(42);ml->addWidget(productsTable_,1);auto* createRow=new QHBoxLayout;serialEdit_=new QLineEdit(master);serialEdit_->setPlaceholderText(QStringLiteral("Новый SN УБСИ"));auto* create=new QPushButton(QStringLiteral("+"),master);create->setFixedWidth(42);createRow->addWidget(serialEdit_,1);createRow->addWidget(create);ml->addLayout(createRow);body->addWidget(master,1);

    auto* detail=panel(this);auto* dl=new QVBoxLayout(detail);dl->setContentsMargins(18,18,18,18);dl->setSpacing(12);compositionLabel_=heading(QStringLiteral("Выберите УБСИ"),20,detail);dl->addWidget(compositionLabel_);
    auto* stageRow=new QHBoxLayout;stageRow->addWidget(cap(QStringLiteral("ПРОИЗВОДСТВЕННЫЙ ЭТАП"),detail));stageCombo_=new QComboBox(detail);for(const auto&s:{QStringLiteral("Primary"),QStringLiteral("ClimateNormal"),QStringLiteral("ClimateMinus"),QStringLiteral("ClimatePlus"),QStringLiteral("PottingClimateNormal"),QStringLiteral("PottingClimatePlus"),QStringLiteral("PottingClimateMinus")})stageCombo_->addItem(stageText(s),s);stageRow->addWidget(stageCombo_,1);dl->addLayout(stageRow);
    dl->addWidget(cap(QStringLiteral("СОСТАВ"),detail));compositionTable_=new QTableWidget(detail);compositionTable_->setColumnCount(4);compositionTable_->setHorizontalHeaderLabels({QStringLiteral("Ячейка"),QStringLiteral("SN"),QStringLiteral("Состояние"),QStringLiteral("Причина снятия")});compositionTable_->verticalHeader()->hide();compositionTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);compositionTable_->setSelectionBehavior(QAbstractItemView::SelectRows);compositionTable_->setSelectionMode(QAbstractItemView::SingleSelection);compositionTable_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);compositionTable_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);compositionTable_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);compositionTable_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);dl->addWidget(compositionTable_,1);

    auto* addPanel=panel(detail);auto* ag=new QGridLayout(addPanel);ag->setContentsMargins(12,12,12,12);ag->addWidget(cap(QStringLiteral("ДОБАВИТЬ ЯЧЕЙКУ"),addPanel),0,0,1,3);componentTypeCombo_=new QComboBox(addPanel);componentTypeCombo_->addItem(QStringLiteral("ЯЛК-96"),QStringLiteral("YALK-96"));componentTypeCombo_->addItem(QStringLiteral("ЯТП"),QStringLiteral("YTP"));componentTypeCombo_->addItem(QStringLiteral("ЯВП-8"),QStringLiteral("YVP"));componentTypeCombo_->addItem(QStringLiteral("ЯП-П"),QStringLiteral("YP-P"));componentSerialEdit_=new QLineEdit(addPanel);componentSerialEdit_->setPlaceholderText(QStringLiteral("SN ячейки"));auto* addComponentButton=new QPushButton(QStringLiteral("Добавить"),addPanel);ag->addWidget(componentTypeCombo_,1,0);ag->addWidget(componentSerialEdit_,1,1);ag->addWidget(addComponentButton,1,2);dl->addWidget(addPanel);

    auto* replacePanel=panel(detail);auto* rg=new QGridLayout(replacePanel);rg->setContentsMargins(12,12,12,12);rg->addWidget(cap(QStringLiteral("ЗАМЕНИТЬ ВЫБРАННУЮ АКТИВНУЮ ЯЧЕЙКУ"),replacePanel),0,0,1,3);replacementSerialEdit_=new QLineEdit(replacePanel);replacementSerialEdit_->setPlaceholderText(QStringLiteral("Новый SN"));replacementReasonEdit_=new QLineEdit(replacePanel);replacementReasonEdit_->setPlaceholderText(QStringLiteral("Причина замены"));auto* replace=new QPushButton(QStringLiteral("Заменить"),replacePanel);rg->addWidget(replacementSerialEdit_,1,0);rg->addWidget(replacementReasonEdit_,1,1);rg->addWidget(replace,1,2);dl->addWidget(replacePanel);
    statusLabel_=new QLabel(detail);statusLabel_->setProperty("muted",true);statusLabel_->setWordWrap(true);dl->addWidget(statusLabel_);body->addWidget(detail,3);root->addLayout(body,1);

    connect(back,&QPushButton::clicked,this,&RegistrarPage::homeRequested);connect(production,&QPushButton::clicked,this,[this]{if(!selectedProductionProduct()){statusLabel_->setText(QStringLiteral("Выберите изделие и этап."));return;}emit productionRequested();});connect(history,&QPushButton::clicked,this,&RegistrarPage::showStageHistory);connect(searchEdit_,&QLineEdit::textChanged,this,&RegistrarPage::refreshProducts);connect(create,&QPushButton::clicked,this,&RegistrarPage::createProduct);connect(serialEdit_,&QLineEdit::returnPressed,this,&RegistrarPage::createProduct);connect(productsTable_,&QTableWidget::itemSelectionChanged,this,&RegistrarPage::refreshComposition);connect(addComponentButton,&QPushButton::clicked,this,&RegistrarPage::addComponent);connect(componentSerialEdit_,&QLineEdit::returnPressed,this,&RegistrarPage::addComponent);connect(replace,&QPushButton::clicked,this,&RegistrarPage::replaceComponent);
}

void RegistrarPage::setRegistrar(ktma::registrar::Registrar* registrar){registrar_=registrar;refreshProducts();}
QString RegistrarPage::selectedProductId()const{int r=productsTable_?productsTable_->currentRow():-1;if(r<0)return{};auto* item=productsTable_->item(r,0);return item?item->data(Qt::UserRole).toString():QString();}
std::optional<RegistrarPage::ProductionSelection> RegistrarPage::selectedProductionSelection()const
{
    const QString productId=selectedProductId();int row=compositionTable_?compositionTable_->currentRow():-1;int productRow=productsTable_?productsTable_->currentRow():-1;auto* typeItem=row<0?nullptr:compositionTable_->item(row,0);auto* productItem=productRow<0?nullptr:productsTable_->item(productRow,0);if(productId.isEmpty()||!typeItem||!productItem||!typeItem->data(Qt::UserRole+2).toBool())return std::nullopt;try{return ProductionSelection{productId,productItem->text(),typeItem->data(Qt::UserRole).toString(),typeItem->data(Qt::UserRole+1).toString(),compositionTable_->item(row,1)->text(),ktma::registrar::stageFromString(stageCombo_->currentData().toString().toStdString())};}catch(...){return std::nullopt;}
}
void RegistrarPage::refreshProducts()
{
    const QString selected=selectedProductId();productsTable_->setRowCount(0);compositionTable_->setRowCount(0);compositionLabel_->setText(QStringLiteral("Выберите УБСИ"));if(!registrar_){statusLabel_->setText(QStringLiteral("Регистратор недоступен."));return;}try{const QString query=searchEdit_->text().trimmed();int row=0;for(const auto&product:registrar_->listProducts()){QString serial=QString::fromStdString(product.serialNumber);if(!query.isEmpty()&&!serial.contains(query,Qt::CaseInsensitive))continue;productsTable_->insertRow(row);auto* item=new QTableWidgetItem(serial);item->setData(Qt::UserRole,QString::fromStdString(product.id));item->setToolTip(QStringLiteral("%1 · %2").arg(QString::fromStdString(product.productType),verdictText(registrar_->productVerdict(product.id))));productsTable_->setItem(row,0,item);if(QString::fromStdString(product.id)==selected)productsTable_->selectRow(row);++row;}statusLabel_->setText(row?QStringLiteral("Изделий: %1").arg(row):QStringLiteral("Изделия не найдены."));}catch(const std::exception&e){statusLabel_->setText(QStringLiteral("Не удалось прочитать registrar.db"));QMessageBox::warning(this,QStringLiteral("Регистратор"),QString::fromUtf8(e.what()));}}
void RegistrarPage::refreshComposition()
{
    compositionTable_->setRowCount(0);const QString productId=selectedProductId();if(!registrar_||productId.isEmpty()){compositionLabel_->setText(QStringLiteral("Выберите УБСИ"));return;}try{const auto report=registrar_->productReport(productId.toStdString());compositionLabel_->setText(QStringLiteral("УБСИ %1").arg(QString::fromStdString(report.product.serialNumber)));int row=0;for(const auto&c:report.components){compositionTable_->insertRow(row);auto* type=new QTableWidgetItem(componentTypeText(QString::fromStdString(c.componentType)));type->setData(Qt::UserRole,QString::fromStdString(c.componentId));type->setData(Qt::UserRole+1,QString::fromStdString(c.componentType));type->setData(Qt::UserRole+2,c.active);compositionTable_->setItem(row,0,type);compositionTable_->setItem(row,1,new QTableWidgetItem(QString::fromStdString(c.serialNumber)));compositionTable_->setItem(row,2,new QTableWidgetItem(c.active?QStringLiteral("УСТАНОВЛЕНА"):QStringLiteral("СНЯТА")));compositionTable_->setItem(row,3,new QTableWidgetItem(QString::fromStdString(c.removalReason)));++row;}}catch(const std::exception&e){QMessageBox::warning(this,QStringLiteral("Состав"),QString::fromUtf8(e.what()));}}
void RegistrarPage::createProduct()
{
    if(!registrar_)return;const QString serial=serialEdit_->text().trimmed();if(serial.isEmpty()){statusLabel_->setText(QStringLiteral("Введите SN УБСИ."));return;}try{if(registrar_->findProductBySerial(serial.toStdString())){statusLabel_->setText(QStringLiteral("УБСИ %1 уже зарегистрировано.").arg(serial));return;}registrar_->createProduct("UBSI",serial.toStdString());serialEdit_->clear();searchEdit_->setText(serial);refreshProducts();for(int r=0;r<productsTable_->rowCount();++r)if(productsTable_->item(r,0)->text()==serial){productsTable_->selectRow(r);break;}refreshComposition();statusLabel_->setText(QStringLiteral("УБСИ %1 зарегистрировано. Заполните четыре слота состава справа.").arg(serial));}catch(const std::exception&e){QMessageBox::warning(this,QStringLiteral("Регистрация"),QString::fromUtf8(e.what()));}}
void RegistrarPage::addComponent()
{
    if(!registrar_)return;const QString productId=selectedProductId(),serial=componentSerialEdit_->text().trimmed(),type=componentTypeCombo_->currentData().toString();if(productId.isEmpty()){statusLabel_->setText(QStringLiteral("Сначала выберите УБСИ."));return;}if(serial.isEmpty()){statusLabel_->setText(QStringLiteral("Введите SN ячейки."));return;}try{for(const auto&c:registrar_->listInstalledComponents(productId.toStdString()))if(c.active&&QString::fromStdString(c.componentType)==type){statusLabel_->setText(QStringLiteral("Активная ячейка этого типа уже установлена."));return;}const auto id=registrar_->createComponent(type.toStdString(),serial.toStdString());registrar_->installComponent(productId.toStdString(),id);componentSerialEdit_->clear();refreshComposition();refreshProducts();statusLabel_->setText(QStringLiteral("%1 добавлена.").arg(componentTypeText(type)));}catch(const std::exception&e){QMessageBox::warning(this,QStringLiteral("Добавление ячейки"),QString::fromUtf8(e.what()));}}
void RegistrarPage::replaceComponent()
{
    if(!registrar_)return;const QString productId=selectedProductId();int row=compositionTable_->currentRow();auto* typeItem=row<0?nullptr:compositionTable_->item(row,0);const QString serial=replacementSerialEdit_->text().trimmed(),reason=replacementReasonEdit_->text().trimmed();if(productId.isEmpty()||!typeItem){statusLabel_->setText(QStringLiteral("Выберите активную ячейку."));return;}if(!typeItem->data(Qt::UserRole+2).toBool()){statusLabel_->setText(QStringLiteral("Заменять можно только активную ячейку."));return;}if(serial.isEmpty()||reason.isEmpty()){statusLabel_->setText(QStringLiteral("Введите новый SN и причину."));return;}try{const QString componentId=typeItem->data(Qt::UserRole).toString(),type=typeItem->data(Qt::UserRole+1).toString();registrar_->replaceComponent(productId.toStdString(),componentId.toStdString(),type.toStdString(),serial.toStdString(),reason.toStdString());replacementSerialEdit_->clear();replacementReasonEdit_->clear();refreshComposition();refreshProducts();statusLabel_->setText(QStringLiteral("Ячейка %1 заменена; предыдущая запись сохранена в истории.").arg(componentTypeText(type)));}catch(const std::exception&e){QMessageBox::warning(this,QStringLiteral("Замена"),QString::fromUtf8(e.what()));}}
void RegistrarPage::showStageHistory()
{
    if(!registrar_)return;const QString productId=selectedProductId();if(productId.isEmpty()){statusLabel_->setText(QStringLiteral("Выберите УБСИ."));return;}try{const auto report=registrar_->productReport(productId.toStdString());QDialog dialog(this);dialog.setWindowTitle(QStringLiteral("История · УБСИ %1").arg(QString::fromStdString(report.product.serialNumber)));dialog.resize(900,560);auto* l=new QVBoxLayout(&dialog);auto* table=new QTableWidget(&dialog);table->setColumnCount(5);table->setHorizontalHeaderLabels({QStringLiteral("Ячейка"),QStringLiteral("Этап"),QStringLiteral("Итог"),QStringLiteral("Run ID"),QStringLiteral("Комментарий")});table->verticalHeader()->hide();table->setEditTriggers(QAbstractItemView::NoEditTriggers);int row=0;for(const auto&a:report.stageAttempts){table->insertRow(row);table->setItem(row,0,new QTableWidgetItem(QString::fromStdString(a.componentId)));table->setItem(row,1,new QTableWidgetItem(QString::fromUtf8(ktma::registrar::toString(a.stage))));table->setItem(row,2,new QTableWidgetItem(QString::fromUtf8(ktma::registrar::toString(a.verdict))));table->setItem(row,3,new QTableWidgetItem(QString::fromStdString(a.runId)));table->setItem(row,4,new QTableWidgetItem(QString::fromStdString(a.comment)));++row;}table->horizontalHeader()->setStretchLastSection(true);l->addWidget(table);auto* close=new QPushButton(QStringLiteral("Закрыть"),&dialog);connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);l->addWidget(close,0,Qt::AlignRight);dialog.exec();}catch(const std::exception&e){QMessageBox::warning(this,QStringLiteral("История"),QString::fromUtf8(e.what()));}}
