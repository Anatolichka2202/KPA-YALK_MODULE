from pathlib import Path

main_cpp = Path("apps/desktop/mainwindow.cpp")
ktma_cpp = Path("apps/desktop/ktma_mainwindow.cpp")

main = main_cpp.read_text(encoding="utf-8")
legacy_catch = '''        testPage_->setScenarioInfo("UBSI_NORMAL_5_6", false, false, {}, detail);\n        log(detail);\n'''
if legacy_catch in main:
    main = main.replace(legacy_catch, '''        // Delivery/application composition publishes product-specific scenario\n        // availability after the reusable station runtime has been created.\n        log(detail);\n''', 1)
main_cpp.write_text(main, encoding="utf-8")

ktma = ktma_cpp.read_text(encoding="utf-8")
ktma = ktma.replace('    const QList<QString> publishedCodes = {\n',
                    '    const QStringList publishedCodes = {\n', 1)
ktma_cpp.write_text(ktma, encoding="utf-8")

print("Finished KTMA scenario bootstrap boundary cleanup")
