import { useState } from "react";
import {
  ArrowCounterClockwise,
  ArrowRight,
  CheckCircle,
  Circuitry,
  Clock,
  FileText,
  FloppyDisk,
  Gear,
  Lock,
  MagnifyingGlass,
  PencilSimple,
  ShieldCheck,
  Trash,
  Warning,
  Wrench,
  XCircle,
} from "@phosphor-icons/react";
import {
  CommandButton,
  Footer,
  Panel,
  ProductHeader,
  StatusBadge,
} from "./designSystem.jsx";

function Page({ children, footer }) {
  return <div className="station-page scenario-page"><div className="station-page__body">{children}</div><Footer left={footer} /></div>;
}

function Eyebrow({ children }) {
  return <span className="ds-eyebrow">{children}</span>;
}

function DemoNotice() {
  return <div className="scenario-demo-notice"><Warning weight="fill" /><span>Все номера, значения и временные интервалы на этом экране — демонстрационные данные UX-прототипа. Экран не читает стенд и не изменяет production-систему.</span></div>;
}

export function ScenarioDock({ route, go }) {
  const items = [
    ["station", "ОСНОВНОЙ UX"],
    ["single-product", "1 ПРОДУКТ"],
    ["recovery", "ОШИБКИ"],
    ["report", "ОТЧЁТ"],
    ["admin-editor", "ADMIN EDIT"],
  ];
  return <nav className="scenario-dock" aria-label="Навигация по сценариям UX-прототипа">
    <span>UX SCENARIOS</span>
    {items.map(([id, label]) => <button key={id} className={route === id ? "active" : ""} onClick={() => go(id)}>{label}</button>)}
  </nav>;
}

export function SingleProductStartup({ go, engineering, toggleEngineering }) {
  return <>
    <ProductHeader product="ЯДРО СТАНЦИИ" title="Специализированная поставка" subtitle="установлен один предметный продукт" engineering={engineering} onEngineering={toggleEngineering} />
    <Page footer="Single-product startup · Station shell остаётся владельцем ресурсов, но не заставляет оператора проходить лишний launcher.">
      <div className="single-product-shell">
        <div className="single-product-brand"><Circuitry weight="duotone" /><div><Eyebrow>ПРОФИЛЬ ПОСТАВКИ · ОДИН ПРОДУКТ</Eyebrow><h1>КТМА</h1><p>Станция обнаружила один доступный предметный контур. Основной вход ведёт сразу к выбору рабочей задачи КТМА.</p></div></div>
        <StatusBadge status="READY" label="КТМА ЗАГРУЖЕНА" />
        <div className="single-product-actions"><CommandButton primary onClick={() => go("ktma")}><ArrowRight /> ОТКРЫТЬ КТМА</CommandButton><CommandButton onClick={() => go("station")}>ПОКАЗАТЬ СТАНЦИЮ</CommandButton></div>
        <div className="single-product-resource-strip"><div><span>АКТИВНЫЕ СЕАНСЫ</span><b>0</b></div><div><span>ОБЩИЕ РЕСУРСЫ</span><b>5 учтено</b></div><div><span>СТАТУС</span><StatusBadge status="READY" /></div></div>
        <DemoNotice />
      </div>
    </Page>
  </>;
}

const recoveryScenarios = {
  product: {
    title: "Дефект изделия",
    status: "NOT_NORMAL",
    icon: XCircle,
    headline: "Канал 57 вышел за допуск",
    context: "Измерение получено корректно; причина относится к результату изделия, а не к состоянию стенда.",
    facts: [["КАНАЛ", "57"], ["ОТКЛОНЕНИЕ", "+0,71 %"], ["ДОПУСК", "±0,50 %"], ["СВЕЖЕСТЬ", "кадр получен"]],
    next: "Повторить измерение разрешается как отдельная попытка. Предыдущий результат не скрывается.",
  },
  stand: {
    title: "Ошибка стенда",
    status: "ERROR",
    icon: Warning,
    headline: "Нет свежих данных эталона В7",
    context: "Нормативный результат шага не определяется. Ошибка должна храниться отдельно от НЕ НОРМА изделия.",
    facts: [["ИСТОЧНИК", "В7-78/1"], ["СОСТОЯНИЕ", "свежий кадр не получен"], ["ПОСЛЕДНИЙ КАДР", "2,4 с назад · DEMO"], ["РЕЗУЛЬТАТ", "не определён"]],
    next: "Проверить соединение/ресурс стенда и повторить техническую проверку. Не превращать ошибку стенда в НЕ НОРМА изделия.",
  },
  stale: {
    title: "Устаревшие данные",
    status: "STALE",
    icon: Clock,
    headline: "Телеметрия больше не считается свежей",
    context: "Число остаётся видимым как последнее полученное значение, но визуально перестаёт выглядеть текущим.",
    facts: [["КАНАЛ", "24"], ["ПОСЛЕДНЕЕ ЗНАЧЕНИЕ", "3,104 В · DEMO"], ["ВОЗРАСТ", "3,8 с · DEMO"], ["СОСТОЯНИЕ", "ДАННЫЕ УСТАРЕЛИ"]],
    next: "Оператор не должен принимать решение по stale data. Система ожидает восстановление потока либо безопасную остановку.",
  },
  busy: {
    title: "Конфликт ресурса",
    status: "BUSY",
    icon: Lock,
    headline: "Адаптер УБСИ занят другим сеансом",
    context: "Занятость — не ошибка прибора и не дефект изделия. Интерфейс показывает владельца ресурса и безопасные варианты.",
    facts: [["РЕСУРС", "Адаптер УБСИ"], ["ВЛАДЕЛЕЦ", "RUN-DEMO-005184"], ["ЗАДАЧА", "Производство · УБСИ-009"], ["СОСТОЯНИЕ", "ЗАНЯТО"]],
    next: "Можно открыть сеанс-владелец или вернуться к очереди. Принудительный захват не предлагается оператору.",
  },
};

