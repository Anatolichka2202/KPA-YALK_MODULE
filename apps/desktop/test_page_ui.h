#pragma once
#include "test_page.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QAbstractItemView>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <tuple>

namespace {

constexpr int kStandMode = 0;
constexpr int kDemoMode = 1;

enum class ScopeKind { Full = 0, Yalk = 1, Ytp = 2, Yvp = 3 };
enum class TopStage { Preparation = 0, Power = 1, Yalk = 2, Ytp = 3, Yvp = 4, Finish = 5 };
enum class YalkPhase { Init = 0, Calibration = 1, Initial = 2, Analog = 3, Discrete = 4, Overload = 5, Reference = 6 };
enum class YtpPhase { Init = 0, Calibration = 1, Channels = 2 };

QString verdictText(orbita::stand::RunVerdict verdict)
{
    using V = orbita::stand::RunVerdict;
    switch (verdict) {
    case V::Ok: return QStringLiteral("НОРМА");
    case V::Fail: return QStringLiteral("НЕ НОРМА");
    case V::Incomplete: return QStringLiteral("НЕПОЛНАЯ");
    case V::Aborted: return QStringLiteral("ОСТАНОВЛЕНО");
    case V::Error: return QStringLiteral("ОШИБКА");
    case V::NotRun: return QStringLiteral("НЕ ВЫПОЛНЯЛОСЬ");
    }
    return QStringLiteral("ОШИБКА");
}

QColor verdictColor(orbita::stand::RunVerdict verdict)
{
    using V = orbita::stand::RunVerdict;
    if (verdict == V::Ok) return QColor("#70d79b");
    if (verdict == V::Incomplete) return QColor("#69aee6");
    if (verdict == V::Aborted) return QColor("#d7a95b");
    return QColor("#e1766d");
}

QString eventValue(const orbita::stand::RunEvent& event, const char* key)
{
    const auto found = event.data.find(key);
    return found == event.data.end() ? QString() : QString::fromStdString(found->second);
}

QVector<double> csvNumbers(const QString& text)
{
    QVector<double> values;
    for (const auto& token : text.split(',', Qt::SkipEmptyParts)) {
        bool ok = false;
        const double v = token.toDouble(&ok);
        if (ok && std::isfinite(v)) values.push_back(v);
    }
    return values;
}

QFrame* panel(bool accent = false)
{
    auto* frame = new QFrame;
    frame->setObjectName(accent ? QStringLiteral("accentPanel") : QStringLiteral("panel"));
    return frame;
}

QLabel* titleLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("pageTitle"));
    return label;
}

QLabel* subtitleLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("pageSubtitle"));
    label->setWordWrap(true);
    return label;
}

QLabel* sectionLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("sectionTitle"));
    return label;
}

QLabel* mutedLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("muted"));
    return label;
}

QFrame* metricCard(const QString& caption, QLabel*& value)
{
    auto* frame = panel();
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(2);
    layout->addWidget(mutedLabel(caption));
    value = new QLabel(QStringLiteral("—"));
    value->setObjectName(QStringLiteral("metricValue"));
    layout->addWidget(value);
    return frame;
}

QString elapsedText(qint64 milliseconds)
{
    const qint64 seconds = std::max<qint64>(0, milliseconds / 1000);
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QChar('0'))
        .arg((seconds / 60) % 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0'));
}

class TrendPlot final : public QWidget
{
public:
    explicit TrendPlot(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(105);
    }

    void configure(QString title, QString unit, QString firstName = {}, QString secondName = {})
    {
        title_ = std::move(title);
        unit_ = std::move(unit);
        firstName_ = std::move(firstName);
        secondName_ = std::move(secondName);
        update();
    }

    void clear()
    {
        first_.clear();
        second_.clear();
        update();
    }

    void append(double first, double second = std::numeric_limits<double>::quiet_NaN())
    {
        first_.push_back(first);
        if (std::isfinite(second)) second_.push_back(second);
        else if (!second_.isEmpty()) second_.push_back(std::numeric_limits<double>::quiet_NaN());
        while (first_.size() > 180) first_.removeFirst();
        while (second_.size() > 180) second_.removeFirst();
        update();
    }

