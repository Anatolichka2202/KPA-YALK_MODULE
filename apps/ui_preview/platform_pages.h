#pragma once

// UI-only previews of application configuration and station administration.
// These widgets do not package applications, manage accounts or touch equipment.

#include <QWidget>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;

namespace miltech::ui {

class TuConfiguratorPage final : public QWidget {
    Q_OBJECT
public:
    explicit TuConfiguratorPage(QString deliveryId, QWidget* parent = nullptr);
    QPlainTextEdit* draftPreview() const noexcept { return draft_; }
    QPushButton* packageButton() const noexcept { return packageButton_; }
    QComboBox* methodologyChoice() const noexcept { return methodology_; }

private:
    void refreshPreview();

    QString deliveryId_;
    QLineEdit* packageId_ = nullptr;
    QLineEdit* title_ = nullptr;
    QLineEdit* version_ = nullptr;
    QComboBox* methodology_ = nullptr;
    QCheckBox* details_ = nullptr;
    QCheckBox* plot_ = nullptr;
    QCheckBox* events_ = nullptr;
    QPlainTextEdit* draft_ = nullptr;
    QLabel* validation_ = nullptr;
    QPushButton* packageButton_ = nullptr;
};

class StationAdministrationPage final : public QWidget {
    Q_OBJECT
public:
    explicit StationAdministrationPage(QString deliveryName,
                                       QStringList equipment,
                                       QWidget* parent = nullptr);
    QTabWidget* sections() const noexcept { return tabs_; }

private:
    QTabWidget* tabs_ = nullptr;
};

} // namespace miltech::ui