export function RecoveryLab({ back, engineering, toggleEngineering }) {
  const [scenario, setScenario] = useState("product");
  const item = recoveryScenarios[scenario];
  const Icon = item.icon;
  return <>
    <ProductHeader product="КТМА · UX SCENARIO LAB" title="Ошибки и восстановление" subtitle="контракт состояний и следующий допустимый шаг" engineering={engineering} onBack={back} onEngineering={toggleEngineering} />
    <Page footer="Recovery UX · НЕ НОРМА, ОШИБКА СТЕНДА, stale data и ЗАНЯТО не взаимозаменяемы.">
      <div className="hero-row"><div><Eyebrow>ERROR / RECOVERY V1</Eyebrow><h1>Сценарии восстановления</h1><p>Каждое состояние отвечает на три вопроса: что случилось, к чему относится причина и что оператор может сделать дальше.</p></div></div>
      <div className="scenario-tabs">{Object.entries(recoveryScenarios).map(([id, value]) => <button key={id} className={scenario === id ? "active" : ""} onClick={() => setScenario(id)}><StatusBadge status={value.status} /><span>{value.title}</span></button>)}</div>
      <div className="recovery-layout">
        <Panel title={item.title.toUpperCase()} badge={<StatusBadge status={item.status} />} className="recovery-primary">
          <div className={`recovery-head recovery-head--${item.status.toLowerCase()}`}><Icon weight="fill" /><div><h2>{item.headline}</h2><p>{item.context}</p></div></div>
          <div className="recovery-facts">{item.facts.map(([label, value]) => <div key={label}><span>{label}</span><b>{value}</b></div>)}</div>
          <div className="recovery-next"><ShieldCheck weight="fill" /><div><span>СЛЕДУЮЩИЙ ДОПУСТИМЫЙ ШАГ</span><b>{item.next}</b></div></div>
          <div className="recovery-actions">
            {scenario === "product" && <><CommandButton><ArrowCounterClockwise /> ПОВТОРИТЬ ИЗМЕРЕНИЕ</CommandButton><CommandButton primary>ЗАФИКСИРОВАТЬ РЕЗУЛЬТАТ</CommandButton></>}
            {scenario === "stand" && <><CommandButton><MagnifyingGlass /> ПРОВЕРИТЬ СВЯЗЬ</CommandButton><CommandButton danger><ShieldCheck /> БЕЗОПАСНО ОСТАНОВИТЬ</CommandButton></>}
            {scenario === "stale" && <><CommandButton disabled>ПРОДОЛЖИТЬ ПО ДАННЫМ</CommandButton><CommandButton danger><ShieldCheck /> БЕЗОПАСНО ОСТАНОВИТЬ</CommandButton></>}
            {scenario === "busy" && <><CommandButton>ОТКРЫТЬ СЕАНС-ВЛАДЕЛЕЦ</CommandButton><CommandButton primary>ВЕРНУТЬСЯ К ОЧЕРЕДИ</CommandButton></>}
          </div>
        </Panel>
        <Panel title="ПРАВИЛО КЛАССИФИКАЦИИ"><div className="classification-list"><div><StatusBadge status="NOT_NORMAL" /><p>Измерение валидно, но изделие не соответствует допуску конкретной проверки.</p></div><div><StatusBadge status="ERROR" /><p>Проверка не даёт нормативного результата из-за технической проблемы стенда/источника.</p></div><div><StatusBadge status="STALE" /><p>Последнее значение известно, но больше не считается текущим.</p></div><div><StatusBadge status="BUSY" /><p>Ресурс исправен, но принадлежит другому активному сеансу.</p></div></div></Panel>
      </div>
      <DemoNotice />
    </Page>
  </>;
}

