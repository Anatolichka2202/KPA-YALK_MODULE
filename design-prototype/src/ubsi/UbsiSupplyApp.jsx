import { useEffect, useMemo, useState } from "react";
import {
  ArrowLeft,
  ArrowRight,
  ArrowsClockwise,
  Bug,
  CheckCircle,
  Circuitry,
  Database,
  FileText,
  Gear,
  GridFour,
  MagnifyingGlass,
  Pause,
  Play,
  Plus,
  Pulse,
  ShieldCheck,
  SlidersHorizontal,
  Stop,
  Warning,
  Waveform,
  Wrench,
  X,
} from "@phosphor-icons/react";
import {
  PRODUCTION_STAGES,
  RUN_OPERATIONS,
  YALK_ADDRESSES,
  YVP_FREQUENCIES,
  useDemoTelemetry,
} from "./demoTelemetry.js";

const INITIAL_PRODUCTS = [
  {
    id: "ubsi-7",
    serial: "УБСИ-0007",
    stage: "Климат НУ",
    lastResult: "НОРМА",
    updated: "сегодня · 14:18",
    cells: { "ЯЛК-96": "96-00427", "ЯТП": "ТП-00184", "ЯВП-8": "ВП-00041", "ЯП-П": "ПП-00018" },
  },
  {
    id: "ubsi-12",
    serial: "УБСИ-0012",
    stage: "Первичное",
    lastResult: "НЕПОЛНАЯ ПРОВЕРКА",
    updated: "сегодня · 12:31",
    cells: { "ЯЛК-96": "96-00431", "ЯТП": "ТП-00192", "ЯВП-8": "ВП-00045", "ЯП-П": "ПП-00020" },
  },
  {
    id: "ubsi-14",
    serial: "УБСИ-0014",
    stage: "Заливка · климат +",
    lastResult: "НЕ НОРМА",
    updated: "вчера · 17:02",
    cells: { "ЯЛК-96": "96-00439", "ЯТП": "ТП-00197", "ЯВП-8": "ВП-00048", "ЯП-П": "ПП-00021" },
  },
];

const STATUS_CLASS = {
  "НОРМА": "ok",
  "ГОТОВО": "ok",
  "ВЫПОЛНЯЕТСЯ": "run",
  "НЕ НОРМА": "bad",
  "ОШИБКА СТЕНДА": "error",
  "НЕПОЛНАЯ ПРОВЕРКА": "warn",
  "ТРЕБУЕТСЯ ДЕЙСТВИЕ": "action",
  "ОСТАНОВЛЕНО": "muted",
  "STALE": "warn",
  "ТРЕБУЕТ COMMISSIONING": "action",
};

function Status({ children, tone }) {
  const value = String(children);
  return <span className={`us-status us-status--${tone || STATUS_CLASS[value] || "muted"}`}><i />{children}</span>;
}

function Button({ children, primary = false, danger = false, quiet = false, className = "", ...props }) {
  return <button className={`us-btn ${primary ? "us-btn--primary" : ""} ${danger ? "us-btn--danger" : ""} ${quiet ? "us-btn--quiet" : ""} ${className}`} {...props}>{children}</button>;
}

function Panel({ title, meta, children, className = "" }) {
  return <section className={`us-panel ${className}`}>
    {(title || meta) && <header className="us-panel__head"><b>{title}</b>{meta && <span>{meta}</span>}</header>}
    {children}
  </section>;
}

function AppHeader({ title, subtitle, onBack, engineering, setEngineering }) {
  return <header className="us-app-header">
    <div className="us-app-header__brand"><Circuitry weight="fill" /><div><span>ОРБИТА · КТМА</span><b>УБСИ · ПОСТАВКА</b></div></div>
    <div className="us-app-header__context">
      {onBack && <button className="us-icon-btn" onClick={onBack} aria-label="Назад"><ArrowLeft /></button>}
      <div><b>{title}</b><span>{subtitle}</span></div>
    </div>
    <button className={`us-engineering ${engineering ? "active" : ""}`} onClick={() => setEngineering(!engineering)}><Wrench /> F12 · ИНЖЕНЕРНЫЙ</button>
  </header>;
}

function Home({ go }) {
  const cards = [
    ["production", "ПРОИЗВОДСТВО", "Найти или зарегистрировать УБСИ, выбрать этап и выполнить производственную проверку", Waveform],
    ["tu", "ПРИЁМО-СДАТОЧНАЯ ПРОВЕРКА ПО ТУ", "Простой нормативный run: номер блока → подготовка стенда → проверка → короткий протокол", ShieldCheck],
    ["admin", "АДМИНИСТРИРОВАНИЕ", "Состав УБСИ, серийные номера ячеек, замены, история и отчёты", SlidersHorizontal],
  ];
  return <main className="us-page us-home">
    <div className="us-hero"><span>КТМА · ТЕКУЩАЯ ПОСТАВКА</span><h1>УБСИ</h1><p>Выберите рабочую задачу. Общая Station-концепция сохранена в проекте, но не мешает предметному операторскому flow.</p></div>
    <div className="us-home-actions">{cards.map(([id, title, text, Icon]) => <button key={id} onClick={() => go(id)} className="us-home-card"><Icon /><div><b>{title}</b><span>{text}</span></div><ArrowRight /></button>)}</div>
    <div className="us-source-note"><Database /><span><b>ПРОТОТИП</b> · интерфейс интерактивный; инженерные величины, не подтверждённые источниками, помечаются как DEMO.</span></div>
  </main>;
}

