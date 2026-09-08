import { useEffect, useState } from "react";
import {
  ArrowLeft, ArrowRight, ArrowsClockwise, Bug, CheckCircle, Circuitry,
  FileText, Gear, MagnifyingGlass, Pause, Play, Plus, ShieldCheck,
  Stop, Warning, Wrench, X,
} from "@phosphor-icons/react";
import {
  CHECK_GROUPS, FULL_ROUTE, PRODUCTION_STAGES, YALK_CHANNELS,
  routeForSelection, useOperatorTelemetry,
} from "./operatorTelemetry.js";

const INITIAL_PRODUCTS = [
  { id: "u7", serial: "УБСИ-0007", stage: "Климат НУ", result: "НОРМА", cells: { "ЯЛК-96": "96-00427", "ЯТП": "ТП-00184", "ЯВП-8": "ВП-00041", "ЯП-П": "ПП-00018" } },
  { id: "u12", serial: "УБСИ-0012", stage: "Первичное", result: "НЕПОЛНАЯ ПРОВЕРКА", cells: { "ЯЛК-96": "96-00431", "ЯТП": "ТП-00192", "ЯВП-8": "ВП-00045", "ЯП-П": "ПП-00020" } },
];

function Button({ children, primary, danger, className = "", ...props }) {
  return <button className={`op-btn ${primary ? "primary" : ""} ${danger ? "danger" : ""} ${className}`} {...props}>{children}</button>;
}

function Status({ value }) {
  const tone = value === "НОРМА" || value === "ГОТОВО" ? "ok" : value === "НЕ НОРМА" ? "bad" : value === "ОШИБКА СТЕНДА" ? "error" : value === "ВЫПОЛНЯЕТСЯ" ? "run" : "warn";
  return <span className={`op-status ${tone}`}>{value}</span>;
}

function Panel({ title, meta, children, className = "" }) {
  return <section className={`op-panel ${className}`}><header><b>{title}</b>{meta && <span>{meta}</span>}</header>{children}</section>;
}

function Header({ route, title, subtitle, back, engineering, setEngineering }) {
  return <header className="op-header">
    <div className="op-brand"><Circuitry weight="fill" /><div><span>КТМА</span><b>УБСИ</b></div></div>
    <div className="op-context">{route !== "home" && <button onClick={back} aria-label="Назад"><ArrowLeft /></button>}<div><b>{title}</b><span>{subtitle}</span></div></div>
    <button className={engineering ? "active" : ""} onClick={() => setEngineering(!engineering)}><Wrench /> F12</button>
  </header>;
}

function Home({ go }) {
  return <main className="op-page op-home"><div><span>КТМА · ПОСТАВКА УБСИ</span><h1>Рабочая задача</h1></div><div className="op-home-grid">
    <button onClick={() => go("production")}><Circuitry /><div><b>ПРОИЗВОДСТВО</b><span>Полная или неполная проверка УБСИ</span></div><ArrowRight /></button>
    <button onClick={() => go("tu")}><ShieldCheck /><div><b>ПРОВЕРКА ПО ТУ</b><span>Полный нормативный маршрут</span></div><ArrowRight /></button>
    <button onClick={() => go("admin")}><Gear /><div><b>АДМИНИСТРИРОВАНИЕ</b><span>Состав, замены, история</span></div><ArrowRight /></button>
  </div></main>;
}

function ProductList({ products, select, create }) {
  const [query, setQuery] = useState("");
  const filtered = products.filter((item) => item.serial.toLowerCase().includes(query.toLowerCase()));
  return <main className="op-page"><div className="op-title"><span>ПРОИЗВОДСТВО</span><h1>УБСИ</h1><p>Найдите изделие или зарегистрируйте новый блок.</p></div>
    <Panel title="ИЗДЕЛИЯ"><div className="op-search"><label><MagnifyingGlass /><input value={query} onChange={(e) => setQuery(e.target.value)} placeholder="Номер УБСИ / SN" /></label><Button primary onClick={create}><Plus /> ЗАРЕГИСТРИРОВАТЬ</Button></div><div className="op-products"><div className="head"><span>ИЗДЕЛИЕ</span><span>ЭТАП</span><span>РЕЗУЛЬТАТ</span><span /></div>{filtered.map((item) => <button key={item.id} onClick={() => select(item)}><b>{item.serial}</b><span>{item.stage}</span><Status value={item.result} /><ArrowRight /></button>)}</div></Panel>
  </main>;
}

