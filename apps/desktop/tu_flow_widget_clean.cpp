#include "tu_flow_widget.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QFrame* panel(QWidget* parent)
{
    auto* p=new QFrame(parent);p->setProperty("panel",true);return p;
}
QLabel* title(const QString& text,int point,QWidget* parent)
{
    auto* l=new QLabel(text,parent);QFont f=l->font();f.setPointSize(point);f.setBold(true);l->setFont(f);return l;
}
QLabel* muted(const QString& text,QWidget* parent)
{
    auto* l=new QLabel(text,parent);l->setProperty("muted",true);l->setWordWrap(true);return l;
}
bool validOperatorName(const QString& value)
{
    static const QRegularExpression pattern(QStringLiteral("^[А-ЯЁ][а-яё-]+\\s[А-ЯЁ]\\.[А-ЯЁ]\\.$"));
    return pattern.match(value.trimmed()).hasMatch();
}
}

TuFlowWidget::TuFlowWidget(QWidget* parent):QWidget(parent)
{
    setObjectName(QStringLiteral("tuFlowWidget"));
    setStyleSheet(QStringLiteral(
        "#tuFlowWidget{background:#08131d;color:#eaf4fb;font-family:'Segoe UI';}"
        "QFrame[panel='true']{background:#102333;border:1px solid #264257;border-radius:8px;}"
        "QLabel[muted='true']{color:#8ea6b7;}"
        "QPushButton{background:#132a3d;color:#eaf4fb;border:1px solid #264257;border-radius:7px;padding:9px 14px;}"
        "QPushButton:hover{border-color:#58a5ff;}"
        "QPushButton#primary{background:#2e7de9;border-color:#58a5ff;font-weight:700;}"
        "QComboBox{background:#0e1e2c;color:#eaf4fb;border:1px solid #264257;border-radius:6px;padding:8px;}"));
    auto* root=new QVBoxLayout(this);root->setContentsMargins(0,0,0,0);pages_=new QStackedWidget(this);root->addWidget(pages_);

    selectionPage_=new QWidget(pages_);auto* selection=new QVBoxLayout(selectionPage_);selection->setContentsMargins(80,55,80,55);selection->setSpacing(18);
    auto* nav=new QHBoxLayout;auto* home=new QPushButton(QStringLiteral("← КТМА"),selectionPage_);home->setObjectName(QStringLiteral("tuHomeButton"));nav->addWidget(home);nav->addStretch();selection->addLayout(nav);selection->addStretch();
    auto* box=panel(selectionPage_);box->setMaximumWidth(760);auto* bl=new QVBoxLayout(box);bl->setContentsMargins(28,26,28,26);bl->setSpacing(12);bl->addWidget(title(QStringLiteral("ПРОВЕРКА УБСИ ПО ТУ"),24,box));bl->addWidget(muted(QStringLiteral("Выберите оператора и зарегистрированное изделие. Обращение к оборудованию начинается только после явной проверки готовности."),box));
    bl->addSpacing(8);bl->addWidget(new QLabel(QStringLiteral("Оператор"),box));auto* opRow=new QHBoxLayout;operator_=new QComboBox(box);operator_->setObjectName(QStringLiteral("tuOperator"));operator_->addItem(QStringLiteral("Выберите оператора"),QString());auto* addOperator=new QPushButton(QStringLiteral("+"),box);addOperator->setObjectName(QStringLiteral("tuAddOperator"));addOperator->setFixedWidth(42);opRow->addWidget(operator_,1);opRow->addWidget(addOperator);bl->addLayout(opRow);
    bl->addWidget(new QLabel(QStringLiteral("Зарегистрированное УБСИ"),box));registered_=new QComboBox(box);registered_->setObjectName(QStringLiteral("tuRegisteredProducts"));registered_->addItem(QStringLiteral("Выберите УБСИ"),QString());bl->addWidget(registered_);
    check_=new QPushButton(QStringLiteral("Проверить готовность"),box);check_->setObjectName(QStringLiteral("primary"));check_->setMinimumHeight(46);bl->addWidget(check_);scenarioState_=muted(QString(),box);scenarioState_->setObjectName(QStringLiteral("tuScenarioState"));bl->addWidget(scenarioState_);
    auto* boxRow=new QHBoxLayout;boxRow->addStretch();boxRow->addWidget(box,1);boxRow->addStretch();selection->addLayout(boxRow);selection->addStretch(2);pages_->addWidget(selectionPage_);

    readinessPage_=new QWidget(pages_);auto* readiness=new QVBoxLayout(readinessPage_);readiness->setContentsMargins(90,60,90,60);readiness->setSpacing(18);auto* rn=new QHBoxLayout;auto* readyHome=new QPushButton(QStringLiteral("← КТМА"),readinessPage_);rn->addWidget(readyHome);rn->addStretch();readiness->addLayout(rn);readiness->addStretch();auto* readyCard=panel(readinessPage_);readyCard->setMaximumWidth(820);auto* rcl=new QVBoxLayout(readyCard);rcl->setContentsMargins(30,28,30,28);rcl->setSpacing(14);serialTitle_=title(QStringLiteral("УБСИ"),17,readyCard);serialTitle_->setObjectName(QStringLiteral("tuReadySerial"));rcl->addWidget(serialTitle_);readinessState_=title(QStringLiteral("ПРОВЕРКА СТЕНДА…"),27,readyCard);readinessState_->setObjectName(QStringLiteral("tuReadyState"));rcl->addWidget(readinessState_);failureDetail_=muted(QString(),readyCard);failureDetail_->setObjectName(QStringLiteral("tuReadyDetail"));failureDetail_->hide();rcl->addWidget(failureDetail_);auto* actions=new QHBoxLayout;back_=new QPushButton(QStringLiteral("Выбрать другое УБСИ"),readyCard);retry_=new QPushButton(QStringLiteral("Повторить проверку"),readyCard);start_=new QPushButton(QStringLiteral("НАЧАТЬ ПРОВЕРКУ"),readyCard);start_->setObjectName(QStringLiteral("primary"));actions->addWidget(back_);actions->addStretch();actions->addWidget(retry_);actions->addWidget(start_);rcl->addLayout(actions);auto* rr=new QHBoxLayout;rr->addStretch();rr->addWidget(readyCard,1);rr->addStretch();readiness->addLayout(rr);readiness->addStretch();pages_->addWidget(readinessPage_);

    connect(home,&QPushButton::clicked,this,&TuFlowWidget::homeRequested);connect(readyHome,&QPushButton::clicked,this,&TuFlowWidget::homeRequested);
    connect(operator_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){updateSelectionAvailability();});connect(registered_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){updateSelectionAvailability();});
    connect(addOperator,&QPushButton::clicked,this,[this]{bool ok=false;const QString value=QInputDialog::getText(this,QStringLiteral("Новый оператор"),QStringLiteral("ФИО в формате «Толмачёв А.Е.»"),QLineEdit::Normal,QString(),&ok).trimmed();if(!ok||value.isEmpty())return;if(!validOperatorName(value)){QMessageBox::warning(this,QStringLiteral("Оператор"),QStringLiteral("Используйте формат: Фамилия И.О."));return;}int i=operator_->findData(value);if(i<0){operator_->addItem(value,value);i=operator_->count()-1;}operator_->setCurrentIndex(i);});
    connect(check_,&QPushButton::clicked,this,[this]{if(!check_->isEnabled())return;const QString serial=registered_->currentData().toString().trimmed();const QString op=operator_->currentData().toString().trimmed();if(!serial.isEmpty()&&!op.isEmpty())emit readinessRequested(serial,op);});
    connect(start_,&QPushButton::clicked,this,[this]{if(!activeSerial_.isEmpty()&&!activeOperator_.isEmpty())emit startRequested(activeSerial_,activeOperator_);});
    connect(retry_,&QPushButton::clicked,this,[this]{if(activeSerial_.isEmpty()||activeOperator_.isEmpty())return;equipmentState_.clear();equipmentDetail_.clear();readinessState_->setText(QStringLiteral("ПРОВЕРКА СТЕНДА…"));readinessState_->setStyleSheet(QStringLiteral("color:#58a5ff;"));failureDetail_->hide();retry_->hide();back_->hide();start_->hide();emit retryRequested(activeSerial_,activeOperator_);});
    connect(back_,&QPushButton::clicked,this,&TuFlowWidget::resetToSelection);
    resetToSelection();
}