function ProductionEntry({ products, onSelect, onCreate }) {
  const [query, setQuery] = useState("");
  const filtered = products.filter((item) => item.serial.toLowerCase().includes(query.toLowerCase()));
  return <main className="us-page">
    <div className="us-title-row"><div><span>ПРОИЗВОДСТВО</span><h1>Выберите УБСИ</h1><p>Поиск изделия или регистрация нового блока — первый штатный шаг.</p></div></div>
    <div className="us-production-entry">
      <Panel title="НАЙТИ ИЗДЕЛИЕ" className="us-search-panel">
        <label className="us-search"><MagnifyingGlass /><input value={query} onChange={(event) => setQuery(event.target.value)} placeholder="Номер УБСИ / SN" autoFocus /></label>
        <Button primary onClick={onCreate}><Plus /> ЗАРЕГИСТРИРОВАТЬ БЛОК</Button>
      </Panel>
      <Panel title="ПОСЛЕДНИЕ ИЗДЕЛИЯ" meta={`${filtered.length} записей`}>
        <div className="us-product-table">
          <div className="us-product-row us-product-row--head"><span>ИЗДЕЛИЕ</span><span>ЭТАП</span><span>ПОСЛЕДНИЙ РЕЗУЛЬТАТ</span><span>ОБНОВЛЕНО</span><span /></div>
          {filtered.map((item) => <button className="us-product-row" key={item.id} onClick={() => onSelect(item)}><b>{item.serial}</b><span>{item.stage}</span><Status>{item.lastResult}</Status><span>{item.updated}</span><ArrowRight /></button>)}
        </div>
      </Panel>
    </div>
  </main>;
}

function CreateProductDialog({ onClose, onCreate }) {
  const [serial, setSerial] = useState("");
  return <div className="us-modal-wrap" role="dialog" aria-modal="true" aria-label="Регистрация нового УБСИ"><div className="us-modal">
    <header><div><span>ПРОИЗВОДСТВО</span><h2>Зарегистрировать УБСИ</h2></div><button className="us-icon-btn" onClick={onClose}><X /></button></header>
    <label className="us-field"><span>НОМЕР / SN УБСИ</span><input value={serial} onChange={(e) => setSerial(e.target.value)} placeholder="например, УБСИ-0015" autoFocus /></label>
    <div className="us-info"><Circuitry /><span>Состав ячеек можно заполнить сразу после создания или позже в разделе «По ячейкам».</span></div>
    <footer><Button onClick={onClose}>ОТМЕНА</Button><Button primary disabled={!serial.trim()} onClick={() => onCreate(serial.trim())}>СОЗДАТЬ</Button></footer>
  </div></div>;
}

function ProductionWorkspace({ product, updateProduct, goRun, goReport, onReplaceCell }) {
  const [tab, setTab] = useState("ubsi");
  const [stage, setStage] = useState(product.stage || PRODUCTION_STAGES[0]);
  useEffect(() => setStage(product.stage || PRODUCTION_STAGES[0]), [product.id, product.stage]);
  const saveStage = (value) => { setStage(value); updateProduct({ ...product, stage: value }); };
  return <main className="us-page us-workspace-page">
    <div className="us-title-row us-title-row--compact"><div><span>ПРОИЗВОДСТВО</span><h1>{product.serial}</h1><p>Этап изделия и текущая измерительная процедура — разные сущности.</p></div><Status>{product.lastResult}</Status></div>
    <div className="us-work-tabs"><button className={tab === "ubsi" ? "active" : ""} onClick={() => setTab("ubsi")}><Waveform /> УБСИ</button><button className={tab === "yvp" ? "active" : ""} onClick={() => setTab("yvp")}><Pulse /> ЯВП</button><button className={tab === "cells" ? "active" : ""} onClick={() => setTab("cells")}><GridFour /> ПО ЯЧЕЙКАМ</button></div>
    {tab === "ubsi" && <UbsiWorkspace product={product} stage={stage} setStage={saveStage} goRun={goRun} goReport={goReport} />}
    {tab === "yvp" && <YvpWorkspace product={product} />}
    {tab === "cells" && <CellsWorkspace product={product} onReplaceCell={onReplaceCell} />}
  </main>;
}

function UbsiWorkspace({ product, stage, setStage, goRun, goReport }) {
  return <div className="us-workspace-grid">
    <Panel title="ЭТАП ПРОИЗВОДСТВА" meta="выбирается до запуска">
      <div className="us-stage-grid">{PRODUCTION_STAGES.map((item) => <button key={item} className={item === stage ? "active" : ""} onClick={() => setStage(item)}>{item}{item === stage && <CheckCircle weight="fill" />}</button>)}</div>
    </Panel>
    <Panel title="СОСТАВ УБСИ" meta="текущие SN">
      <div className="us-composition">{Object.entries(product.cells).map(([name, sn]) => <div key={name}><span>{name}</span><b>{sn || "не задан"}</b></div>)}</div>
    </Panel>
    <Panel title="ПРОИЗВОДСТВЕННАЯ ПРОВЕРКА" className="us-launch-panel">
      <div className="us-check-summary"><div><span>ИЗДЕЛИЕ</span><b>{product.serial}</b></div><div><span>ЭТАП</span><b>{stage}</b></div><div><span>ПАКЕТ</span><b>ПОЛНАЯ ПРОВЕРКА УБСИ</b><small>питание · ЯЛК · ЯТП · ЯВП</small></div></div>
      <div className="us-launch-actions"><Button onClick={goReport}><FileText /> ПОСЛЕДНИЙ ОТЧЁТ</Button><Button primary onClick={goRun}><Play /> НАЧАТЬ ПРОВЕРКУ</Button></div>
    </Panel>
  </div>;
}

