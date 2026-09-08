import { useMemo, useState } from "react";
import {
  ArrowRight,
  Buildings,
  ChartLine,
  CheckCircle,
  Circuitry,
  Database,
  FileText,
  Gear,
  GridFour,
  ListChecks,
  MagnifyingGlass,
  Play,
  Pulse,
  ShieldCheck,
  SlidersHorizontal,
  SquaresFour,
  TreeStructure,
  Warning,
  Waveform,
  Wrench,
} from "@phosphor-icons/react";
import {
  CommandButton,
  Footer,
  Panel,
  ProductHeader,
  SessionCommandBar,
  StatusBadge,
} from "./designSystem.jsx";

const products = [
  { id: "ktma", name: "КТМА", description: "контрольно-тестовая и производственная надстройка", status: "READY" },
  { id: "ppb", name: "ППБ", description: "предметный контур не раскрыт в текущей UX-спеке", status: "UNAVAILABLE" },
  { id: "sus", name: "СУС", description: "предметный контур не раскрыт в текущей UX-спеке", status: "UNAVAILABLE" },
  { id: "voronezh", name: "ВОРОНЕЖ", description: "предметный контур не раскрыт в текущей UX-спеке", status: "UNAVAILABLE" },
];

const stationResources = [
  ["Адаптер УБСИ", "УБСИ-468157-009", "BUSY"],
  ["ИСД", "УБСИ-468157-009", "BUSY"],
  ["В7-78/1", "УБСИ-468157-009", "BUSY"],
  ["Р4831", "свободен", "READY"],
  ["АКИП-1160/6", "свободен", "READY"],
];

function Page({ children, footer, className = "" }) {
  return <div className={`station-page ${className}`}><div className="station-page__body">{children}</div><Footer left={footer} /></div>;
}

function Eyebrow({ children }) {
  return <span className="ds-eyebrow">{children}</span>;
}

function Metric({ label, value, note }) {
  return <div className="metric"><span>{label}</span><b>{value}</b>{note && <small>{note}</small>}</div>;
}

function DataRow({ title, detail, status = "READY", onClick }) {
  const content = <><div><b>{title}</b><small>{detail}</small></div><StatusBadge status={status} /><ArrowRight /></>;
  return onClick ? <button className="data-row-v1" onClick={onClick}>{content}</button> : <div className="data-row-v1">{content}</div>;
}

export function StationHome({ go, engineering, toggleEngineering }) {
  return <>
    <ProductHeader product="ЯДРО СТАНЦИИ" title="Рабочее место" subtitle="выбор продукта, сеансы и оборудование" engineering={engineering} onEngineering={toggleEngineering} />
    <Page footer="Станция управляет продуктами, сеансами и общими ресурсами; предметная логика остаётся в надстройках.">
      <div className="hero-row">
        <div><Eyebrow>STATION SHELL V1</Eyebrow><h1>Измерительные продукты</h1><p>Открывайте рабочую задачу, а не внутреннюю структуру программы.</p></div>
        <StatusBadge status="READY" label="СТАНЦИЯ ГОТОВА" />
      </div>
      <div className="station-shell-grid">
        <Panel title="ПРОДУКТЫ" badge={<span className="ds-counter">4 КОНТУРА</span>} className="product-launcher">
          <div className="product-list">
            {products.map((product) => (
              <button key={product.id} className={`product-row ${product.id === "ktma" ? "product-row--active" : ""}`} disabled={product.id !== "ktma"} onClick={() => product.id === "ktma" && go("ktma")}>
                <div className="product-glyph">{product.id === "ktma" ? <Circuitry /> : <SquaresFour />}</div>
                <div><b>{product.name}</b><small>{product.description}</small></div>
                <StatusBadge status={product.status} label={product.id === "ktma" ? "ЗАГРУЖЕНО" : "НЕ ОПИСАНО"} />
                <ArrowRight />
              </button>
            ))}
          </div>
        </Panel>
        <div className="station-shell-side">
          <Panel title="АКТИВНЫЕ СЕАНСЫ" badge={<span className="ds-counter">2</span>}>
            <DataRow title="УБСИ-468157-009" detail="КТМА · производство · контроль после сборки" status="RUNNING" onClick={() => go("production-session")} />
            <DataRow title="УБСИ-468157-012" detail="КТМА · ПСИ по ТУ · ожидает ручное действие" status="ACTION" onClick={() => go("acceptance-session")} />
          </Panel>
          <Panel title="РЕСУРСЫ СТАНЦИИ" badge={<StatusBadge status="READY" label="5 УЧТЕНО" />}>
            <div className="resource-list">{stationResources.map(([name, owner, status]) => <div className="resource-row" key={name}><div><b>{name}</b><small>{owner}</small></div><StatusBadge status={status} /></div>)}</div>
          </Panel>
        </div>
      </div>
    </Page>
  </>;
}