function CreateProduct({ close, save }) {
  const [serial, setSerial] = useState("");
  return <div className="op-modal-wrap"><div className="op-modal"><header><h2>Новый УБСИ</h2><button onClick={close}><X /></button></header><label><span>НОМЕР / SN</span><input autoFocus value={serial} onChange={(e) => setSerial(e.target.value)} /></label><footer><Button onClick={close}>ОТМЕНА</Button><Button primary disabled={!serial.trim()} onClick={() => save(serial.trim())}>СОЗДАТЬ</Button></footer></div></div>;
}

function CheckSelection({ product, updateProduct, start }) {
  const [full, setFull] = useState(true);
  const [groups, setGroups] = useState(["yalk-analog"]);
  const toggleGroup = (id) => setGroups((items) => items.includes(id) ? items.filter((x) => x !== id) : [...items, id]);
  const route = routeForSelection(full, groups);
  return <main className="op-page"><div className="op-title op-title-row"><div><span>ПРОИЗВОДСТВО</span><h1>{product.serial}</h1><p>УБСИ — изделие. ЯЛК, ЯТП и ЯВП являются частями его проверки, а не отдельными верхнеуровневыми сценариями.</p></div><Status value={product.result} /></div>
    <div className="op-setup-grid"><Panel title="ЭТАП ПРОИЗВОДСТВА"><div className="op-stage-grid">{PRODUCTION_STAGES.map((stage) => <button className={stage === product.stage ? "active" : ""} key={stage} onClick={() => updateProduct({ ...product, stage })}>{stage}{stage === product.stage && <CheckCircle weight="fill" />}</button>)}</div></Panel>
    <Panel title="СОСТАВ"><div className="op-composition">{Object.entries(product.cells).map(([cell, sn]) => <div key={cell}><span>{cell}</span><b>{sn || "не задан"}</b></div>)}</div></Panel></div>
    <Panel title="СОСТАВ ПРОВЕРКИ" meta={full ? "полная проверка" : "неполная проверка"}><div className="op-mode-choice"><button className={full ? "active" : ""} onClick={() => setFull(true)}><b>ПОЛНАЯ ПРОВЕРКА УБСИ</b><span>весь установленный маршрут</span></button><button className={!full ? "active" : ""} onClick={() => setFull(false)}><b>НЕПОЛНАЯ ПРОВЕРКА</b><span>выбрать только требуемые части</span></button></div>{!full && <div className="op-check-grid">{CHECK_GROUPS.map((item) => <label className={groups.includes(item.id) ? "checked" : ""} key={item.id}><input type="checkbox" checked={groups.includes(item.id)} onChange={() => toggleGroup(item.id)} /><div><b>{item.title}</b><span>{item.note || ""}</span></div></label>)}</div>}
      <div className="op-route-preview"><b>БУДЕТ ВЫПОЛНЕНО</b><ol>{route.map((item) => <li key={item.id}>{item.title}</li>)}</ol>{!full && groups.includes("yalk-analog") && <p>Поток и калибровка ЯЛК выполняются как служебные prerequisite выбранной аналоговой проверки.</p>}</div>
      <div className="op-start"><Button primary disabled={!route.length} onClick={() => start(route, full ? "ПОЛНАЯ ПРОВЕРКА УБСИ" : "НЕПОЛНАЯ ПРОВЕРКА")}><Play /> НАЧАТЬ</Button></div>
    </Panel>
  </main>;
}

function linePath(values, width, height, minV, maxV) {
  if (!values.length) return "";
  const span = Math.max(0.000001, maxV - minV);
  return values.map((value, index) => {
    const x = values.length === 1 ? width / 2 : index / (values.length - 1) * width;
    const y = height - (value - minV) / span * height;
    return `${index ? "L" : "M"}${x.toFixed(1)},${Math.max(0, Math.min(height, y)).toFixed(1)}`;
  }).join(" ");
}