function YvpWorkspace({ product }) {
  const [channel, setChannel] = useState(1);
  const [frequency, setFrequency] = useState(250);
  return <div className="us-workspace-grid">
    <Panel title="ЯВП-8" meta={`SN ${product.cells["ЯВП-8"] || "не задан"}`}>
      <div className="us-yvp-channels">{Array.from({ length: 8 }, (_, i) => i + 1).map((item) => <button className={item === channel ? "active" : ""} key={item} onClick={() => setChannel(item)}>КАНАЛ {item}</button>)}</div>
      <div className="us-yvp-state"><Status>ТРЕБУЕТ COMMISSIONING</Status><p>UI маршрута подготовлен, но подтверждённая привязка live-потока ЯВП в текущем каталоге ещё не зафиксирована. Числовой verdict не подменяем DEMO-данными.</p></div>
    </Panel>
    <Panel title="МЕТОДИЧЕСКИЙ МАРШРУТ" meta="подтверждённые частоты">
      <div className="us-frequency-grid">{YVP_FREQUENCIES.map((value) => <button className={value === frequency ? "active" : ""} key={value} onClick={() => setFrequency(value)}>{String(value).replace(".", ",")} Гц</button>)}</div>
      <div className="us-yvp-path"><span>Rigol DG-1022Z</span><ArrowRight /><span>C = 1000 пФ</span><ArrowRight /><span>вход ЯВП {channel}</span><ArrowRight /><span>X1</span><ArrowRight /><span>ЯЛК / поток</span></div>
      <div className="us-demo-wave"><svg viewBox="0 0 700 120" preserveAspectRatio="none"><polyline points={Array.from({ length: 80 }, (_, i) => `${i * 9},${60 + Math.sin(i * (0.25 + frequency / 9000)) * 35}`).join(" ")} /></svg><span>DEMO · визуальный placeholder сигнала, не результат измерения</span></div>
    </Panel>
  </div>;
}

const RETEST_RULES = {
  "ЯЛК-96": "полная проверка ЯЛК",
  "ЯТП": "полный ЯТП · все 30 каналов · 0 / 120 / 240 Ω",
  "ЯВП-8": "полный ЯВП + связанные каналы ЯЛК 89–96",
  "ЯП-П": "питание / потребление",
};

function CellsWorkspace({ product, onReplaceCell }) {
  return <Panel title="СОСТАВ И ЗАМЕНЫ" meta="история SN сохраняется в целевой реализации">
    <div className="us-cell-list">{Object.entries(product.cells).map(([name, sn]) => <div className="us-cell-row" key={name}><div><b>{name}</b><span>SN {sn || "не задан"}</span></div><div className="us-cell-retest"><span>После замены</span><b>{RETEST_RULES[name]}</b></div><Button onClick={() => onReplaceCell(name)}><ArrowsClockwise /> ЗАМЕНИТЬ</Button></div>)}</div>
  </Panel>;
}

function ReplaceCellDialog({ cell, product, onClose, onSave, onRun }) {
  const [serial, setSerial] = useState("");
  const [saved, setSaved] = useState(false);
  const finish = () => { onSave(cell, serial.trim()); setSaved(true); };
  return <div className="us-modal-wrap" role="dialog" aria-modal="true"><div className="us-modal us-modal--wide">
    <header><div><span>СОСТАВ {product.serial}</span><h2>Замена {cell}</h2></div><button className="us-icon-btn" onClick={onClose}><X /></button></header>
    {!saved ? <><div className="us-replacement-before"><div><span>ТЕКУЩИЙ SN</span><b>{product.cells[cell] || "не задан"}</b></div><ArrowRight /><label className="us-field"><span>НОВЫЙ SN</span><input autoFocus value={serial} onChange={(e) => setSerial(e.target.value)} /></label></div><div className="us-warning"><Warning /><div><b>После замены потребуется повторная проверка</b><span>{RETEST_RULES[cell]}</span></div></div><footer><Button onClick={onClose}>ОТМЕНА</Button><Button primary disabled={!serial.trim()} onClick={finish}>СОХРАНИТЬ ЗАМЕНУ</Button></footer></> : <><div className="us-success"><CheckCircle weight="fill" /><div><b>{cell} · SN {serial}</b><span>Новый состав сохранён в состоянии прототипа.</span></div></div><div className="us-retest-card"><span>НЕОБХОДИМАЯ ПРОВЕРКА</span><b>{RETEST_RULES[cell]}</b></div><footer><Button onClick={onClose}>ПОЗЖЕ</Button><Button primary onClick={onRun}>ПРОВЕРИТЬ СЕЙЧАС</Button></footer></>}
  </div></div>;
}

function linePoints(values, width, height, accessor, min, max) {
  if (!values.length) return "";
  const range = Math.max(0.0001, max - min);
  return values.map((item, index) => `${(index / Math.max(1, values.length - 1)) * width},${height - ((accessor(item) - min) / range) * height}`).join(" ");
}

function ChannelBars({ channels, selected, setSelected, stale }) {
  return <div className={`us-channel-chart ${stale ? "is-stale" : ""}`}>
    <div className="us-y-axis"><span>+0,5%</span><span>0</span><span>−0,5%</span></div>
    <div className="us-bars">{channels.map((item) => {
      const height = Math.min(48, Math.max(2, Math.abs(item.deviation) / 0.5 * 44));
      return <button key={item.address} onClick={() => setSelected(item.address)} className={`${item.address === selected ? "selected" : ""} ${item.isFault ? "fault" : ""}`} title={`УЛК ${item.address}: ${item.actual.toFixed(4)} В; ${item.deviation >= 0 ? "+" : ""}${item.deviation.toFixed(3)} % FS`}>
        <span className="us-bar-value">{item.actual.toFixed(3)}</span>
        <span className="us-bar-dev">{item.deviation >= 0 ? "+" : ""}{item.deviation.toFixed(2)}%</span>
        <i className={item.deviation >= 0 ? "up" : "down"} style={{ height: `${height}%` }} />
        <small>{item.address}</small>
      </button>;
    })}</div>
    <div className="us-chart-caption"><span>Y: отклонение, % шкалы 0…6,2 В</span><span>X: 80 подтверждённых рабочих адресов УЛК</span><span>над столбцом: фактическое значение, В</span></div>
  </div>;
}

