# Технический reference

Этот раздел содержит короткие технические контракты текущей системы.

Его не нужно читать целиком.

Открывать только документ по конкретному вопросу.

---

## API и ABI

| Вопрос | Документ |
|---|---|
| Как программные части вызывают друг друга | [API](api.md) |
| Как DLL-плагин физически подключается к станции | [Equipment Plugin ABI](abi.md) |

---

## Протоколы

| Протокол | Документ |
|---|---|
| Орбита | [protocols/orbita.md](protocols/orbita.md) |
| ROKT / адаптер RS-485 ↔ Ethernet | [protocols/rokt.md](protocols/rokt.md) |
| VISA / SCPI | [protocols/scpi-visa.md](protocols/scpi-visa.md) |
| HTTP ИСД | [protocols/isd-http.md](protocols/isd-http.md) |

---

## Компоненты

| Компонент | Документ |
|---|---|
| liborbita | [components/liborbita.md](components/liborbita.md) |
| Плагины оборудования | [components/equipment-plugins.md](components/equipment-plugins.md) |

---

# Граница reference

Reference отвечает:

> Как текущая система работает сейчас?

Research отвечает:

> Откуда мы это узнали?

Поэтому в reference:

- нет длинной истории экспериментов;
- нет старых гипотез;
- нет UI-дизайна;
- нет копии ТУ;
- нет планов разработки.

Если утверждение основано на исследовании живого стенда, reference даёт
ссылку на соответствующий research-документ.
