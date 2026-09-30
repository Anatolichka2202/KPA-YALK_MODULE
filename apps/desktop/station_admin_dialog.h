#pragma once

#include <QDialog>

namespace orbita::stand {
class ProjectDefinition;
class StationSession;
}

class StationAdminDialog final : public QDialog
{
    Q_OBJECT
public:
    StationAdminDialog(const orbita::stand::ProjectDefinition& project,
                       const orbita::stand::StationSession& session,
                       QWidget* parent = nullptr);
};