function BackgroundHeat({ rows }) {
  const latest = rows.length ? rows : [Array.from({ length: 80 }, () => 0)];
  return <div className="us-background-heat"><div className="us-heat-grid" style={{ gridTemplateColumns: `repeat(${YALK_ADDRESSES.length}, 1fr)` }}>{latest.flatMap((row, r) => row.map((value, c) => <i key={`${r}-${c}`} style={{ opacity: Math.min(0.95, 0.18 + Math.abs(value) * 7) }} />))}</div><div className="us-heat-caption"><span>старше</span><b>ФОНОВЫЕ КОЛЕБАНИЯ · 80 АДРЕСОВ</b><span>свежее</span></div></div>;
}

function TrendChart({ history, selected }) {
  const values = history.length ? history : Array.from({ length: 20 }, (_, i) => ({ value: 3.1 + Math.sin(i * 0.5) * 0.002 }));
  const min = Math.min(...values.map((v) => v.value)) - 0.002;
  const max = Math.max(...values.map((v) => v.value)) + 0.002;
  return <div className="us-small-chart"><header><span>ДИНАМИКА ВЫБРАННОГО АДРЕСА</span><b>УЛК {selected}</b></header><svg viewBox="0 0 500 125" preserveAspectRatio="none"><polyline points={linePoints(values, 500, 125, (v) => v.value, min, max)} /></svg><footer><span>{min.toFixed(4)} В</span><span>последние свежие отсчёты</span><span>{max.toFixed(4)} В</span></footer></div>;
}

function ConsumptionChart({ values }) {
  const data = values.length ? values : Array.from({ length: 25 }, (_, i) => ({ current: 0.28 + Math.sin(i * 0.4) * 0.01, voltage: 27 }));
  return <div className="us-small-chart us-consumption"><header><span>ПОТРЕБЛЕНИЕ УБСИ</span><b>граница 400 мА</b></header><svg viewBox="0 0 500 125" preserveAspectRatio="none"><line x1="0" y1="25" x2="500" y2="25" className="limit"/><polyline points={linePoints(data, 500, 125, (v) => v.current, 0.2, 0.45)} /></svg><footer><span>I, А</span><span>U: {data[data.length - 1]?.voltage || 27} В · DEMO trace</span><span>{Math.round((data[data.length - 1]?.current || 0) * 1000)} мА</span></footer></div>;
}

function OverloadView({ overload }) {
  const failed = overload.filter((item) => item.failed);
  const sample = overload[0] || { stressed: 1, polarity: "+12 В" };
  return <div className="us-overload"><div className="us-overload-head"><div><span>ПЕРЕГРУЖАЕМЫЙ ФИЗИЧЕСКИЙ КАНАЛ</span><b>{sample.stressed} / 88 · {sample.polarity}</b></div><div><span>КРИТЕРИЙ ОСТАЛЬНЫХ</span><b>|Δcode| ≤ 2</b></div><Status>{failed.length ? "НЕ НОРМА" : "НОРМА"}</Status></div><div className="us-overload-grid">{overload.map((item) => <div key={item.channel} className={`${item.failed ? "fault" : ""} ${item.channel === item.stressed ? "stressed" : ""}`}><small>{item.channel}</small><b>{item.channel === item.stressed ? "±12" : `${item.delta >= 0 ? "+" : ""}${item.delta}`}</b></div>)}</div><p>DEMO-представление процедуры: 88 физических каналов; перегруженный канал исключается из критерия устойчивости остальных.</p></div>;
}

function ManualResistor({ value = 120, onConfirm, onCancel }) {
  const [checked, setChecked] = useState(false);
  return <div className="us-modal-wrap"><div className="us-modal"><header><div><span>РУЧНОЕ ДЕЙСТВИЕ · Р4831</span><h2>Установите {value} Ω</h2></div></header><div className="us-manual-value"><b>{value} Ω</b><span>общий разъём X123</span></div><label className="us-check"><input type="checkbox" checked={checked} onChange={(e) => setChecked(e.target.checked)} /><span>Р4831 физически установлен на {value} Ω</span></label><footer>{onCancel && <Button onClick={onCancel}>ОТМЕНА</Button>}<Button primary disabled={!checked} onClick={onConfirm}>ПОДТВЕРДИТЬ</Button></footer></div></div>;
}

