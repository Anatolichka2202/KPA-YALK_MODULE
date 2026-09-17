from pathlib import Path

path = Path("apps/desktop/mainwindow.cpp")
text = path.read_text(encoding="utf-8")
legacy = "        for (const auto& device : standProfile_.devices) {\n"
canonical = "        for (const auto& component : standProfile_.components) {\n"

if legacy not in text:
    if canonical in text:
        print("ComponentProfile desktop cleanup already applied")
        raise SystemExit(0)
    raise SystemExit("legacy DeviceProfile metadata loop not found")

start = text.index(legacy)
end_marker = "        const auto catalog = orbita::stand::importCatalogYaml(\n"
end = text.index(end_marker, start)

replacement = '''        for (const auto& component : standProfile_.components) {
            if (component.kind != "equipment") continue;
            const auto host = component.configuration.find("host");
            const auto port = component.configuration.find("port");
            QString endpoint = host == component.configuration.end()
                ? QString::fromStdString(component.provider)
                : QString::fromStdString(host->second);
            if (port != component.configuration.end())
                endpoint += QStringLiteral(":") + QString::fromStdString(port->second);
            for (const auto& capability : component.capabilities) {
                const QString code = equipmentCode.value(QString::fromStdString(capability));
                if (!code.isEmpty()) testPage_->setEquipmentConnection(code, endpoint);
            }
        }
'''

text = text[:start] + replacement + text[end:]
path.write_text(text, encoding="utf-8")
print("Replaced legacy DeviceProfile desktop metadata loop with ComponentProfile")