export function KtmaHub({ go, back, engineering, toggleEngineering }) {
  const tasks = [
    { id: "production", icon: Buildings, title: "ПРОИЗВОДСТВО", description: "изделия, этапы изготовления, промежуточные проверки, состав и повторы", state: "3 СЕАНСА" },
    { id: "acceptance", icon: ShieldCheck, title: "ПРИЁМО-СДАТОЧНАЯ ПРОВЕРКА ПО ТУ", description: "назначенная методика, обязательный маршрут, итог и протокол", state: "2 ОЖИДАЮТ" },
    { id: "admin", icon: SlidersHorizontal, title: "АДМИНИСТРИРОВАНИЕ", description: "изделия, состав, история, профили стенда и эксплуатационные настройки", state: "ДОСТУПНО" },
  ];
  return <>
    <ProductHeader product="КТМА" title="Выбор рабочей задачи" subtitle="три штатных контура; F12 не меняет бизнес-задачу" engineering={engineering} onBack={back} onEngineering={toggleEngineering} />
    <Page footer="КТМА · рабочая задача первична; инженерный уровень открывается отдельно по F12.">
      <div className="hero-row"><div><Eyebrow>КТМА · PRODUCT HOME</Eyebrow><h1>Что требуется сделать</h1><p>Выберите контур работы. УБСИ, БСИ и отдельные ячейки появляются ниже — внутри выбранной задачи.</p></div></div>
      <div className="task-grid">
        {tasks.map(({ id, icon: Icon, title, description, state }) => <button className="task-card" key={id} onClick={() => go(id)}><Icon /><div><b>{title}</b><p>{description}</p></div><strong>{state}</strong><ArrowRight /></button>)}
      </div>
      <div className="two-column-grid">
        <Panel title="ПОСЛЕДНИЕ СОБЫТИЯ"><div className="timeline-v1"><span><b>12:38</b> Производство · УБСИ-009 · этап выполняется</span><span><b>12:31</b> ПСИ · УБСИ-012 · требуется установка Р4831</span><span><b>11:54</b> Администрирование · состав изделия подтверждён</span></div></Panel>
        <Panel title="ОБОРУДОВАНИЕ"><div className="resource-list">{stationResources.slice(0,4).map(([name, owner, status]) => <div className="resource-row" key={name}><div><b>{name}</b><small>{owner}</small></div><StatusBadge status={status} /></div>)}</div></Panel>
      </div>
    </Page>
  </>;
}

const productionRows = [
  ["УБСИ-468157-009", "После сборки", "Контроль ЯЛК и питания", "RUNNING"],
  ["БСИ-468157-042", "Входной контроль", "Назначить процедуру", "ACTION"],
  ["ЯЛК-96 № 96-00427", "До установки", "Повторная проверка после замены", "ACTION"],
  ["ЯТП № ТП-00184", "Сборка", "Результаты сохранены", "NORMAL"],
];