void TuFlowWidget::setRegisteredSerials(const QStringList& serials){const QString selected=registered_->currentData().toString();QStringList unique=serials;unique.removeDuplicates();unique.sort(Qt::CaseInsensitive);registered_->blockSignals(true);registered_->clear();registered_->addItem(QStringLiteral("Выберите УБСИ"),QString());for(const auto&s:unique)registered_->addItem(s,s);int i=registered_->findData(selected);registered_->setCurrentIndex(i>=0?i:0);registered_->blockSignals(false);updateSelectionAvailability();}
void TuFlowWidget::setOperators(const QStringList& operators){const QString selected=operator_->currentData().toString();QStringList unique=operators;unique.removeAll(QString());unique.removeDuplicates();unique.sort(Qt::CaseInsensitive);operator_->blockSignals(true);operator_->clear();operator_->addItem(QStringLiteral("Выберите оператора"),QString());for(const auto&s:unique)operator_->addItem(s,s);int i=operator_->findData(selected);operator_->setCurrentIndex(i>=0?i:0);operator_->blockSignals(false);updateSelectionAvailability();}
void TuFlowWidget::setScenarioAvailable(bool available,const QString& detail){scenarioAvailable_=available;operator_->setEnabled(available);registered_->setEnabled(available);scenarioState_->setText(available?QString():detail.isEmpty()?QStringLiteral("Проверка по ТУ недоступна"):detail);scenarioState_->setStyleSheet(available?QString():QStringLiteral("color:#ef5a5a;font-weight:700;"));updateSelectionAvailability();}
void TuFlowWidget::beginStandCheck(const QString& serial,const QString& operatorName,const QStringList& requiredEquipment){activeSerial_=serial.trimmed();activeOperator_=operatorName.trimmed();if(activeSerial_.isEmpty()||activeOperator_.isEmpty())return;requiredEquipment_.clear();QSet<QString> seen;for(const auto&code:requiredEquipment){if(code==QStringLiteral("R4831")||code==QStringLiteral("SCHEME")||seen.contains(code))continue;seen.insert(code);requiredEquipment_<<code;}equipmentState_.clear();equipmentDetail_.clear();for(const auto&code:requiredEquipment_)equipmentState_[code]=-1;serialTitle_->setText(QStringLiteral("УБСИ SN %1 · %2").arg(activeSerial_,activeOperator_));readinessState_->setText(QStringLiteral("ПРОВЕРКА СТЕНДА…"));readinessState_->setStyleSheet(QStringLiteral("color:#58a5ff;"));failureDetail_->hide();start_->hide();retry_->hide();back_->hide();pages_->setCurrentWidget(readinessPage_);if(requiredEquipment_.isEmpty())showReady();}
void TuFlowWidget::setEquipmentChecking(const QString& code){if(!equipmentState_.contains(code))return;equipmentState_[code]=-1;equipmentDetail_.remove(code);updateReadiness();}
void TuFlowWidget::setEquipmentStatus(const QString& code,bool ready,const QString& detail){if(!equipmentState_.contains(code))return;equipmentState_[code]=ready?1:0;equipmentDetail_[code]=detail;updateReadiness();}
void TuFlowWidget::resetToSelection(){activeSerial_.clear();activeOperator_.clear();requiredEquipment_.clear();equipmentState_.clear();equipmentDetail_.clear();if(registered_->count()>0)registered_->setCurrentIndex(0);if(operator_->count()>0)operator_->setCurrentIndex(0);pages_->setCurrentWidget(selectionPage_);updateSelectionAvailability();}
QString TuFlowWidget::activeSerial()const{return activeSerial_;}
QString TuFlowWidget::activeOperator()const{return activeOperator_;}
void TuFlowWidget::updateSelectionAvailability(){check_->setEnabled(scenarioAvailable_&&!operator_->currentData().toString().trimmed().isEmpty()&&!registered_->currentData().toString().trimmed().isEmpty());}
void TuFlowWidget::updateReadiness(){bool pending=false;QStringList failures;for(const auto&code:requiredEquipment_){int state=equipmentState_.value(code,-1);if(state<0)pending=true;else if(state==0){const QString detail=equipmentDetail_.value(code).trimmed();failures<<(detail.isEmpty()?code:QStringLiteral("%1: %2").arg(code,detail));}}if(!failures.isEmpty()){showNotReady(failures.join(QLatin1Char('\n')));return;}if(!pending)showReady();}
void TuFlowWidget::showReady(){readinessState_->setText(QStringLiteral("СТЕНД ГОТОВ"));readinessState_->setStyleSheet(QStringLiteral("color:#35cf79;"));failureDetail_->hide();retry_->hide();back_->hide();start_->show();}
void TuFlowWidget::showNotReady(const QString& detail){readinessState_->setText(QStringLiteral("СТЕНД НЕ ГОТОВ"));readinessState_->setStyleSheet(QStringLiteral("color:#ef5a5a;"));failureDetail_->setText(detail);failureDetail_->show();start_->hide();retry_->show();back_->show();}