function LiveAllChannels({ histories, current, selected, setSelected, overload, stale }) {
  const width = 900, height = 275;
  const allCurrent = overload ? current.map((item) => ({ channel: item.channel, volts: item.volts, deviation: item.deltaCode, failed: item.failed })) : current;
  const values = allCurrent.map((x) => x.volts);
  const center = values.length ? values.reduce((a, b) => a + b, 0) / values.length : 3.1;
  const minV = Math.max(0, center - 0.08), maxV = Math.min(6.3, center + 0.08);
  return <div className={`op-live ${stale ? "stale" : ""}`}>
    <div className="op-live-title"><div><b>ЖИВЫЕ ЗНАЧЕНИЯ ВСЕХ КАНАЛОВ, В</b><span>сырой поток → пересчёт в вольты · DEMO telemetry</span></div><span>{allCurrent.length} каналов</span></div>
    <svg viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none" role="img" aria-label="Живые колебания всех каналов">{[0,1,2,3,4].map((n) => <line key={n} x1="0" x2={width} y1={n * height / 4} y2={n * height / 4} className="grid" />)}{histories.slice(0, allCurrent.length).map((history, index) => history.length > 1 && <path key={allCurrent[index]?.channel ?? index} d={linePath(history, width, height, minV, maxV)} className={`${allCurrent[index]?.channel === selected ? "selected" : ""} ${allCurrent[index]?.failed ? "failed" : ""}`} />)}</svg>
    <div className="op-axis"><span>{maxV.toFixed(3)} В</span><span>{minV.toFixed(3)} В</span></div>
    <div className="op-channel-strip">{allCurrent.map((item) => <button title={`Канал ${item.channel}: ${item.volts.toFixed(4)} В`} className={`${item.channel === selected ? "selected" : ""} ${item.failed ? "failed" : ""}`} key={item.channel} onClick={() => setSelected(item.channel)}><span>{item.volts.toFixed(3)}</span><b>{item.channel}</b></button>)}</div>
  </div>;
}

function ResultOverview({ current, operation, selected, setSelected }) {
  if (!operation?.yalk) return <div className="op-empty">Для этой процедуры используется собственный измерительный тракт. В этой версии прототипа детально реализован ЯЛК.</div>;
  const rows = operation.overload ? current.map((item) => ({ channel: item.channel, value: `${item.volts.toFixed(4)} В`, detail: `${item.deltaCode >= 0 ? "+" : ""}${item.deltaCode} code`, failed: item.failed })) : current.map((item) => ({ channel: item.channel, value: `${item.volts.toFixed(4)} В`, detail: `${item.deviation >= 0 ? "+" : ""}${item.deviation.toFixed(3)} % FS`, failed: item.failed }));
  return <div className="op-result-overview"><div className="op-bars">{rows.map((item) => <button key={item.channel} className={`${item.channel === selected ? "selected" : ""} ${item.failed ? "failed" : ""}`} onClick={() => setSelected(item.channel)}><span>{item.value}</span><i style={{ height: `${Math.min(100, 12 + Math.abs(parseFloat(item.detail)) * 90)}%` }} /><b>{item.channel}</b></button>)}</div><div className="op-result-table"><div className="head"><span>КАНАЛ</span><span>ЗНАЧЕНИЕ</span><span>{operation.overload ? "Δ CODE" : "ОТКЛОНЕНИЕ"}</span><span>ИТОГ</span></div>{rows.slice(0, 88).map((item) => <button key={item.channel} onClick={() => setSelected(item.channel)}><b>{item.channel}</b><span>{item.value}</span><span>{item.detail}</span><Status value={item.failed ? "НЕ НОРМА" : "НОРМА"} /></button>)}</div></div>;
}

function Consumption({ values }) {
  const width = 320, height = 90;
  const current = values.map((item) => item.currentA);
  const d = linePath(current, width, height, 0.2, 0.42);
  const latest = values.at(-1);
  return <Panel title="ПОТРЕБЛЕНИЕ УБСИ" meta="граница 400 мА"><div className="op-consumption"><svg viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none"><line x1="0" x2={width} y1={height - (0.4 - 0.2) / 0.22 * height} y2={height - (0.4 - 0.2) / 0.22 * height} className="limit" />{d && <path d={d} />}</svg><div><span>U: <b>{latest ? `${latest.voltage} В` : "—"}</b></span><span>I: <b>{latest ? `${(latest.currentA * 1000).toFixed(0)} мА` : "—"}</b></span></div></div></Panel>;
}