export function ProductionHome({ go, back, engineering, toggleEngineering }) {
  return <>
    <ProductHeader product="КТМА · ПРОИЗВОДСТВО" title="Очередь изделий и этапов" subtitle="производственные задачи и незавершённые проверки" engineering={engineering} onBack={back} onEngineering={toggleEngineering} />
    <Page footer="Производство · этапы, состав и повторные проверки — предметный контур КТМА.">
      <div className="hero-row"><div><Eyebrow>ПРОИЗВОДСТВО</Eyebrow><h1>Очередь работ</h1><p>Оператор видит следующую рабочую задачу и состояние изделия, а не внутренние модули приложения.</p></div><CommandButton primary onClick={() => go("production-session")}><Play /> ОТКРЫТЬ СЕАНС</CommandButton></div>
      <Panel title="ПРОИЗВОДСТВЕННАЯ ОЧЕРЕДЬ" badge={<span className="ds-counter">4 ЗАПИСИ</span>}>
        <div className="work-table work-table--production"><div className="work-table__head"><span>ИЗДЕЛИЕ / ЯЧЕЙКА</span><span>ЭТАП</span><span>СЛЕДУЮЩЕЕ ДЕЙСТВИЕ</span><span>СОСТОЯНИЕ</span></div>{productionRows.map(([item, stage, action, status], index) => <button key={item} className="work-table__row" onClick={() => index === 0 && go("production-session")}><b>{item}</b><span>{stage}</span><span>{action}</span><StatusBadge status={status} /></button>)}</div>
      </Panel>
      <div className="domain-grid">
        {[['УБСИ', Waveform, '3 в работе'], ['БСИ', Circuitry, '1 ожидает'], ['ОТДЕЛЬНЫЕ ЯЧЕЙКИ', GridFour, '2 проверки'], ['ПРОМЕЖУТОЧНЫЕ ИЗМЕРЕНИЯ', Pulse, '7 операций']].map(([name, Icon, meta]) => <div className="domain-card" key={name}><Icon /><b>{name}</b><span>{meta}</span></div>)}
      </div>
    </Page>
  </>;
}

function ChannelOverview({ selected = 57, onSelect }) {
  const data = useMemo(() => Array.from({ length: 96 }, (_, i) => {
    const ch = i + 1;
    const deviation = Number((((ch * 37) % 83) / 100 - 0.41).toFixed(2));
    const actual = Number((3.1 + deviation * 0.031).toFixed(3));
    return { ch, deviation, actual };
  }), []);
  return <div className="channel-overview">
    <div className="channel-axis"><span>+0,50%</span><span>0</span><span>−0,50%</span></div>
    <div className="channel-bars-v1">{data.map((item) => {
      const height = Math.max(8, Math.abs(item.deviation) / 0.5 * 46);
      return <button key={item.ch} className={`channel-bar-v1 ${item.ch === selected ? "channel-bar-v1--selected" : ""}`} onClick={() => onSelect(item.ch)} title={`Канал ${item.ch}: ${item.deviation > 0 ? '+' : ''}${item.deviation}%`}><span className="channel-bar-v1__value">{item.actual.toFixed(3)}</span><i className={item.deviation >= 0 ? "positive" : "negative"} style={{ height: `${height}%` }} /><small>{item.ch}</small></button>;
    })}</div>
    <div className="channel-caption"><span>Y: отклонение, %</span><span>X: каналы 1–96</span><span>число над столбцом: фактическое значение, В</span></div>
  </div>;
}

function MiniTrend({ label, value, unit, kind = "signal" }) {
  const points = kind === "power" ? "0,42 20,40 40,41 60,38 80,39 100,37 120,38 140,35 160,36 180,34 200,35 220,34 240,36 260,35" : "0,39 20,41 40,38 60,40 80,37 100,39 120,36 140,38 160,35 180,37 200,34 220,36 240,35 260,36";
  return <div className="mini-trend"><div><span>{label}</span><b>{value} {unit}</b></div><svg viewBox="0 0 260 72" preserveAspectRatio="none" aria-label={label}><polyline points={points} /></svg></div>;
}