    void setSamples(const QVector<double>& samples)
    {
        first_ = samples;
        second_.clear();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#0e1115"));

        QFont font = painter.font();
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor("#dce6ef"));
        painter.drawText(QRectF(9, 4, width() - 18, 20), Qt::AlignLeft, title_);

        font.setBold(false);
        font.setPointSize(std::max(8, font.pointSize() - 1));
        painter.setFont(font);
        if (!firstName_.isEmpty()) {
            painter.setPen(QColor("#70d79b"));
            painter.drawText(QRectF(width() - 260, 4, 120, 20), Qt::AlignRight, firstName_);
        }
        if (!secondName_.isEmpty()) {
            painter.setPen(QColor("#69aee6"));
            painter.drawText(QRectF(width() - 130, 4, 120, 20), Qt::AlignRight, secondName_);
        }

        const QRectF plot(42, 29, width() - 54, height() - 42);
        painter.setPen(QPen(QColor("#27313c"), 1));
        painter.drawRect(plot);
        for (int i = 1; i < 4; ++i) {
            const double y = plot.top() + plot.height() * i / 4.0;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }

        QVector<double> values;
        for (double v : first_) if (std::isfinite(v)) values.push_back(v);
        for (double v : second_) if (std::isfinite(v)) values.push_back(v);
        if (values.size() < 2) {
            painter.setPen(QColor("#667484"));
            painter.drawText(plot, Qt::AlignCenter, QStringLiteral("данные появятся во время измерения"));
            return;
        }

        const auto mm = std::minmax_element(values.begin(), values.end());
        double low = *mm.first;
        double high = *mm.second;
        double pad = (high - low) * 0.2;
        if (pad < 1e-6) pad = unit_ == QStringLiteral("Ом") ? 0.5 : 0.01;
        low -= pad;
        high += pad;
        if (!(high > low)) high = low + 1.0;

        painter.setPen(QColor("#7e8a98"));
        painter.drawText(QRectF(0, plot.top() - 7, 38, 14), Qt::AlignRight,
                         QString::number(high, 'f', unit_ == QStringLiteral("Ом") ? 2 : 3));
        painter.drawText(QRectF(0, plot.bottom() - 7, 38, 14), Qt::AlignRight,
                         QString::number(low, 'f', unit_ == QStringLiteral("Ом") ? 2 : 3));

        const auto draw = [&](const QVector<double>& series, const QColor& color) {
            const int count = static_cast<int>(series.size());
            if (count < 2) return;
            QPainterPath path;
            bool started = false;
            for (int i = 0; i < count; ++i) {
                if (!std::isfinite(series[i])) continue;
                const double x = plot.left() + plot.width() * i / std::max(1, count - 1);
                const double y = plot.bottom() - (series[i] - low) / (high - low) * plot.height();
                if (!started) { path.moveTo(x, y); started = true; }
                else path.lineTo(x, y);
            }
            painter.setPen(QPen(color, 1.6));
            painter.drawPath(path);
        };
        draw(first_, QColor("#70d79b"));
        draw(second_, QColor("#69aee6"));
    }

private:
    QString title_;
    QString unit_;
    QString firstName_;
    QString secondName_;
    QVector<double> first_;
    QVector<double> second_;
};

class StepPlot final : public QWidget
{
public:
    explicit StepPlot(QWidget* parent = nullptr) : QWidget(parent) { setMinimumHeight(105); }

    void configure(QString title, QString unit, QVector<double> steps)
    {
        title_ = std::move(title);
        unit_ = std::move(unit);
        steps_ = std::move(steps);
        update();
    }

