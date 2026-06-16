#include "readout_widget.h"
#include "channel_status.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

ReadoutWidget::ReadoutWidget(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);

    // Верхняя строка: номер и имя
    QHBoxLayout* topLayout = new QHBoxLayout;
    m_numLabel = new QLabel;
    m_numLabel->setStyleSheet("font-family: 'IBM Plex Mono'; color: #5b6573; font-size: 12px;");
    m_nameLabel = new QLabel;
    m_nameLabel->setStyleSheet("font-weight: 600; font-size: 16px;");
    topLayout->addWidget(m_numLabel);
    topLayout->addWidget(m_nameLabel);
    topLayout->addStretch();
    mainLayout->addLayout(topLayout);

    // Категория
    m_catLabel = new QLabel;
    m_catLabel->setStyleSheet("color: #7e8a98; font-size: 12px;");
    mainLayout->addWidget(m_catLabel);

    // Значение
    m_valueLabel = new QLabel("---");
    m_valueLabel->setStyleSheet("font-family: 'IBM Plex Mono'; font-size: 40px; font-weight: 600;");
    mainLayout->addWidget(m_valueLabel);

    // Статус
    m_statusLabel = new QLabel;
    m_statusLabel->setStyleSheet("padding: 4px 10px; border-radius: 6px;");
    mainLayout->addWidget(m_statusLabel);

    // Шкала допуска
    m_toleranceBar = new QProgressBar;
    m_toleranceBar->setRange(0, 1023);
    m_toleranceBar->setTextVisible(false);
    m_toleranceBar->setStyleSheet(
        "QProgressBar { background-color: #1c222a; border-radius: 3px; height: 8px; } "
        "QProgressBar::chunk { background-color: #5e93b8; border-radius: 3px; }"
        );
    mainLayout->addWidget(m_toleranceBar);

    // Подпись допуска
    m_rangeLabel = new QLabel;
    m_rangeLabel->setStyleSheet("color: #7e8a98; font-family: 'IBM Plex Mono'; font-size: 12px;");
    mainLayout->addWidget(m_rangeLabel);

    mainLayout->addStretch();
}

void ReadoutWidget::setMetadataService(MetadataService* db)
{
    m_db = db;
}

void ReadoutWidget::setChannel(const orbita::ChannelSpec& spec)
{
    m_spec = spec;
    m_numLabel->setText(QString("КАН %1").arg(/* номер? можно по индексу, но его нет в spec */ "?"));
    m_nameLabel->setText(QString::fromStdString(spec.name.empty() ? spec.address : spec.name));
    if (m_db) {
        auto info = m_db->lookup(QString::fromStdString(spec.address));
        if (info) {
            m_catLabel->setText(info->category);
            // TODO: получить допуски из БД (если есть) или из конфига
        } else {
            m_catLabel->setText("");
        }
    }
    updateUI();
}

void ReadoutWidget::updateValue(double value)
{
    m_currentValue = value;
    updateUI();
}

void ReadoutWidget::updateUI()
{
    if (m_spec.address.empty()) return;

    m_valueLabel->setText(QString::number(m_currentValue, 'f', 0));
    m_toleranceBar->setValue((int)m_currentValue);

    chstatus::Tolerance tol = chstatus::forAddress(m_db, m_spec.address);
    chstatus::Level lvl = chstatus::evaluate(m_currentValue, tol);

    if (tol.set)
        m_rangeLabel->setText(QString("допуск: %1 – %2 – %3")
                                  .arg(tol.lo).arg(tol.nominal).arg(tol.hi));
    else
        m_rangeLabel->setText("допуск не задан");

    m_statusLabel->setText(chstatus::text(lvl));
    m_statusLabel->setStyleSheet(QString("padding: 4px 10px; border-radius: 6px; color: %1;")
                                     .arg(chstatus::textColor(lvl).name()));

    m_toleranceBar->setStyleSheet(
        QString("QProgressBar { background-color: #1c222a; border-radius: 3px; height: 8px; } "
                "QProgressBar::chunk { background-color: %1; border-radius: 3px; }")
            .arg(chstatus::barColor(lvl).name()));
}