function ManualR4831({ operation, confirm, cancel }) {
  return <div className="op-modal-wrap"><div className="op-modal op-manual"><header><div><span>РУЧНОЕ ДЕЙСТВИЕ</span><h2>Р4831 · {operation.pointOhm} Ω</h2></div><Warning weight="fill" /></header><p>Установите требуемое сопротивление на магазине Р4831 и подтвердите физическое действие.</p><footer><Button onClick={cancel}>ОТМЕНА</Button><Button primary onClick={confirm}>УСТАНОВЛЕНО · ПРОДОЛЖИТЬ</Button></footer></div></div>;
}

function ScenarioDrawer({ fault, setFault, close, jump, route }) {
  return <aside className="op-scenarios"><header><div><span>ТОЛЬКО ПРОТОТИП</span><b>Нештатные состояния</b></div><button onClick={close}><X /></button></header><p>Не часть операторского UI. Нужна для проверки UX без стенда.</p>{[["normal","НОРМА"],["not-normal","НЕ НОРМА"],["stale","STALE DATA"],["stand-error","ОШИБКА СТЕНДА"],["overload-fail","OVERLOAD FAIL"]].map(([id, label]) => <button className={fault === id ? "active" : ""} key={id} onClick={() => setFault(id)}>{label}</button>)}<hr /><span>ПЕРЕЙТИ К ШАГУ</span>{route.map((item, index) => <button key={`${item.id}-${index}`} onClick={() => jump(index)}>{item.title}</button>)}</aside>;
}