    void setActiveValue(double value)
    {
        active_ = -1;
        double best = std::numeric_limits<double>::max();
        for (int i = 0; i < steps_.size(); ++i) {
            const double distance = std::abs(steps_[i] - value);
            if (distance < best) { best = distance; active_ = i; }
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#0e1115"));
        painter.setPen(QColor("#dce6ef"));
        QFont font = painter.font(); font.setBold(true); painter.setFont(font);
        painter.drawText(QRectF(9, 4, width() - 18, 20), Qt::AlignLeft, title_);
        if (steps_.isEmpty()) return;

        const QRectF plot(42, 31, width() - 54, height() - 47);
        painter.setPen(QPen(QColor("#27313c"), 1));
        painter.drawRect(plot);
        double minV = *std::min_element(steps_.begin(), steps_.end());
        double maxV = *std::max_element(steps_.begin(), steps_.end());
        if (!(maxV > minV)) { minV -= 1.0; maxV += 1.0; }
        const double stepWidth = plot.width() / std::max(1, static_cast<int>(steps_.size()));
        QPainterPath path;
        for (int i = 0; i < steps_.size(); ++i) {
            const double y = plot.bottom() - (steps_[i] - minV) / (maxV - minV) * (plot.height() * 0.8);
            const double x0 = plot.left() + i * stepWidth;
            const double x1 = plot.left() + (i + 1) * stepWidth;
            if (i == 0) path.moveTo(x0, y); else path.lineTo(x0, y);
            path.lineTo(x1, y);
            if (i == active_) painter.fillRect(QRectF(x0, plot.top(), stepWidth, plot.height()), QColor(215,169,91,26));
            painter.setPen(i == active_ ? QColor("#ffda83") : QColor("#7e8a98"));
            painter.drawText(QRectF(x0, plot.bottom() + 2, stepWidth, 16), Qt::AlignCenter,
                             QStringLiteral("%1 %2").arg(steps_[i], 0, 'f', 1).arg(unit_));
        }
        painter.setPen(QPen(QColor("#d7a95b"), 2));
        painter.drawPath(path);
    }

private:
    QString title_;
    QString unit_;
    QVector<double> steps_;
    int active_ = -1;
};

struct ChannelSample {
    QString key;
    QString point;
    double reference = 0.0;
    double measured = 0.0;
    bool signal = false;
    bool passed = true;
    QVector<double> samples;
};

class ChannelOverview final : public QWidget
{
public:
    explicit ChannelOverview(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("channelHistogram"));
        setMinimumHeight(290);
    }

    void configure(int count, QString unit)
    {
        count_ = count;
        unit_ = std::move(unit);
        clear();
    }

    void clear()
    {
        items_.clear();
        currentKey_.clear();
        currentPoint_.clear();
        update();
    }

