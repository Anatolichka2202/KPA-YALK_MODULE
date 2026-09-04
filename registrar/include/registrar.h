#ifndef REGISTRAR_H
#define REGISTRAR_H

#include "model.h"

#include <QSqlDatabase>

#include <string>

namespace ktma::registrar {

class Registrar
{
public:
    explicit Registrar(const std::string& databasePath);

    std::string createProduct(
        const std::string& productType,
        const std::string& serialNumber);

    std::string createComponent(
        const std::string& componentType,
        const std::string& serialNumber);

    void installComponent(
        const std::string& productId,
        const std::string& componentId);

    void removeComponent(
        const std::string& productId,
        const std::string& componentId,
        const std::string& reason);

    std::string beginStage(
        const std::string& productId,
        Stage stage);

    void attachRun(
        const std::string& stageAttemptId,
        const std::string& runId);

    void finishStage(
        const std::string& stageAttemptId,
        Verdict verdict);

private:
    QSqlDatabase database_;
};

} // namespace ktma::registrar

#endif // REGISTRAR_H
