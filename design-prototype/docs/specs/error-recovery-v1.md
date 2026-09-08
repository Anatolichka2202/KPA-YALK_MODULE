> **08.09.2026 — ИСТОРИЧЕСКИЙ РЕФЕРЕНС; НЕ АКТИВНОЕ ЗАДАНИЕ.** Приоритет имеет [поставка 2.0](../../../_plans/ubsi-minimal-delivery-2.0.md). Допустимый браузерный референс — состояние до `6ad9902`; сегодняшнюю переработку не переносить.

# Error & Recovery UX v1

Область: дизайн состояний и прототипы. Не определяет backend retry policy или аппаратные алгоритмы безопасности.

## 1. Классы проблем

### A. NOT_NORMAL — предметный результат

Проверка завершена, результат валиден, но не соответствует критерию.

UI:

- semantic status `НЕ НОРМА`;
- фактическое значение;
- критерий/допуск;
- источник измерения;
- допустимые действия по утверждённой методике;
- Safe Stop остаётся доступным в активном сеансе.

### B. ERROR — техническая ошибка

Результат шага нельзя считать валидным.

UI:

- `ОШИБКА`;
- краткая причина;
- какие данные считаются недействительными/несвежими;
- operator recovery action, если она безопасно определена;
- technical code только вторично;
- возможность безопасной остановки.

### C. BUSY — конфликт общего ресурса

Ресурс занят другим сеансом.

UI:

- `ЗАНЯТО`;
- имя ресурса;
- ID владельца/сеанса;
- текущая операция заблокирована;
- кнопка запуска disabled;
- никаких принудительных команд освобождения без отдельной подтверждённой процедуры.

### D. UNAVAILABLE — ресурс/функция отсутствует

UI:

- `НЕДОСТУПНО`;
- причина, если известна;
- что пользователь может сделать: выбрать другой объект, обратиться к администратору, установить/настроить профиль — только если это действительно применимо.

### E. STALE DATA — потеря свежести

Состояние относится к технической ошибке, но должно быть визуально заметно на HMI.

Требования к прототипу:

- last update timestamp/age;
- визуальное прекращение анимации live state;
- выбранный канал не должен продолжать выглядеть как свежий RUNNING;
- результат шага не вычисляется из stale data;
- Safe Stop сохраняется.

## 2. Recovery flow активного сеанса

```text
RUNNING
  ├── result outside criterion → NOT_NORMAL
  ├── operator action needed → ACTION → RUNNING
  ├── technical fault → ERROR
  │      ├── safe retry allowed → retry → RUNNING
  │      ├── operator correction required → ACTION → retry
  │      └── cannot recover → SAFE STOP → STOPPED
  └── operator Safe Stop → STOPPED
```

Конкретные retry rules задаются backend/методикой и не выдумываются в UX.

## 3. Equipment conflict flow

```text
operator selects run
→ Station checks required resource state
→ BUSY
→ show owner/session
→ disable Start
→ allow open owning session if permission allows
→ when resource becomes READY, Start may become available
```

Prototype responsibility: показать states и переходы. Production responsibility: обеспечить фактический lock/ownership.

## 4. Stand-error vs product-defect presentation

Никогда не объединять в один красный `FAIL`.

| Состояние | Значение |
|---|---|
| НЕ НОРМА | валидный предметный результат вне допуска |
| ОШИБКА | техническая невозможность определить валидный результат |
| НЕПОЛНАЯ | итог целого маршрута пока нельзя определить |

## 5. Permission denied

Системный паттерн:

```text
НЕДОСТУПНО
Для этого действия требуется другой уровень допуска.
Текущий сеанс продолжает работать без изменений.
```

Не маскировать permission denied как `ОШИБКА СТЕНДА`.

## 6. Destructive administration action

До реализации edit/delete flows использовать двухшаговый pattern:

1. обычная команда открывает confirmation;
2. confirmation показывает объект и последствия;
3. destructive button формулирует действие явно;
4. если действие влияет на активный сеанс/ресурс, его нельзя подтверждать без отдельного safety rule.

Пример UI anatomy, не утверждение о доступной production-функции:

```text
ИЗМЕНИТЬ СОСТАВ ИЗДЕЛИЯ?
Изделие: ...
Текущий сеанс: ...
Последствие: связанные проверки могут потребовать повторного открытия.

[ОТМЕНА] [ПОДТВЕРДИТЬ ИЗМЕНЕНИЕ]
```

Фактическое правило повторного открытия проверок должно приходить из утверждённой бизнес-логики.

## 7. Design QA cases

Обязательные screenshot states для следующего QA slice:

- Station: no sessions;
- Station: product unavailable;
- Equipment: busy;
- Equipment: error;
- Production channel: NOT_NORMAL selected channel;
- Production channel: stale data;
- Acceptance: manual action;
- Acceptance: stand error;
- Session: safely stopped;
- Engineering: permission denied.