export function ProductionSession({ back, engineering, toggleEngineering }) {
  const [selected, setSelected] = useState(57);
  const [paused, setPaused] = useState(false);
  const [stopped, setStopped] = useState(false);
  return <>
    <ProductHeader product="КТМА · ПРОИЗВОДСТВО" title="УБСИ-468157-009 · контроль после сборки" subtitle="активный сеанс · все значения демонстрационные" engineering={engineering} onBack={back} onEngineering={toggleEngineering} />
    <div className="session-page">
      <div className="session-page__body">
        <div className="hero-row hero-row--compact"><div><Eyebrow>ЭТАП 02 · НОРМАЛЬНЫЕ УСЛОВИЯ</Eyebrow><h1>Контроль каналов ЯЛК</h1><p>Канальный обзор — главный HMI; временной тренд и потребление дают контекст выбранного канала.</p></div><StatusBadge status={stopped ? "STOPPED" : paused ? "INCOMPLETE" : "RUNNING"} /></div>
        <div className="production-session-layout">
          <Panel title="ВСЕ КАНАЛЫ · ОТКЛОНЕНИЕ ОТ ЭТАЛОНА" badge={<span className="ds-counter">96 КАНАЛОВ</span>} className="channel-panel">
            <ChannelOverview selected={selected} onSelect={setSelected} />
          </Panel>
          <aside className="measurement-rail">
            <Panel title={`ВЫБРАН КАНАЛ ${String(selected).padStart(2, '0')}`}><div className="metric-grid"><Metric label="ЗАДАНО" value="3,100 В" /><Metric label="В7" value="3,107 В" /><Metric label="ЯЛК" value="3,105 В" /><Metric label="ОТКЛОНЕНИЕ" value="−0,032 %" note="допуск ±0,50 %" /></div></Panel>
            <MiniTrend label="ВЫБРАННЫЙ КАНАЛ · 16 СВЕЖИХ ОТСЧЁТОВ" value="3,105" unit="В" />
            <MiniTrend label="ПОТРЕБЛЕНИЕ · 24 В" value="0,31" unit="А" kind="power" />
            <Panel title="СОСТАВ"><div className="composition"><b>ЯЛК-96 № 96-00427</b><span>состав подтверждён</span><small>замен в текущем этапе нет</small></div></Panel>
          </aside>
        </div>
      </div>
      <SessionCommandBar status={stopped ? "STOPPED" : paused ? "INCOMPLETE" : "RUNNING"} progress={stopped ? "Сеанс остановлен безопасно" : "Канал 57 / 96 · поток активен"} onPause={() => !stopped && setPaused(v => !v)} paused={paused} onStop={() => setStopped(true)} primaryLabel="ОТКРЫТЬ ВЕДОМОСТЬ" onPrimary={() => {}} />
    </div>
  </>;
}

const acceptanceSteps = [
  ["01", "Назначение", "изделие и методика"],
  ["02", "Готовность", "оборудование и безопасное состояние"],
  ["03", "Калибровка", "ЯЛК · адреса 97 / 99"],
  ["04", "Каналы ЯЛК", "измерения по методике"],
  ["05", "ЯТП · 0 Ω", "30 каналов"],
  ["06", "ЯТП · 120 Ω", "ручное действие Р4831"],
  ["07", "ЯТП · 240 Ω", "30 каналов"],
  ["08", "Итог", "безопасный сброс и отчёт"],
];

export function AcceptanceHome({ go, back, engineering, toggleEngineering }) {
  return <>
    <ProductHeader product="КТМА · ПСИ ПО ТУ" title="Назначенные проверки" subtitle="приёмо-сдаточная рабочая задача" engineering={engineering} onBack={back} onEngineering={toggleEngineering} />
    <Page footer="ПСИ по ТУ · полный нормативный итог возможен только по утверждённому составу обязательных проверок.">
      <div className="hero-row"><div><Eyebrow>ПРИЁМО-СДАТОЧНАЯ ПРОВЕРКА</Eyebrow><h1>Назначенные изделия</h1><p>Экран показывает готовность маршрута и причину, по которой запуск доступен или заблокирован.</p></div><CommandButton primary onClick={() => go("acceptance-session")}><Play /> ОТКРЫТЬ ПРОВЕРКУ</CommandButton></div>
      <div className="two-column-grid">
        <Panel title="ОЧЕРЕДЬ ПСИ"><DataRow title="УБСИ-468157-012" detail="методика назначена · оборудование готово" status="READY" onClick={() => go("acceptance-session")} /><DataRow title="УБСИ-468157-014" detail="ожидается подтверждение состава" status="ACTION" /><DataRow title="УБСИ-468157-015" detail="адаптер занят другим сеансом" status="BUSY" /></Panel>
        <Panel title="ГОТОВНОСТЬ МАРШРУТА"><div className="check-list-v1">{['Изделие и серийный номер', 'Методика и версия сценария', 'Профиль стенда', 'Обязательные ресурсы', 'Безопасное состояние выходов'].map((x) => <div key={x}><CheckCircle weight="fill" /><span>{x}</span><b>ГОТОВО</b></div>)}</div></Panel>
      </div>
    </Page>
  </>;
}