function ProductionRun({ product, onExit, onReport }) {
  const [operationIndex, setOperationIndex] = useState(2);
  const [selected, setSelected] = useState(47);
  const [running, setRunning] = useState(true);
  const [stopped, setStopped] = useState(false);
  const [faultMode, setFaultMode] = useState("normal");
  const [manual, setManual] = useState(false);
  const [scenarioOpen, setScenarioOpen] = useState(false);
  const operation = RUN_OPERATIONS[operationIndex];
  const telemetry = useDemoTelemetry({ running: running && !stopped, operation, faultMode, selectedAddress: selected });
  const selectedData = telemetry.channels.find((item) => item.address === selected) || telemetry.channels[0];
  const blocked = faultMode === "stale" || faultMode === "stand-error" || stopped;
  const verdict = stopped ? "ОСТАНОВЛЕНО" : faultMode === "stand-error" ? "ОШИБКА СТЕНДА" : faultMode === "not-normal" || faultMode === "overload-fail" ? "НЕ НОРМА" : faultMode === "stale" ? "STALE" : running ? "ВЫПОЛНЯЕТСЯ" : "ГОТОВО";

  useEffect(() => {
    if (!running || blocked || manual) return undefined;
    const timer = window.setInterval(() => {
      if (operation?.manual) { setManual(true); setRunning(false); return; }
      setOperationIndex((index) => index < RUN_OPERATIONS.length - 1 ? index + 1 : index);
    }, 9000);
    return () => window.clearInterval(timer);
  }, [blocked, manual, operation?.manual, running]);

  const jump = (id) => {
    const index = RUN_OPERATIONS.findIndex((item) => item.id === id);
    if (index >= 0) { setOperationIndex(index); setRunning(true); setStopped(false); setManual(false); telemetry.reset(); }
  };
  const safeStop = () => { setStopped(true); setRunning(false); setManual(false); };
  const finishManual = () => { setManual(false); setRunning(true); setOperationIndex((index) => Math.min(index + 1, RUN_OPERATIONS.length - 1)); };

  return <main className="us-run-page">
    <div className="us-run-top"><div><span>ПРОИЗВОДСТВО · {product.stage}</span><h1>{product.serial}</h1><p>Текущая операция: <b>{operation.title}</b></p></div><div className="us-run-top__right"><Status>{verdict}</Status><button className="us-scenario-toggle" onClick={() => setScenarioOpen(!scenarioOpen)}><Bug /> СЦЕНАРИИ ПРОТОТИПА</button></div></div>
    <div className="us-operation-strip">{RUN_OPERATIONS.map((item, index) => <button key={item.id} className={`${index < operationIndex ? "done" : ""} ${index === operationIndex ? "current" : ""}`} onClick={() => jump(item.id)}><span>{index + 1}</span><b>{item.short}</b></button>)}</div>
    <div className="us-hmi-grid">
      <Panel title={operation.overload ? "ЯЛК · ПЕРЕГРУЗКА ±12 В" : "ЯЛК · ВСЕ РАБОЧИЕ АДРЕСА"} meta={operation.overload ? "88 физических каналов" : "80 подтверждённых адресов"} className="us-main-chart-panel">
        {operation.overload ? <OverloadView overload={telemetry.overload} /> : <ChannelBars channels={telemetry.channels} selected={selected} setSelected={setSelected} stale={faultMode === "stale"} />}
        {!operation.overload && <BackgroundHeat rows={telemetry.background} />}
        {faultMode === "stand-error" && <div className="us-chart-overlay"><Warning /><div><Status>ОШИБКА СТЕНДА</Status><b>Нет достоверного измерения</b><span>Нормативный verdict изделия не формируется. Safe Stop остаётся доступен.</span></div></div>}
        {faultMode === "stale" && <div className="us-chart-overlay"><Warning /><div><Status>STALE</Status><b>Поток не обновляется</b><span>Последний кадр остаётся видимым, но не считается текущим.</span></div></div>}
      </Panel>
      <aside className="us-run-side">
        <Panel title={`АДРЕС УЛК ${selected}`} meta="выбран кликом"><div className="us-metrics"><div><span>ТОЧКА</span><b>{typeof operation.point === "number" ? `${String(operation.point).replace(".", ",")} В` : "—"}</b></div><div><span>ФАКТ ЯЛК</span><b>{selectedData?.actual.toFixed(4)} В</b><small>DEMO stream</small></div><div><span>ОТКЛОНЕНИЕ</span><b>{selectedData?.deviation >= 0 ? "+" : ""}{selectedData?.deviation.toFixed(3)} % FS</b></div><div><span>ANALOG CODE</span><b>{selectedData?.code}</b><small>DEMO stream</small></div><div><span>SIGNAL</span><b>{selectedData?.signal ? "1" : "0"}</b></div><div><span>NOISE</span><b>{telemetry.history.length > 3 ? `${(Math.max(...telemetry.history.map(v => v.value)) - Math.min(...telemetry.history.map(v => v.value))).toFixed(4)} В` : "накапливается"}</b><small>без выдуманного verdict</small></div></div></Panel>
        <TrendChart history={telemetry.history} selected={selected} />
        <ConsumptionChart values={telemetry.consumption} />
      </aside>
    </div>
    <div className="us-run-command"><div><Status>{verdict}</Status><span>{stopped ? "Сеанс безопасно остановлен" : blocked ? "Продолжение заблокировано до восстановления достоверных данных" : operation.title}</span></div><div><Button disabled={stopped || blocked} onClick={() => setRunning(!running)}>{running ? <Pause /> : <Play />}{running ? "ПАУЗА" : "ПРОДОЛЖИТЬ"}</Button><Button danger onClick={safeStop}><Stop weight="fill" /> SAFE STOP</Button><Button primary disabled={blocked || stopped} onClick={() => onReport({ verdict: faultMode === "not-normal" || faultMode === "overload-fail" ? "НЕ НОРМА" : "НОРМА", operation: operation.title })}><FileText /> ЗАВЕРШИТЬ / ОТЧЁТ</Button></div></div>
    {scenarioOpen && <ScenarioDrawer faultMode={faultMode} setFaultMode={setFaultMode} jump={jump} close={() => setScenarioOpen(false)} onManual={() => { setManual(true); setRunning(false); }} />}
    {manual && <ManualResistor onConfirm={finishManual} onCancel={() => { setManual(false); setRunning(true); }} />}
  </main>;
}