function Run({ product, route, scopeLabel, mode, finish }) {
  const [index, setIndex] = useState(0);
  const [selected, setSelected] = useState(YALK_CHANNELS[0]);
  const [running, setRunning] = useState(true);
  const [stopped, setStopped] = useState(false);
  const [fault, setFault] = useState("normal");
  const [scenarios, setScenarios] = useState(false);
  const [manual, setManual] = useState(false);
  const operation = route[index] || route[0];
  const telemetry = useOperatorTelemetry({ running: running && !stopped, operation, fault, selectedChannel: selected });
  const currentForView = operation?.overload ? telemetry.overload : telemetry.current;
  const blocked = stopped || fault === "stale" || fault === "stand-error";
  const verdict = stopped ? "ОСТАНОВЛЕНО" : fault === "stand-error" ? "ОШИБКА СТЕНДА" : fault === "not-normal" || fault === "overload-fail" ? "НЕ НОРМА" : "ВЫПОЛНЯЕТСЯ";

  useEffect(() => {
    setSelected(operation?.overload ? 1 : YALK_CHANNELS[0]);
    telemetry.resetHistory();
    if (operation?.manual) { setManual(true); setRunning(false); }
  }, [operation?.id]);

  const next = () => {
    if (index >= route.length - 1) return;
    setIndex((value) => value + 1);
    setRunning(true);
    setFault("normal");
  };
  const finishRun = () => finish({ verdict: fault === "not-normal" || fault === "overload-fail" ? "НЕ НОРМА" : fault === "stand-error" ? "НЕПОЛНАЯ ПРОВЕРКА" : "НОРМА", scopeLabel, histories: telemetry.history, operation });

  return <main className="op-run"><div className="op-run-head"><div><span>{mode === "tu" ? "ПРОВЕРКА ПО ТУ" : "ПРОИЗВОДСТВО"} · {scopeLabel}</span><h1>{product.serial}</h1><p>{operation?.title}</p></div><div><Status value={verdict} /><button className="op-proto-btn" onClick={() => setScenarios(!scenarios)}><Bug /> СЦЕНАРИИ</button></div></div>
    <div className="op-step-strip">{route.map((item, i) => <button className={`${i < index ? "done" : ""} ${i === index ? "current" : ""}`} key={`${item.id}-${i}`} onClick={() => { setIndex(i); setRunning(true); setFault("normal"); }}><span>{i + 1}</span><b>{item.title}</b></button>)}</div>
    <div className="op-run-layout"><div className="op-run-main">
      {operation?.yalk ? <Panel title="ПОТОК ЯЛК" meta={`${operation.overload ? 88 : 80} каналов · постоянная область во время проверки`}><LiveAllChannels histories={telemetry.history} current={currentForView} selected={selected} setSelected={setSelected} overload={operation.overload} stale={fault === "stale"} />{fault === "stand-error" && <div className="op-data-error"><Warning /><div><b>ОШИБКА СТЕНДА</b><span>Последнее значение остаётся на экране, но нормативный результат не формируется.</span></div></div>}</Panel> : <Panel title={operation?.title || "ПРОЦЕДУРА"}><div className="op-nonyalk"><b>{operation?.ytp ? "ЯТП · 30 каналов" : operation?.yvp ? "ЯВП-8" : operation?.group === "power" ? "Питание / потребление" : "Служебная операция"}</b><span>{operation?.yvp ? "ЯВП остаётся частью полной проверки УБСИ; отдельным верхнеуровневым сценарием не является." : "DEMO-представление текущей процедуры."}</span></div></Panel>}
      <Panel title="РЕЗУЛЬТАТ ТЕКУЩЕЙ ПРОВЕРКИ" meta={operation?.overload ? "критерий остальных каналов: |Δcode| ≤ 2" : operation?.point !== undefined ? `точка ${String(operation.point).replace(".", ",")} В` : ""}><ResultOverview current={currentForView} operation={operation} selected={selected} setSelected={setSelected} /></Panel>
    </div><aside className="op-run-side"><Panel title="ТЕКУЩИЙ ШАГ"><dl><dt>Проверка</dt><dd>{operation?.title}</dd><dt>Канал</dt><dd>{operation?.yalk ? selected : "—"}</dd>{operation?.overload && <><dt>Воздействие</dt><dd>{operation.polarity}</dd><dt>Перегружаемый</dt><dd>{telemetry.overload[0]?.stressedChannel}</dd></>}{operation?.contact && <><dt>Контакт</dt><dd>ожидается {operation.expectedSignal}</dd></>}</dl></Panel><Consumption values={telemetry.consumption} /><Panel title="СОСТАВ"><div className="op-composition compact">{Object.entries(product.cells).map(([cell, sn]) => <div key={cell}><span>{cell}</span><b>{sn}</b></div>)}</div></Panel></aside></div>
    <div className="op-command"><div><Status value={verdict} /><span>{blocked ? "Продолжение заблокировано до восстановления достоверных данных" : operation?.title}</span></div><div><Button disabled={stopped || fault === "stand-error" || fault === "stale"} onClick={() => setRunning(!running)}>{running ? <Pause /> : <Play />}{running ? "ПАУЗА" : "ПРОДОЛЖИТЬ"}</Button><Button danger onClick={() => { setStopped(true); setRunning(false); }}><Stop weight="fill" /> SAFE STOP</Button>{index < route.length - 1 ? <Button primary disabled={blocked} onClick={next}>СЛЕДУЮЩИЙ ШАГ <ArrowRight /></Button> : <Button primary disabled={blocked} onClick={finishRun}><FileText /> ЗАВЕРШИТЬ / ОТЧЁТ</Button>}</div></div>
    {scenarios && <ScenarioDrawer fault={fault} setFault={setFault} close={() => setScenarios(false)} route={route} jump={(i) => { setIndex(i); setRunning(true); setFault("normal"); }} />}
    {manual && <ManualR4831 operation={operation} cancel={() => { setManual(false); setRunning(true); }} confirm={() => { setManual(false); setRunning(true); next(); }} />}
  </main>;
}