function ManualAction({ onClose, onConfirm }) {
  const [checked, setChecked] = useState(false);
  return <div className="manual-overlay" role="dialog" aria-modal="true" aria-label="Ручное действие Р4831"><div className="manual-dialog">
    <header><div><Eyebrow>MANUAL ACTION · Р4831</Eyebrow><h2>Установите 120 Ω</h2><p>ЯТП · канал 01 / 30 · общий разъём X123</p></div><StatusBadge status="ACTION" /></header>
    <div className="manual-values"><Metric label="ЦЕЛЕВОЕ ЗНАЧЕНИЕ" value="120 Ω" /><Metric label="ФАКТИЧЕСКОЕ ЗНАЧЕНИЕ" value="120,34 Ω" /><Metric label="ДОПУСК" value="±0,50 %" /></div>
    <div className="manual-instruction"><Warning weight="fill" /><div><b>Физически установите сопротивление на магазине.</b><span>После установки подтвердите действие. Галочка фиксирует действие оператора, но не заменяет измерительный результат.</span></div></div>
    <label className="manual-confirm"><input type="checkbox" checked={checked} onChange={e => setChecked(e.target.checked)} /><span>Р4831 физически установлен на 120 Ω</span></label>
    <div className="manual-audit"><span>Оператор: <b>Иванов И.И.</b></span><span>Сеанс: <b>RUN-DEMO-005184</b></span></div>
    <footer><CommandButton onClick={onClose}>ОТМЕНА</CommandButton><CommandButton primary disabled={!checked} onClick={onConfirm}>ПОДТВЕРДИТЬ ДЕЙСТВИЕ</CommandButton></footer>
  </div></div>;
}

function AcceptanceContent({ step, openManual }) {
  if (step === 0) return <Panel title="НАЗНАЧЕНИЕ И МЕТОДИКА" badge={<StatusBadge status="READY" label="ПОДТВЕРЖДЕНО" />}><div className="form-readonly"><label><span>ИЗДЕЛИЕ</span><b>УБСИ-468157-012</b></label><label><span>МЕТОДИКА</span><b>ПСИ · утверждённая версия</b></label><label><span>СОСТАВ</span><b>ЯЛК-96 № 96-00431 · ЯТП № ТП-00192</b></label><label><span>ПРОФИЛЬ СТЕНДА</span><b>Станция 01 · профиль 1.0</b></label></div></Panel>;
  if (step === 1) return <Panel title="ГОТОВНОСТЬ ОБОРУДОВАНИЯ" badge={<StatusBadge status="READY" />}><div className="check-list-v1">{['Адаптер УБСИ', 'ИСД', 'В7-78/1', 'Р4831', 'Безопасное состояние выходов'].map(x => <div key={x}><CheckCircle weight="fill" /><span>{x}</span><b>ГОТОВО</b></div>)}</div></Panel>;
  if (step === 2) return <Panel title="КАЛИБРОВКА ЯЛК-96" badge={<StatusBadge status="NORMAL" />}><div className="calibration-v1"><Metric label="АДРЕС 97" value="0,512 В" note="демо-значение" /><div className="calibration-link"><i /></div><Metric label="АДРЕС 99" value="5,986 В" note="демо-значение" /></div><div className="info-note"><ShieldCheck /> Адреса 97 / 99 отображаются как предметная деталь КТМА и не входят в общий Station design system.</div></Panel>;
  if (step === 3) return <Panel title="КАНАЛЫ ЯЛК-96" badge={<StatusBadge status="RUNNING" />}><ChannelOverview selected={57} onSelect={() => {}} /></Panel>;
  if (step === 4) return <Panel title="ЯТП · ТОЧКА 0 Ω" badge={<StatusBadge status="NORMAL" />}><div className="summary-strip"><Metric label="КАНАЛЫ" value="30 / 30" /><Metric label="МАКС. ОТКЛОНЕНИЕ" value="0,28 %" /><Metric label="ИТОГ" value="НОРМА" /></div></Panel>;
  if (step === 5) return <Panel title="ЯТП · ТОЧКА 120 Ω" badge={<StatusBadge status="ACTION" />}><div className="required-action"><Warning weight="fill" /><div><b>Требуется ручное действие оператора</b><p>Установите 120 Ω на Р4831 и подтвердите физическое действие в отдельном диалоге.</p></div><CommandButton primary onClick={openManual}><Gear /> ОТКРЫТЬ MANUAL ACTION</CommandButton></div></Panel>;
  if (step === 6) return <Panel title="ЯТП · ТОЧКА 240 Ω" badge={<StatusBadge status="NORMAL" />}><div className="summary-strip"><Metric label="КАНАЛЫ" value="30 / 30" /><Metric label="МАКС. ОТКЛОНЕНИЕ" value="0,24 %" /><Metric label="ИТОГ" value="НОРМА" /></div></Panel>;
  return <Panel title="ИТОГ И БЕЗОПАСНЫЙ СБРОС" badge={<StatusBadge status="INCOMPLETE" />}><div className="result-summary"><FileText /><div><span>РЕЗУЛЬТАТ ДЕМОНСТРАЦИОННОГО МАРШРУТА</span><h2>ЯЛК-96 И ЯТП — НОРМА</h2><p>Этот прототип не заявляет полный итог по п. 5.6 ТУ только на основании ЯЛК/ЯТП. Для полного нормативного заключения должны быть завершены все обязательные проверки утверждённой методики.</p></div></div></Panel>;
}