function ScenarioDrawer({ faultMode, setFaultMode, jump, close, onManual }) {
  return <aside className="us-scenario-drawer"><header><div><span>ТОЛЬКО ПРОТОТИП</span><b>Нештатные сценарии</b></div><button className="us-icon-btn" onClick={close}><X /></button></header><p>Эта панель не является операторским UI. Она нужна, чтобы руками воспроизвести все состояния без ожидания стенда.</p><div className="us-scenario-group"><span>СОСТОЯНИЕ ДАННЫХ</span>{[["normal","НОРМА"],["not-normal","НЕ НОРМА · выбранный адрес"],["stale","STALE DATA"],["stand-error","ОШИБКА СТЕНДА"],["overload-fail","НЕ НОРМА · overload"]].map(([id,label]) => <button className={faultMode === id ? "active" : ""} key={id} onClick={() => setFaultMode(id)}>{label}</button>)}</div><div className="us-scenario-group"><span>ПЕРЕЙТИ К ОПЕРАЦИИ</span>{[["yalk-mid","ЯЛК · 3,1 В"],["yalk-contact","КОНТАКТЫ"],["yalk-overload","±12 В"],["ytp","ЯТП / Р4831"],["yvp","ЯВП"]].map(([id,label]) => <button key={id} onClick={() => { jump(id); if (id === "ytp") window.setTimeout(onManual, 120); }}>{label}</button>)}</div></aside>;
}

function ProductionReport({ product, result, back }) {
  const verdict = result?.verdict || product.lastResult || "НОРМА";
  const demoRows = [
    ["ЯЛК · 80 рабочих адресов", "0 / 3,1 / 6,2 В", verdict === "НЕ НОРМА" ? "1 отклонение · DEMO" : "без отклонений · DEMO", verdict],
    ["ЯЛК · контактные пороги", "0 / 0,9 / 2,5 В", "80 адресов · DEMO", "НОРМА"],
    ["ЯЛК · перегрузка ±12 В", "88 физических каналов", "|Δcode| ≤ 2 · DEMO", "НОРМА"],
    ["ЯТП", "0 / 120 / 240 Ω", "30 каналов · DEMO", "НОРМА"],
    ["ЯВП-8", "0,15…4000 Гц", "UI готов · тракт commissioning", "НЕПОЛНАЯ ПРОВЕРКА"],
  ];
  const trace = Array.from({ length: 70 }, (_, i) => ({ value: 3.1 + Math.sin(i * .35) * .004 + Math.sin(i * .13) * .0015 }));
  const current = Array.from({ length: 70 }, (_, i) => ({ current: .285 + Math.sin(i * .22) * .018, voltage: i < 10 ? 24 : i > 50 ? 35 : 27 }));
  return <main className="us-page">
    <div className="us-title-row"><div><span>ПРОИЗВОДСТВЕННАЯ ВЕДОМОСТЬ</span><h1>{product.serial} · {product.stage}</h1><p>Графики и фоновая динамика сохраняются в производственном представлении результата.</p></div><Status>{verdict}</Status></div>
    <Panel title="РЕЗУЛЬТАТЫ ПРОЦЕДУР"><div className="us-report-table"><div className="head"><span>ПРОВЕРКА</span><span>МЕТОД</span><span>РЕЗУЛЬТАТ</span><span>VERDICT</span></div>{demoRows.map((row) => <div key={row[0]}><b>{row[0]}</b><span>{row[1]}</span><span>{row[2]}</span><Status>{row[3]}</Status></div>)}</div></Panel>
    <div className="us-report-charts"><TrendChart history={trace} selected={47} /><ConsumptionChart values={current} /><Panel title="ФОНОВАЯ ДИНАМИКА ПОСЛЕ ТЕСТА"><BackgroundHeat rows={Array.from({ length: 18 }, (_, r) => YALK_ADDRESSES.map((_, i) => Math.sin(i * .2 + r * .35) * .05))} /></Panel></div>
    <div className="us-report-actions"><Button onClick={back}><ArrowLeft /> НАЗАД</Button><Button><FileText /> ЭКСПОРТ · ПРОТОТИП</Button></div>
  </main>;
}

function TuEntry({ productNumber, setProductNumber, state, setState, goRun }) {
  const startPrep = () => { setState("preparing"); window.setTimeout(() => setState("ready"), 1200); };
  return <main className="us-page us-tu-entry"><div className="us-title-row"><div><span>ПРИЁМО-СДАТОЧНАЯ ПРОВЕРКА ПО ТУ</span><h1>УБСИ</h1><p>Никакого production stage. Номер блока → автоматическая подготовка стенда → единый нормативный run.</p></div></div><Panel className="us-tu-card"><label className="us-field"><span>БЛОК / УБСИ №</span><input value={productNumber} onChange={(e) => setProductNumber(e.target.value)} placeholder="Введите номер" /></label>{state === "idle" && <Button primary disabled={!productNumber.trim()} onClick={startPrep}><ShieldCheck /> ПОДГОТОВИТЬ СТЕНД</Button>}{state === "preparing" && <div className="us-preparing"><i /><div><b>ПОДГОТОВКА СТЕНДА...</b><span>Низкоуровневые RS485 / ИСД / В7 / АКИП в штатном UI не раскрываются.</span></div></div>}{state === "ready" && <div className="us-tu-ready"><CheckCircle weight="fill" /><div><b>СТЕНД ГОТОВ</b><span>Можно запускать нормативную проверку.</span></div><Button primary onClick={goRun}><Play /> НАЧАТЬ ПРОВЕРКУ</Button></div>}</Panel></main>;
}