    void add(ChannelSample sample)
    {
        bool replaced = false;
        for (auto& item : items_) {
            if (item.key == sample.key && item.point == sample.point) {
                item = sample;
                replaced = true;
                break;
            }
        }
        if (!replaced) items_.push_back(sample);
        currentKey_ = sample.key;
        currentPoint_ = sample.point;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#0e1115"));
        painter.setPen(QColor("#dce6ef"));
        QFont font = painter.font(); font.setBold(true); painter.setFont(font);
        painter.drawText(QRectF(10, 4, width() - 20, 20), Qt::AlignLeft,
            QStringLiteral("Все каналы · %1").arg(currentPoint_.isEmpty() ? QStringLiteral("ожидание") : currentPoint_));

        QVector<double> trace;
        double currentMeasured = 0.0;
        for (const auto& item : items_) {
            if (item.key == currentKey_ && item.point == currentPoint_) {
                trace = item.samples;
                currentMeasured = item.measured;
            }
        }
        if (trace.isEmpty() && !currentKey_.isEmpty()) trace.push_back(currentMeasured);
        const QRectF traceRect(50, 29, width() - 65, 52);
        painter.setPen(QPen(QColor("#27313c"), 1));
        painter.drawRect(traceRect);
        painter.setPen(QColor("#8b95a3"));
        painter.drawText(QRectF(52, 31, width()-70, 16), Qt::AlignLeft,
                         QStringLiteral("текущие колебания · канал %1").arg(currentKey_));
        if (trace.size() > 1) {
            auto mm = std::minmax_element(trace.begin(), trace.end());
            double low = *mm.first, high = *mm.second;
            const double pad = std::max(1e-4, (high-low)*0.2);
            low -= pad; high += pad;
            QPainterPath path;
            for (int i=0;i<trace.size();++i) {
                const double x=traceRect.left()+traceRect.width()*i/std::max(1,static_cast<int>(trace.size())-1);
                const double y=traceRect.bottom()-(trace[i]-low)/(high-low)*traceRect.height();
                if(i==0)path.moveTo(x,y);else path.lineTo(x,y);
            }
            painter.setPen(QPen(QColor("#70d79b"),1.6));
            painter.drawPath(path);
        }

        const QRectF area(50, 96, width() - 65, height() - 126);
        painter.setPen(QPen(QColor("#27313c"), 1));
        painter.drawRect(area);
        if (items_.isEmpty()) {
            painter.setPen(QColor("#667484"));
            painter.drawText(area, Qt::AlignCenter, QStringLiteral("поканальные данные появятся во время проверки"));
            return;
        }

        QVector<QString> keys;
        for (const auto& item : items_) if (item.point == currentPoint_ && !keys.contains(item.key)) keys.push_back(item.key);
        const int n = std::max(1, static_cast<int>(keys.size()));
        const int rows = count_ > 40 ? 4 : 2;
        const int perRow = std::max(1, (n + rows - 1) / rows);
        const double rowHeight = area.height() / rows;

        double extent = unit_ == QStringLiteral("Ом") ? 2.0 : 0.05;
        for (const auto& item : items_) if (item.point == currentPoint_)
            extent = std::max(extent, std::abs(item.measured-item.reference)*1.3);

        for (int row=0; row<rows; ++row) {
            const int begin=row*perRow;
            const int end=std::min(n,begin+perRow);
            if(begin>=end)break;
            const double dx=area.width()/std::max(1,end-begin);
            const double zero=area.top()+row*rowHeight+rowHeight/2.0;
            painter.setPen(QPen(QColor("#45515e"),1,Qt::DashLine));
            painter.drawLine(QPointF(area.left(),zero),QPointF(area.right(),zero));
            for(int i=begin;i<end;++i){
                const QString key=keys[i];
                const ChannelSample* sample=nullptr;
                for(const auto& item:items_) if(item.key==key&&item.point==currentPoint_){sample=&item;break;}
                if(!sample)continue;
                const double x=area.left()+(i-begin)*dx;
                const double e=sample->measured-sample->reference;
                const double h=std::clamp(std::abs(e)/extent,0.02,1.0)*(rowHeight*0.36);
                QRectF bar(x+dx*0.25,e>=0?zero-h:zero,dx*0.5,h);
                painter.fillRect(bar,sample->passed?QColor("#4f7fa7"):QColor("#e1766d"));
                if(key==currentKey_) painter.fillRect(QRectF(x,zero-rowHeight*0.45,dx,rowHeight*0.9),QColor(94,147,184,22));
                painter.setPen(QColor("#9aafbf"));
                painter.drawText(QRectF(x,zero+rowHeight*0.34,dx,15),Qt::AlignCenter,key);
            }
        }
    }

private:
    int count_ = 80;
    QString unit_ = QStringLiteral("В");
    QVector<ChannelSample> items_;
    QString currentKey_;
    QString currentPoint_;
};

class StateGrid final : public QWidget
{
public:
    explicit StateGrid(QWidget* parent=nullptr):QWidget(parent){setMinimumHeight(280);}
    void clear(){states_.clear();current_.clear();expected_=0;update();}
    void setCurrent(QString key,int signal,int expected){states_[key]=signal;current_=std::move(key);expected_=expected;update();}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#0e1115"));
        const int cols=10;const double gap=5,m=8;const int rows=8;
        const double cw=(width()-2*m-gap*(cols-1))/cols;const double ch=(height()-2*m-gap*(rows-1))/rows;
        for(int i=1;i<=80;++i){const int r=(i-1)/cols,c=(i-1)%cols;QRectF cell(m+c*(cw+gap),m+r*(ch+gap),cw,ch);
            const QString key=QString::number(i);const bool known=states_.contains(key);const int state=states_.value(key,0);
            QColor fill=known?(state==expected_?QColor("#14251c"):QColor("#2a1718")):QColor("#111820");
            QColor border=known?(state==expected_?QColor("#315c43"):QColor("#6b3434")):QColor("#27313c");
            if(key==current_){fill=QColor("#132033");border=QColor("#5e93b8");}
            p.setPen(QPen(border,key==current_?2:1));p.setBrush(fill);p.drawRoundedRect(cell,4,4);p.setPen(QColor("#dce6ef"));p.drawText(cell.adjusted(5,2,-5,-2),Qt::AlignTop|Qt::AlignLeft,key);
            if(known){QRectF bit(cell.right()-24,cell.bottom()-23,18,18);p.setBrush(state?QColor("#5e93b8"):QColor("#27313c"));p.setPen(Qt::NoPen);p.drawRoundedRect(bit,3,3);p.setPen(QColor("#f1f5f9"));p.drawText(bit,Qt::AlignCenter,QString::number(state));}
        }
    }