export function AcceptanceSession({ back, engineering, toggleEngineering }) {
  const [step, setStep] = useState(0);
  const [manual, setManual] = useState(false);
  const [stopped, setStopped] = useState(false);
  const next = () => setStep(v => Math.min(7, v + 1));
  return <>
    <ProductHeader product="КТМА · ПСИ ПО ТУ" title="УБСИ-468157-012 · активный сеанс" subtitle={`шаг ${step + 1} из 8 · данные прототипа`} engineering={engineering} onBack={back} onEngineering={toggleEngineering} />
    <div className="session-page">
      <div className="session-page__body">
        <div className="hero-row hero-row--compact"><div><Eyebrow>ПРИЁМО-СДАТОЧНЫЙ МАРШРУТ</Eyebrow><h1>{acceptanceSteps[step][1]}</h1><p>{acceptanceSteps[step][2]}</p></div><StatusBadge status={stopped ? "STOPPED" : step === 5 ? "ACTION" : step === 7 ? "INCOMPLETE" : "RUNNING"} /></div>
        <div className="step-rail">{acceptanceSteps.map(([num, title, note], index) => <button key={num} className={`${index === step ? "current" : ""} ${index < step ? "done" : ""}`} onClick={() => !stopped && setStep(index)}><span>{index < step ? <CheckCircle weight="fill" /> : num}</span><b>{title}</b><small>{note}</small></button>)}</div>
        <div className="acceptance-body"><AcceptanceContent step={step} openManual={() => setManual(true)} /><Panel title="ХОД ПРОЦЕДУРЫ"><div className="progress-v1"><strong>{Math.round((step + 1) / 8 * 100)}%</strong><div><i style={{ width: `${((step + 1) / 8) * 100}%` }} /></div></div><dl className="session-meta"><dt>Изделие</dt><dd>УБСИ-468157-012</dd><dt>Стенд</dt><dd>Станция 01</dd><dt>Ошибки стенда</dt><dd>0</dd><dt>Режим F12</dt><dd>{engineering ? "инженерный" : "обычный"}</dd></dl></Panel></div>
      </div>
      <SessionCommandBar status={stopped ? "STOPPED" : step === 5 ? "ACTION" : step === 7 ? "INCOMPLETE" : "RUNNING"} progress={stopped ? "Сеанс остановлен безопасно" : `Шаг ${step + 1} / 8`} onBack={step > 0 && !stopped ? () => setStep(v => Math.max(0, v - 1)) : undefined} onStop={() => setStopped(true)} primaryLabel={step === 7 ? "ОТКРЫТЬ ПРОТОКОЛ" : "СЛЕДУЮЩИЙ ШАГ"} onPrimary={!stopped ? next : undefined} />
    </div>
    {manual && <ManualAction onClose={() => setManual(false)} onConfirm={() => { setManual(false); next(); }} />}
  </>;
}