const reportChecks = [
  ["ЯЛК-96 · канальные измерения", "NORMAL", "результаты демонстрационного маршрута"],
  ["ЯТП · 0 / 120 / 240 Ω", "NORMAL", "ручные действия и измерения зафиксированы"],
  ["Остальные обязательные проверки утверждённой методики", "INCOMPLETE", "не представлены в этом UX-прототипе"],
];

export function ReportViewer({ back, engineering, toggleEngineering }) {
  return <>
    <ProductHeader product="КТМА · ОТЧЁТЫ" title="RUN-DEMO-005184" subtitle="итог, доказательства и детальная ведомость" engineering={engineering} onBack={back} onEngineering={toggleEngineering} />
    <Page footer="ReportViewer v1 · лицевая форма не подменяет детальную ведомость и не завышает нормативный итог.">
      <div className="report-viewer-layout">
        <Panel title="ИТОГ ЗАПУСКА" badge={<StatusBadge status="INCOMPLETE" />} className="report-summary-panel">
          <div className="report-v1-head"><div><Eyebrow>ПРОТОКОЛ · ДЕМОНСТРАЦИОННЫЙ ЗАПУСК</Eyebrow><h1>УБСИ № DEMO-012</h1><p>Полный вывод по п. 5.6 ТУ не формируется, потому что прототип показывает не весь утверждённый состав обязательных проверок.</p></div><StatusBadge status="INCOMPLETE" label="ПОЛНЫЙ ИТОГ НЕ ОПРЕДЕЛЁН" /></div>
          <div className="report-meta-v1"><div><span>ЗАПУСК</span><b>RUN-DEMO-005184</b></div><div><span>ОПЕРАТОР</span><b>Иванов И.И. · DEMO</b></div><div><span>СТАНЦИЯ</span><b>Станция 01 · DEMO</b></div><div><span>ПРОФИЛЬ</span><b>1.0 · DEMO</b></div></div>
          <div className="report-checks-v1">{reportChecks.map(([name, status, note]) => <div key={name}><div><b>{name}</b><span>{note}</span></div><StatusBadge status={status} /></div>)}</div>
          <div className="report-actions-v1"><CommandButton><FileText /> ОТКРЫТЬ ВЕДОМОСТЬ</CommandButton><CommandButton disabled>СФОРМИРОВАТЬ ИТОГОВЫЙ PDF</CommandButton></div>
        </Panel>
        <Panel title="ДЕТАЛЬНАЯ ВЕДОМОСТЬ" badge={<span className="ds-counter">ПРИМЕР 4 СТРОКИ</span>}>
          <div className="detail-ledger"><div className="detail-ledger__head"><span>ПРОВЕРКА</span><span>ИСТОЧНИК</span><span>ЗНАЧЕНИЕ</span><span>ДОПУСК</span><span>СТАТУС</span></div>{[
            ["ЯЛК · канал 57", "В7 + ЯЛК", "3,105 В · DEMO", "±0,50 %", "NORMAL"],
            ["ЯТП · канал 01 · 120 Ω", "Р4831 + ЯТП", "120,34 Ω · DEMO", "±0,50 %", "NORMAL"],
            ["Проверка X", "не выполнено", "—", "по методике", "INCOMPLETE"],
            ["Проверка Y", "не выполнено", "—", "по методике", "INCOMPLETE"],
          ].map((row) => <div className="detail-ledger__row" key={row[0]}><b>{row[0]}</b><span>{row[1]}</span><span>{row[2]}</span><span>{row[3]}</span><StatusBadge status={row[4]} /></div>)}</div>
        </Panel>
      </div>
      <DemoNotice />
    </Page>
  </>;
}