const TU_CHECKS = ["Подготовка стенда", "Питание / потребление", "ЯЛК", "ЯТП", "ЯВП", "Безопасный сброс"];
function TuRun({ number, goReport }) {
  const [index, setIndex] = useState(2);
  const [manual, setManual] = useState(false);
  const [stopped, setStopped] = useState(false);
  const [standError, setStandError] = useState(false);
  useEffect(() => { if (stopped || standError || manual) return undefined; const timer = window.setInterval(() => setIndex((v) => Math.min(v + 1, TU_CHECKS.length - 1)), 7000); return () => window.clearInterval(timer); }, [manual, standError, stopped]);
  return <main className="us-run-page us-tu-run"><div className="us-run-top"><div><span>ПСИ ПО ТУ · ЕДИНЫЙ RUN</span><h1>{number}</h1><p>Текущая проверка: <b>{TU_CHECKS[index]}</b></p></div><Status>{stopped ? "ОСТАНОВЛЕНО" : standError ? "ОШИБКА СТЕНДА" : manual ? "ТРЕБУЕТСЯ ДЕЙСТВИЕ" : "ВЫПОЛНЯЕТСЯ"}</Status></div><div className="us-tu-progress"><div className="us-tu-progress__bar"><i style={{ width: `${((index + 1) / TU_CHECKS.length) * 100}%` }} /></div><div>{TU_CHECKS.map((item, i) => <span className={i < index ? "done" : i === index ? "current" : ""} key={item}>{item}</span>)}</div></div><Panel title="ХОД ПРОВЕРКИ" className="us-tu-live"><div className="us-tu-live-main"><Waveform /><div><span>ТЕКУЩАЯ ОПЕРАЦИЯ</span><h2>{TU_CHECKS[index]}</h2><p>{standError ? "Достоверное измерение получить нельзя; техническая ошибка не превращается в НЕ НОРМА изделия." : "Процедура выполняется автоматически. Технические таблицы оборудования скрыты."}</p></div></div><div className="us-tu-demo-controls"><span>ПРОТОТИП:</span><Button onClick={() => { setIndex(3); setManual(true); }}>Р4831</Button><Button onClick={() => setStandError(!standError)}>ОШИБКА СТЕНДА</Button><Button onClick={() => setIndex(Math.min(index + 1, TU_CHECKS.length - 1))}>СЛЕДУЮЩАЯ ПРОВЕРКА</Button></div></Panel><div className="us-run-command"><div><span>Общий прогресс: {Math.round(((index + 1) / TU_CHECKS.length) * 100)}%</span></div><div><Button danger onClick={() => setStopped(true)}><Stop weight="fill" /> SAFE STOP</Button><Button primary disabled={stopped || standError} onClick={() => goReport({ verdict: "НОРМА" })}><FileText /> ЗАВЕРШИТЬ / ПРОТОКОЛ</Button></div></div>{manual && <ManualResistor onConfirm={() => { setManual(false); setIndex(4); }} />}</main>;
}

function TuReport({ number, result, back }) {
  return <main className="us-page us-tu-report-page"><div className="us-title-row"><div><span>ПСИ ПО ТУ</span><h1>Итоговый протокол</h1><p>Представление намеренно короткое: без графиков, без технических версий и без цветного dashboard.</p></div></div><article className="us-tu-paper"><h2>ПРОТОКОЛ ПРИЁМО-СДАТОЧНОЙ ПРОВЕРКИ</h2><dl><dt>Изделие</dt><dd>{number}</dd><dt>Проверка</dt><dd>УБСИ · текущая утверждённая методика</dd><dt>Дата</dt><dd>DEMO</dd><dt>Результат</dt><dd><b>{result?.verdict || "НОРМА"}</b></dd></dl><table><thead><tr><th>Раздел</th><th>Результат</th></tr></thead><tbody>{["Питание / потребление", "ЯЛК", "ЯТП", "ЯВП", "Безопасный сброс"].map((item) => <tr key={item}><td>{item}</td><td>НОРМА · DEMO</td></tr>)}</tbody></table><div className="us-signatures"><span>Оператор __________________</span><span>Дата __________________</span></div></article><div className="us-report-actions"><Button onClick={back}><ArrowLeft /> НАЗАД</Button><Button><FileText /> ПЕЧАТЬ · ПРОТОТИП</Button></div></main>;
}

function Admin({ products, onSelect, onReplaceCell }) {
  const [active, setActive] = useState(products[0]);
  useEffect(() => { if (!products.find((p) => p.id === active?.id)) setActive(products[0]); }, [active, products]);
  return <main className="us-page"><div className="us-title-row"><div><span>АДМИНИСТРИРОВАНИЕ</span><h1>УБСИ · изделия и состав</h1><p>Только предметные эксплуатационные действия текущей поставки.</p></div></div><div className="us-admin-grid"><Panel title="ИЗДЕЛИЯ"><div className="us-admin-products">{products.map((item) => <button className={active?.id === item.id ? "active" : ""} key={item.id} onClick={() => setActive(item)}><div><b>{item.serial}</b><span>{item.stage}</span></div><Status>{item.lastResult}</Status></button>)}</div></Panel>{active && <Panel title={`СОСТАВ · ${active.serial}`}><div className="us-cell-list">{Object.entries(active.cells).map(([cell, sn]) => <div className="us-cell-row" key={cell}><div><b>{cell}</b><span>SN {sn || "не задан"}</span></div><div className="us-cell-retest"><span>После замены</span><b>{RETEST_RULES[cell]}</b></div><Button onClick={() => { onSelect(active); onReplaceCell(cell); }}><ArrowsClockwise /> ЗАМЕНИТЬ</Button></div>)}</div></Panel>}<Panel title="ИСТОРИЯ"><div className="us-admin-actions"><button><FileText /><div><b>PRODUCTION RUNS</b><span>ведомости и сохранённая динамика</span></div><ArrowRight /></button><button><ShieldCheck /><div><b>TU RUNS</b><span>короткие нормативные протоколы</span></div><ArrowRight /></button><button><Gear /><div><b>СОСТОЯНИЕ СТЕНДА</b><span>эксплуатационный итог без raw-диагностики</span></div><ArrowRight /></button></div></Panel></div></main>;
}