function Report({ product, result, back, tu = false }) {
  const traces = result?.histories || YALK_CHANNELS.map(() => []);
  const dummyCurrent = YALK_CHANNELS.map((channel, index) => ({ channel, volts: traces[index]?.at(-1) ?? 3.1, deviation: 0, failed: false }));
  if (tu) return <main className="op-page op-tu-report"><article><h1>ПРОТОКОЛ ПРИЁМО-СДАТОЧНОЙ ПРОВЕРКИ</h1><dl><dt>Изделие</dt><dd>{product.serial}</dd><dt>Результат</dt><dd><b>{result?.verdict || "НОРМА"}</b></dd></dl><table><tbody>{["Питание / потребление", "ЯЛК", "ЯТП", "ЯВП", "Безопасный сброс"].map((name) => <tr key={name}><td>{name}</td><td>DEMO</td></tr>)}</tbody></table><p>Оператор ____________________</p></article><Button onClick={back}><ArrowLeft /> НАЗАД</Button></main>;
  return <main className="op-page"><div className="op-title op-title-row"><div><span>ПРОИЗВОДСТВЕННЫЙ ОТЧЁТ</span><h1>{product.serial}</h1><p>{result?.scopeLabel}</p></div><Status value={result?.verdict || "НОРМА"} /></div><Panel title="СОХРАНЁННЫЙ ПОТОК КАНАЛОВ" meta="визуальная проверка после теста"><LiveAllChannels histories={traces} current={dummyCurrent} selected={YALK_CHANNELS[0]} setSelected={() => {}} /></Panel><Panel title="ИТОГ"><div className="op-report-summary"><div><span>ЭТАП</span><b>{product.stage}</b></div><div><span>СОСТАВ ПРОВЕРКИ</span><b>{result?.scopeLabel}</b></div><div><span>РЕЗУЛЬТАТ</span><Status value={result?.verdict || "НОРМА"} /></div></div></Panel><Button onClick={back}><ArrowLeft /> НАЗАД</Button></main>;
}

function TuEntry({ start }) {
  const [serial, setSerial] = useState("УБСИ-0012");
  const [state, setState] = useState("idle");
  const prepare = () => { setState("preparing"); window.setTimeout(() => setState("ready"), 1000); };
  return <main className="op-page"><div className="op-title"><span>ПРОВЕРКА ПО ТУ</span><h1>УБСИ</h1><p>Полный нормативный маршрут. Без production stage и без выбора частей проверки.</p></div><Panel title="ЗАПУСК"><div className="op-tu-entry"><label><span>БЛОК / УБСИ №</span><input value={serial} onChange={(e) => setSerial(e.target.value)} /></label>{state === "idle" && <Button primary onClick={prepare}>ПОДГОТОВИТЬ СТЕНД</Button>}{state === "preparing" && <div className="op-preparing"><i /><b>ПОДГОТОВКА СТЕНДА...</b></div>}{state === "ready" && <div className="op-ready"><CheckCircle weight="fill" /><b>СТЕНД ГОТОВ</b><Button primary onClick={() => start(serial)}>НАЧАТЬ ПРОВЕРКУ</Button></div>}</div></Panel></main>;
}

function Admin({ products, replace }) {
  return <main className="op-page"><div className="op-title"><span>АДМИНИСТРИРОВАНИЕ</span><h1>Состав УБСИ</h1></div>{products.map((product) => <Panel key={product.id} title={product.serial} meta={product.stage}><div className="op-cell-list">{Object.entries(product.cells).map(([cell, sn]) => <div key={cell}><div><b>{cell}</b><span>SN {sn}</span></div><Button onClick={() => replace(product, cell)}><ArrowsClockwise /> ЗАМЕНИТЬ</Button></div>)}</div></Panel>)}</main>;
}

function ReplaceCell({ product, cell, close, save }) {
  const [serial, setSerial] = useState(product.cells[cell] || "");
  return <div className="op-modal-wrap"><div className="op-modal"><header><h2>Замена {cell}</h2><button onClick={close}><X /></button></header><label><span>НОВЫЙ SN</span><input value={serial} onChange={(e) => setSerial(e.target.value)} /></label><footer><Button onClick={close}>ОТМЕНА</Button><Button primary onClick={() => save(serial)}>СОХРАНИТЬ</Button></footer></div></div>;
}

function Engineering({ open, close }) {
  if (!open) return null;
  return <aside className="op-engineering"><header><b>F12 · инженерный слой</b><button onClick={close}><X /></button></header><p>Только здесь допустимы внутренние термины транспорта/адресации. Операторский UI их не показывает.</p><dl><dt>ЯЛК logical set</dt><dd>80 рабочих каналов</dd><dt>Overload</dt><dd>88 физических каналов</dd><dt>Калибровка</dt><dd>97 / 99</dd></dl></aside>;
}