function DestructiveDialog({ onClose, onConfirm }) {
  const [reason, setReason] = useState("");
  const [checked, setChecked] = useState(false);
  return <div className="admin-modal" role="dialog" aria-modal="true" aria-label="Подтверждение изменения состава"><div className="admin-modal__dialog">
    <header><div><Eyebrow>ПОДТВЕРЖДЕНИЕ ИЗМЕНЕНИЯ СОСТАВА</Eyebrow><h2>Заменить ЯЛК-96 в карточке изделия?</h2></div><StatusBadge status="ACTION" /></header>
    <div className="admin-change"><span>СЕЙЧАС</span><b>ЯЛК-96 № 96-00427</b><ArrowRight /><span>НОВОЕ</span><b>ЯЛК-96 № DEMO-NEW</b></div>
    <label className="admin-field"><span>ПРИЧИНА ИЗМЕНЕНИЯ</span><textarea value={reason} onChange={(e) => setReason(e.target.value)} placeholder="Обязательное пояснение для журнала изменений" /></label>
    <label className="manual-confirm"><input type="checkbox" checked={checked} onChange={(e) => setChecked(e.target.checked)} /><span>Понимаю, что изменение состава должно открыть связанные обязательные проверки повторно</span></label>
    <div className="admin-modal__warning"><Warning weight="fill" /><span>Это только UX-состояние. Прототип не записывает данные и не вызывает backend.</span></div>
    <footer><CommandButton onClick={onClose}>ОТМЕНА</CommandButton><CommandButton danger disabled={!checked || reason.trim().length < 5} onClick={onConfirm}><Trash /> ПОДТВЕРДИТЬ ЗАМЕНУ</CommandButton></footer>
  </div></div>;
}

export function AdminEditor({ back, engineering, toggleEngineering }) {
  const [editing, setEditing] = useState(false);
  const [confirming, setConfirming] = useState(false);
  const [saved, setSaved] = useState(false);
  const [profileName, setProfileName] = useState("Станция 01 · основной профиль");
  return <>
    <ProductHeader product="КТМА · АДМИНИСТРИРОВАНИЕ" title="Карточка изделия и профиль" subtitle="редактирование эксплуатационной конфигурации" engineering={engineering} onBack={back} onEngineering={toggleEngineering} />
    <Page footer="Administration edit v1 · изменения имеют владельца, причину, подтверждение и понятное влияние на повторные проверки.">
      <div className="hero-row"><div><Eyebrow>ADMINISTRATION · EDIT STATE</Eyebrow><h1>УБСИ-468157-009</h1><p>Форма показывает эксплуатационные сущности, а не таблицы backend.</p></div><div className="hero-actions"><StatusBadge status={saved ? "READY" : editing ? "ACTION" : "NORMAL"} label={saved ? "ЧЕРНОВИК СОХРАНЁН" : editing ? "ЕСТЬ НЕСОХРАНЁННЫЕ ИЗМЕНЕНИЯ" : "ДАННЫЕ ЗАГРУЖЕНЫ"} />{editing ? <CommandButton primary onClick={() => { setSaved(true); setEditing(false); }}><FloppyDisk /> СОХРАНИТЬ ЧЕРНОВИК</CommandButton> : <CommandButton onClick={() => setEditing(true)}><PencilSimple /> РЕДАКТИРОВАТЬ</CommandButton>}</div></div>
      <div className="admin-editor-grid">
        <Panel title="КАРТОЧКА ИЗДЕЛИЯ"><div className="admin-form-grid"><label className="admin-field"><span>ЗАВОДСКОЙ НОМЕР</span><input value="УБСИ-468157-009" readOnly /></label><label className="admin-field"><span>ТЕКУЩИЙ ЭТАП</span><input value="После сборки" readOnly /></label><label className="admin-field admin-field--wide"><span>ПРОФИЛЬ СТАНЦИИ</span><input value={profileName} onChange={(e) => { setProfileName(e.target.value); setEditing(true); setSaved(false); }} readOnly={!editing} /></label></div></Panel>
        <Panel title="СОСТАВ" badge={<StatusBadge status="NORMAL" label="ПОДТВЕРЖДЁН" />}><div className="composition-list-v1"><div><div><b>ЯЛК-96 № 96-00427</b><span>установлена · проверки связаны с этим серийным номером</span></div><CommandButton danger onClick={() => setConfirming(true)}><Gear /> ЗАМЕНИТЬ</CommandButton></div><div><div><b>ЯТП № ТП-00184</b><span>установлена · изменений нет</span></div><StatusBadge status="NORMAL" /></div></div></Panel>
        <Panel title="ЖУРНАЛ ИЗМЕНЕНИЙ"><div className="admin-history"><div><b>07.09 · DEMO</b><span>профиль открыт для редактирования · оператор Иванов И.И.</span></div><div><b>05.09 · DEMO</b><span>состав подтверждён перед производственным этапом</span></div><div><b>02.09 · DEMO</b><span>предыдущая замена ячейки открыла связанные проверки повторно</span></div></div></Panel>
      </div>
      <DemoNotice />
    </Page>
    {confirming && <DestructiveDialog onClose={() => setConfirming(false)} onConfirm={() => { setConfirming(false); setSaved(false); setEditing(false); }} />}
  </>;
}