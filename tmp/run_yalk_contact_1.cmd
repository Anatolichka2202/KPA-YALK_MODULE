@echo off
"C:\Orbita\releases\tu_app-tu-final-direct-20260921\yalk_channel_probe.exe" "C:\Orbita\releases\tu_app-tu-final-direct-20260921\data\stand_ktma.yaml" 1 0.8 "C:\Orbita\yalk_contact_1.log" > "C:\Orbita\yalk_contact_1.stdout" 2> "C:\Orbita\yalk_contact_1.stderr"
echo %ERRORLEVEL% > "C:\Orbita\yalk_contact_1.exit"