private:QHash<QString,int> states_;QString current_;int expected_=0;
};

QString pageStyle()
{
    return QStringLiteral(R"QSS(
QWidget#operatorTestPage { background:#14171c; color:#e6eaf0; }
QFrame#panel { background:#10151b; border:1px solid #27313c; border-radius:8px; }
QFrame#accentPanel { background:#132033; border:1px solid #27466c; border-radius:8px; }
QLabel#pageTitle { color:#f1f5f9; font-size:27px; font-weight:700; }
QLabel#pageSubtitle { color:#8b95a3; font-size:13px; }
QLabel#sectionTitle { color:#e6eaf0; font-size:15px; font-weight:700; }
QLabel#muted { color:#7e8a98; font-size:11px; }
QLabel#metricValue { color:#f1f5f9; font-size:20px; font-weight:700; }
QPushButton { background:#1b2129; color:#c2ccd8; border:1px solid #2c333d; padding:7px 13px; border-radius:6px; }
QPushButton:hover { background:#2a313b; border-color:#5e93b8; }
QPushButton:disabled { color:#5b6573; background:#1c2128; border-color:#232a33; }
QPushButton#primary { background:#214e78; border:1px solid #5e93b8; color:#eef7ff; font-weight:700; padding:10px 18px; }
QPushButton#scopeCard, QPushButton#subCheckCard { background:#12161c; color:#dfe6ee; border:1px solid #2c333d; border-radius:9px; text-align:left; padding:13px; font-weight:650; }
QPushButton#scopeCard { min-height:74px; font-size:14px; }
QPushButton#subCheckCard { min-height:64px; font-size:12px; }
QPushButton#scopeCard:hover, QPushButton#subCheckCard:hover { background:#1b2129; border:2px solid #5e93b8; }
QPushButton#scopeCard:checked, QPushButton#subCheckCard:checked { background:#18212c; border:2px solid #5e93b8; color:#9ac7ff; }
QLineEdit, QComboBox { background:#1b2129; color:#e6eaf0; border:1px solid #2c333d; border-radius:5px; padding:7px 9px; }
QLineEdit:focus, QComboBox:focus { border-color:#5e93b8; }
QTableWidget { background:#0e1115; alternate-background-color:#1c2128; border:1px solid #232a33; gridline-color:#1a1f26; }
QHeaderView::section { background:#0e1115; color:#7e8a98; padding:7px; border:none; border-right:1px solid #232a33; font-weight:600; }
QProgressBar { background:#1c222a; border:1px solid #232a33; border-radius:4px; min-height:18px; text-align:center; }
QProgressBar::chunk { background:#5e93b8; border-radius:3px; }
QLabel#stage { color:#8d9aa8; padding:7px 9px; border:1px solid transparent; border-radius:5px; }
QLabel#stageActive { background:#132033; border:1px solid #27466c; border-left:4px solid #5e93b8; color:#9ac7ff; padding:7px 9px; font-weight:700; border-radius:5px; }
QLabel#stageDone { color:#70d79b; padding:7px 9px; border:1px solid transparent; }
QToolTip { background:#1b2129; border:1px solid #3a424d; color:#e6eaf0; padding:7px 10px; }
)QSS");
}

} // namespace