export function AdminHome({ back, engineering, toggleEngineering }) {
  return <>
    <ProductHeader product="КТМА · АДМИНИСТРИРОВАНИЕ" title="Эксплуатационная конфигурация" subtitle="изделия, состав, история и профили" engineering={engineering} onBack={back} onEngineering={toggleEngineering} />
    <Page footer="Администрирование — штатная рабочая задача. Низкоуровневая диагностика и редактор сценариев остаются инженерным уровнем F12.">
      <div className="hero-row"><div><Eyebrow>АДМИНИСТРИРОВАНИЕ</Eyebrow><h1>Объекты и конфигурация</h1><p>Здесь управляют эксплуатационными сущностями. Представление таблиц backend и сырой БД не является целью интерфейса.</p></div></div>
      <div className="admin-grid">
        <Panel title="ИЗДЕЛИЯ И СОСТАВ"><div className="admin-actions"><button><TreeStructure /><b>КАРТОЧКИ ИЗДЕЛИЙ</b><span>серийные номера, состав, замены</span></button><button><ListChecks /><b>ИСТОРИЯ ПРОВЕРОК</b><span>запуски, результаты, повторно открытые проверки</span></button></div></Panel>
        <Panel title="СТАНЦИЯ И ПРОФИЛИ"><div className="admin-actions"><button><SlidersHorizontal /><b>ПРОФИЛИ СТЕНДА</b><span>разрешённые ресурсы и эксплуатационные настройки</span></button><button><Database /><b>АРХИВ И ОТЧЁТЫ</b><span>поиск по изделию и запуску, без раскрытия backend-схемы</span></button></div></Panel>
        <Panel title="ДОСТУП И ОТВЕТСТВЕННОСТЬ"><div className="admin-actions"><button><ShieldCheck /><b>УРОВНИ ДОПУСКА</b><span>разделены с рабочими задачами и F12</span></button><button><FileText /><b>ЖУРНАЛ ИЗМЕНЕНИЙ</b><span>кто, что и когда изменил в эксплуатационной конфигурации</span></button></div></Panel>
      </div>
    </Page>
  </>;
}

export function EngineeringDrawer({ open, route, onClose }) {
  if (!open) return null;
  return <aside className="engineering-drawer">
    <header><div><Wrench weight="fill" /><div><b>ИНЖЕНЕРНЫЙ РЕЖИМ</b><small>F12 · контекст рабочей задачи сохранён</small></div></div><CommandButton quiet onClick={onClose}>F12 · ЗАКРЫТЬ</CommandButton></header>
    <div className="engineering-context"><span>ТЕКУЩИЙ КОНТЕКСТ</span><b>{route.toUpperCase()}</b><p>Инженерный режим добавляет диагностику и редакторские инструменты, но не переключает «Производство ↔ ПСИ».</p></div>
    <div className="engineering-tools">
      <button><MagnifyingGlass /><div><b>ПОАДРЕСНАЯ ДИАГНОСТИКА</b><span>каналы, raw, трассировка источника</span></div><ArrowRight /></button>
      <button><TreeStructure /><div><b>РЕДАКТОР СЦЕНАРИЯ</b><span>граф шагов, свойства, diff и публикация</span></div><ArrowRight /></button>
      <button><ChartLine /><div><b>ТЕЛЕМЕТРИЯ СТАНЦИИ</b><span>потоки, задержки, технические ошибки</span></div><ArrowRight /></button>
      <button><Database /><div><b>ДИАГНОСТИЧЕСКИЕ ДАННЫЕ</b><span>техническое представление для инженера, не операторский UI</span></div><ArrowRight /></button>
    </div>
    <div className="engineering-warning"><Warning weight="fill" /><span>Воздействующие команды должны дополнительно проверять уровень допуска и занятость оборудования. В прототипе они намеренно не реализованы.</span></div>
  </aside>;
}