function EngineeringDrawer({ open, close }) {
  if (!open) return null;
  return <aside className="us-engineering-drawer"><header><div><span>F12</span><b>Инженерный слой</b></div><button className="us-icon-btn" onClick={close}><X /></button></header><div className="us-eng-grid"><div><span>ЯЛК stream</span><b>mode 6 · DEMO</b></div><div><span>Калибровка</span><b>97 / 99</b></div><div><span>ЯЛК addresses</span><b>80 confirmed</b></div><div><span>Overload</span><b>88 physical · ±2 code</b></div><div><span>ЯВП binding</span><b>commissioning required</b></div></div><p>Этот слой не меняет Production↔TU и не является повышением прав. В целевом ПО здесь будут low-level причины и диагностика.</p></aside>;
}

export function UbsiSupplyApp() {
  const [route, setRoute] = useState("home");
  const [products, setProducts] = useState(INITIAL_PRODUCTS);
  const [product, setProduct] = useState(INITIAL_PRODUCTS[0]);
  const [creating, setCreating] = useState(false);
  const [replaceCell, setReplaceCell] = useState(null);
  const [engineering, setEngineering] = useState(false);
  const [productionResult, setProductionResult] = useState(null);
  const [tuNumber, setTuNumber] = useState("");
  const [tuState, setTuState] = useState("idle");
  const [tuResult, setTuResult] = useState(null);

  useEffect(() => {
    const handler = (event) => { if (event.key === "F12") { event.preventDefault(); setEngineering((v) => !v); } if (event.key === "Escape") { setEngineering(false); setCreating(false); setReplaceCell(null); } };
    window.addEventListener("keydown", handler); return () => window.removeEventListener("keydown", handler);
  }, []);

  const updateProduct = (next) => { setProduct(next); setProducts((items) => items.map((item) => item.id === next.id ? next : item)); };
  const createProduct = (serial) => { const next = { id: `ubsi-${Date.now()}`, serial, stage: "Первичное", lastResult: "ГОТОВО", updated: "только что · DEMO", cells: { "ЯЛК-96": "", "ЯТП": "", "ЯВП-8": "", "ЯП-П": "" } }; setProducts((items) => [next, ...items]); setProduct(next); setCreating(false); setRoute("production-workspace"); };
  const saveCell = (cell, serial) => updateProduct({ ...product, cells: { ...product.cells, [cell]: serial } });
  const selectProduct = (item) => { setProduct(item); setRoute("production-workspace"); };
  const back = () => {
    const map = { production: "home", "production-workspace": "production", "production-run": "production-workspace", "production-report": "production-workspace", tu: "home", "tu-run": "tu", "tu-report": "tu", admin: "home" };
    setRoute(map[route] || "home");
  };

  let title = "КТМА";
  let subtitle = "УБСИ · интерактивный прототип";
  if (route.startsWith("production")) { title = "ПРОИЗВОДСТВО"; subtitle = product?.serial || "УБСИ"; }
  else if (route.startsWith("tu")) { title = "ПРИЁМО-СДАТОЧНАЯ ПРОВЕРКА ПО ТУ"; subtitle = tuNumber || "УБСИ"; }
  else if (route === "admin") { title = "АДМИНИСТРИРОВАНИЕ"; subtitle = "изделия · состав · история"; }

  return <div className="ubsi-supply-app">
    <AppHeader title={title} subtitle={subtitle} onBack={route !== "home" ? back : null} engineering={engineering} setEngineering={setEngineering} />
    {route === "home" && <Home go={setRoute} />}
    {route === "production" && <ProductionEntry products={products} onSelect={selectProduct} onCreate={() => setCreating(true)} />}
    {route === "production-workspace" && <ProductionWorkspace product={product} updateProduct={updateProduct} goRun={() => { setProductionResult(null); setRoute("production-run"); }} goReport={() => setRoute("production-report")} onReplaceCell={setReplaceCell} />}
    {route === "production-run" && <ProductionRun product={product} onExit={back} onReport={(result) => { setProductionResult(result); updateProduct({ ...product, lastResult: result.verdict }); setRoute("production-report"); }} />}
    {route === "production-report" && <ProductionReport product={product} result={productionResult} back={back} />}
    {route === "tu" && <TuEntry productNumber={tuNumber} setProductNumber={setTuNumber} state={tuState} setState={setTuState} goRun={() => setRoute("tu-run")} />}
    {route === "tu-run" && <TuRun number={tuNumber} goReport={(result) => { setTuResult(result); setRoute("tu-report"); }} />}
    {route === "tu-report" && <TuReport number={tuNumber} result={tuResult} back={back} />}
    {route === "admin" && <Admin products={products} onSelect={setProduct} onReplaceCell={setReplaceCell} />}
    {creating && <CreateProductDialog onClose={() => setCreating(false)} onCreate={createProduct} />}
    {replaceCell && <ReplaceCellDialog cell={replaceCell} product={product} onClose={() => setReplaceCell(null)} onSave={saveCell} onRun={() => { setReplaceCell(null); setRoute("production-workspace"); }} />}
    <EngineeringDrawer open={engineering} close={() => setEngineering(false)} />
  </div>;
}