export function UbsiOperatorApp() {
  const [route, setRoute] = useState("home");
  const [products, setProducts] = useState(INITIAL_PRODUCTS);
  const [product, setProduct] = useState(INITIAL_PRODUCTS[0]);
  const [runRoute, setRunRoute] = useState(FULL_ROUTE);
  const [scopeLabel, setScopeLabel] = useState("ПОЛНАЯ ПРОВЕРКА УБСИ");
  const [result, setResult] = useState(null);
  const [creating, setCreating] = useState(false);
  const [replacement, setReplacement] = useState(null);
  const [engineering, setEngineering] = useState(false);
  const [tuProduct, setTuProduct] = useState(null);

  useEffect(() => {
    const handler = (event) => {
      if (event.key === "F12") { event.preventDefault(); setEngineering((value) => !value); }
      if (event.key === "Escape") { setEngineering(false); setCreating(false); setReplacement(null); }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, []);

  const updateProduct = (next) => { setProduct(next); setProducts((items) => items.map((item) => item.id === next.id ? next : item)); };
  const startProduction = (nextRoute, label) => { setRunRoute(nextRoute); setScopeLabel(label); setResult(null); setRoute("production-run"); };
  const selectProduct = (item) => { setProduct(item); setRoute("production-setup"); };
  const createProduct = (serial) => { const next = { id: `u-${Date.now()}`, serial, stage: "Первичное", result: "ГОТОВО", cells: { "ЯЛК-96": "", "ЯТП": "", "ЯВП-8": "", "ЯП-П": "" } }; setProducts((items) => [next, ...items]); setProduct(next); setCreating(false); setRoute("production-setup"); };
  const back = () => { const map = { production: "home", "production-setup": "production", "production-run": "production-setup", "production-report": "production-setup", tu: "home", "tu-run": "tu", "tu-report": "tu", admin: "home" }; setRoute(map[route] || "home"); };

  let title = "КТМА · УБСИ", subtitle = "интерактивный операторский прототип";
  if (route.startsWith("production")) { title = "ПРОИЗВОДСТВО"; subtitle = product.serial; }
  if (route.startsWith("tu")) { title = "ПРОВЕРКА ПО ТУ"; subtitle = tuProduct?.serial || "УБСИ"; }
  if (route === "admin") { title = "АДМИНИСТРИРОВАНИЕ"; subtitle = "состав и история"; }

  return <div className="op-app"><Header route={route} title={title} subtitle={subtitle} back={back} engineering={engineering} setEngineering={setEngineering} />
    {route === "home" && <Home go={setRoute} />}
    {route === "production" && <ProductList products={products} select={selectProduct} create={() => setCreating(true)} />}
    {route === "production-setup" && <CheckSelection product={product} updateProduct={updateProduct} start={startProduction} />}
    {route === "production-run" && <Run product={product} route={runRoute} scopeLabel={scopeLabel} mode="production" finish={(next) => { setResult(next); updateProduct({ ...product, result: next.verdict }); setRoute("production-report"); }} />}
    {route === "production-report" && <Report product={product} result={result} back={back} />}
    {route === "tu" && <TuEntry start={(serial) => { const p = products.find((item) => item.serial === serial) || { ...INITIAL_PRODUCTS[0], id: "tu-temp", serial }; setTuProduct(p); setRunRoute(FULL_ROUTE); setScopeLabel("ПОЛНЫЙ МАРШРУТ ПО ТУ"); setRoute("tu-run"); }} />}
    {route === "tu-run" && tuProduct && <Run product={tuProduct} route={FULL_ROUTE} scopeLabel="ПОЛНЫЙ МАРШРУТ ПО ТУ" mode="tu" finish={(next) => { setResult(next); setRoute("tu-report"); }} />}
    {route === "tu-report" && tuProduct && <Report product={tuProduct} result={result} back={back} tu />}
    {route === "admin" && <Admin products={products} replace={(p, cell) => { setProduct(p); setReplacement({ product: p, cell }); }} />}
    {creating && <CreateProduct close={() => setCreating(false)} save={createProduct} />}
    {replacement && <ReplaceCell product={replacement.product} cell={replacement.cell} close={() => setReplacement(null)} save={(serial) => { const p = replacement.product; const next = { ...p, cells: { ...p.cells, [replacement.cell]: serial } }; setProducts((items) => items.map((item) => item.id === p.id ? next : item)); setProduct(next); setReplacement(null); }} />}
    <Engineering open={engineering} close={() => setEngineering(false)} />
  </div>;
}
