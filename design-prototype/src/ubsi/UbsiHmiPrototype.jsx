import { useEffect, useMemo, useState } from "react";
import {
  ArrowLeft, ArrowRight, ArrowsClockwise, Bug, CheckCircle, Circuitry,
  FileText, Gear, MagnifyingGlass, Pause, Play, Plus, Printer,
  ShieldCheck, Stop, Warning, Wrench, X,
} from "@phosphor-icons/react";
import {
  CHECK_GROUPS, FULL_ROUTE, PRODUCTION_STAGES, YALK_CHANNELS,
  routeForSelection, useOperatorTelemetry,
} from "./operatorTelemetry.js";

const PRODUCTS = [
  {
    id: "u7",
    serial: "УБСИ-468157-009",
    stage: "Климат НУ",
    result: "НОРМА",
    cells: { "ЯЛК-96": "ЯЛК-00427", "ЯТП": "ЯТП-00184", "ЯВП-8": "ЯВП-00041", "ЯП-П": "ЯПП-00018" },
  },
  {
    id: "u12",
    serial: "УБСИ-468157-012",
    stage: "Первичное",
    result: "НЕПОЛНАЯ ПРОВЕРКА",
    cells: { "ЯЛК-96": "ЯЛК-00431", "ЯТП": "ЯТП-00192", "ЯВП-8": "ЯВП-00045", "ЯП-П": "ЯПП-00020" },
  },
];

const YVP_FREQUENCIES = [0.15, 20, 250, 500, 1800, 2000, 4000];

function Btn({ children, primary, danger, compact, ...props }) {
  return <button className={`hmi-btn ${primary ? "primary" : ""} ${danger ? "danger" : ""} ${compact ? "compact" : ""}`} {...props}>{children}</button>;
}

function Status({ value }) {
  const tone = value === "НОРМА" || value === "ГОТОВО" || value === "ЗАВЕРШЕНО"
    ? "ok" : value === "НЕ НОРМА" ? "bad" : value === "ОШИБКА СТЕНДА"
      ? "error" : value === "ВЫПОЛНЯЕТСЯ" ? "run" : "warn";
  return <span className={`hmi-status ${tone}`}>{value}</span>;
}

function Panel({ title, meta, children, className = "" }) {
  return <section className={`hmi-panel ${className}`}><header><b>{title}</b>{meta && <span>{meta}</span>}</header>{children}</section>;
}

function AppHeader({ route, title, subtitle, back, engineering, setEngineering }) {
  return <header className="hmi-header">
    <div className="hmi-brand"><Circuitry weight="fill" /><div><b>Орбита</b><span>стендовый комплекс КТМА</span></div></div>
    <div className="hmi-context">{route !== "home" && <button onClick={back}><ArrowLeft /></button>}<div><b>{title}</b><span>{subtitle}</span></div></div>
    <div className="hmi-header-actions"><span className="stand-ready">● СТЕНД ГОТОВ</span><button className={engineering ? "active" : ""} onClick={() => setEngineering(!engineering)}><Wrench /> F12</button></div>
  </header>;
}

function Home({ go }) {
  return <main className="hmi-home">
    <div className="hmi-home-head"><span>КТМА · УБСИ</span><h1>Рабочий контур</h1><p>Текущая поставка: производство, приёмо-сдаточная проверка по ТУ и базовое администрирование.</p></div>
    <div className="hmi-home-actions">
      <button onClick={() => go("production")}><Circuitry /><div><b>ПРОИЗВОДСТВО</b><span>Регистрация изделия · полная / неполная проверка · отчёт</span></div><ArrowRight /></button>
      <button onClick={() => go("tu")}><ShieldCheck /><div><b>ПРИЁМО-СДАТОЧНАЯ ПРОВЕРКА ПО ТУ</b><span>Подготовка стенда · единый нормативный прогон · короткий протокол</span></div><ArrowRight /></button>
      <button onClick={() => go("admin")}><Gear /><div><b>АДМИНИСТРИРОВАНИЕ</b><span>Состав УБСИ · замены · история запусков</span></div><ArrowRight /></button>
    </div>
  </main>;
}

function Registry({ products, select, create }) {
  const [query, setQuery] = useState("");
  const rows = products.filter((p) => p.serial.toLowerCase().includes(query.toLowerCase()));
  return <main className="hmi-page">
    <div className="hmi-title"><span>ПРОИЗВОДСТВО</span><h1>УБСИ</h1><p>Выберите зарегистрированное изделие или зарегистрируйте новое.</p></div>
    <Panel title="РЕЕСТР ИЗДЕЛИЙ" meta={`${rows.length} записей`}>
      <div className="hmi-search"><label><MagnifyingGlass /><input value={query} onChange={(e) => setQuery(e.target.value)} placeholder="Номер УБСИ / SN" /></label><Btn primary onClick={create}><Plus /> ЗАРЕГИСТРИРОВАТЬ УБСИ</Btn></div>
      <div className="hmi-registry-table"><div className="head"><span>ИЗДЕЛИЕ</span><span>ЭТАП</span><span>СОСТАВ</span><span>ПОСЛЕДНИЙ РЕЗУЛЬТАТ</span><span /></div>{rows.map((p) => <button key={p.id} onClick={() => select(p)}><b>{p.serial}</b><span>{p.stage}</span><span>{Object.values(p.cells).filter(Boolean).length}/4 ячейки</span><Status value={p.result} /><ArrowRight /></button>)}</div>
    </Panel>
  </main>;
}

function ProductModal({ close, save }) {
  const [serial, setSerial] = useState("");
  const [cells, setCells] = useState({ "ЯЛК-96": "", "ЯТП": "", "ЯВП-8": "", "ЯП-П": "" });
  return <div className="hmi-modal-bg"><div className="hmi-modal wide"><header><div><span>РЕГИСТРАЦИЯ</span><h2>Новый УБСИ</h2></div><button onClick={close}><X /></button></header>
    <label><span>НОМЕР / SN УБСИ</span><input autoFocus value={serial} onChange={(e) => setSerial(e.target.value)} placeholder="УБСИ-..." /></label>
    <div className="hmi-form-grid">{Object.keys(cells).map((name) => <label key={name}><span>{name} · SN</span><input value={cells[name]} onChange={(e) => setCells({ ...cells, [name]: e.target.value })} placeholder="можно заполнить позже" /></label>)}</div>
    <footer><Btn onClick={close}>ОТМЕНА</Btn><Btn primary disabled={!serial.trim()} onClick={() => save(serial.trim(), cells)}>ЗАРЕГИСТРИРОВАТЬ</Btn></footer>
  </div></div>;
}

function Setup({ product, update, start }) {
  const [full, setFull] = useState(true);
  const [groups, setGroups] = useState(["yalk-analog"]);
  const route = routeForSelection(full, groups);
  const toggle = (id) => setGroups((xs) => xs.includes(id) ? xs.filter((x) => x !== id) : [...xs, id]);
  return <main className="hmi-page">
    <div className="hmi-title hmi-title-row"><div><span>ПРОИЗВОДСТВО</span><h1>{product.serial}</h1><p>УБСИ — изделие. ЯЛК, ЯТП, ЯВП и питание — части маршрута проверки.</p></div><Status value={product.result} /></div>
    <div className="hmi-setup-grid">
      <Panel title="ЭТАП ПРОИЗВОДСТВА"><div className="hmi-stage-grid">{PRODUCTION_STAGES.map((stage) => <button key={stage} className={stage === product.stage ? "active" : ""} onClick={() => update({ ...product, stage })}>{stage}{stage === product.stage && <CheckCircle weight="fill" />}</button>)}</div></Panel>
      <Panel title="СОСТАВ УБСИ"><div className="hmi-composition">{Object.entries(product.cells).map(([name, sn]) => <div key={name}><span>{name}</span><b>{sn || "не задан"}</b></div>)}</div></Panel>
    </div>
    <Panel title="СОСТАВ ПРОВЕРКИ" meta={full ? "полная проверка УБСИ" : "неполная проверка"}>
      <div className="hmi-mode-choice"><button className={full ? "active" : ""} onClick={() => setFull(true)}><b>ПОЛНАЯ ПРОВЕРКА УБСИ</b><span>последовательно пройти весь установленный маршрут</span></button><button className={!full ? "active" : ""} onClick={() => setFull(false)}><b>НЕПОЛНАЯ ПРОВЕРКА</b><span>выбрать только необходимые части после ремонта / замены</span></button></div>
      {!full && <div className="hmi-check-grid">{CHECK_GROUPS.map((g) => <label key={g.id} className={groups.includes(g.id) ? "checked" : ""}><input type="checkbox" checked={groups.includes(g.id)} onChange={() => toggle(g.id)} /><div><b>{g.title}</b><span>{g.note || ""}</span></div></label>)}</div>}
      <div className="hmi-route-preview"><b>МАРШРУТ</b><ol>{route.map((r) => <li key={r.id}>{r.title}</li>)}</ol>{!full && groups.some((x) => x.startsWith("yalk")) && <p>Подготовка потока и калибровка ЯЛК добавляются автоматически как prerequisite.</p>}</div>
      <div className="hmi-actions-right"><Btn primary disabled={!route.length} onClick={() => start(route, full ? "ПОЛНАЯ ПРОВЕРКА УБСИ" : "НЕПОЛНАЯ ПРОВЕРКА")}><Play /> НАЧАТЬ ПРОВЕРКУ</Btn></div>
    </Panel>
  </main>;
}

function linePath(values, width, height, min, max) {
  if (!values?.length) return "";
  const span = Math.max(1e-9, max - min);
  return values.map((v, i) => {
    const x = values.length === 1 ? width / 2 : i / (values.length - 1) * width;
    const y = height - (v - min) / span * height;
    return `${i ? "L" : "M"}${x.toFixed(1)},${Math.max(0, Math.min(height, y)).toFixed(1)}`;
  }).join(" ");
}

function MiniTrace({ values, min, max, bad }) {
  const d = linePath(values?.length ? values : [(min + max) / 2], 70, 22, min, max);
  return <svg className={`hmi-mini-trace ${bad ? "bad" : ""}`} viewBox="0 0 70 22" preserveAspectRatio="none"><path d={d} /></svg>;
}

function LiveMatrix({ histories, current, selected, setSelected, stale }) {
  const all = [];
  histories.forEach((h, i) => all.push(...(h?.length ? h : [current[i]?.volts ?? 0])));
  const low = all.length ? Math.min(...all) : 0;
  const high = all.length ? Math.max(...all) : 6.2;
  const center = (low + high) / 2;
  const span = Math.max(0.01, high - low) * 1.35;
  const min = center - span / 2, max = center + span / 2;
  return <div className={`hmi-live-matrix ${stale ? "stale" : ""}`}>
    <div className="hmi-live-head"><div><b>СЫРЫЕ КОЛЕБАНИЯ ВСЕХ КАНАЛОВ · В</b><span>свежие отсчёты → пересчёт в вольты · общий масштаб · DEMO DATA</span></div><span>{current.length} каналов</span></div>
    {stale && <div className="hmi-stale"><Warning /> ДАННЫЕ УСТАРЕЛИ — отображение заморожено</div>}
    <div className="hmi-live-grid">{current.map((item, i) => <button key={item.channel} className={`${selected === item.channel ? "selected" : ""} ${item.failed ? "bad" : ""}`} onClick={() => setSelected(item.channel)}><span>К{item.channel}</span><MiniTrace values={histories[i]} min={min} max={max} bad={item.failed} /><b>{item.volts.toFixed(3)}</b></button>)}</div>
    <div className="hmi-live-scale"><span>{max.toFixed(4)} В</span><span>единый масштаб для визуального сравнения</span><span>{min.toFixed(4)} В</span></div>
  </div>;
}

function SelectedTrace({ histories, current, selected, operation }) {
  const i = Math.max(0, current.findIndex((x) => x.channel === selected));
  const item = current[i] || current[0];
  const values = histories[i]?.length ? histories[i] : [item?.volts ?? 0];
  const target = typeof operation?.point === "number" ? operation.point : item?.volts ?? 0;
  const low = Math.min(...values, target) - 0.015, high = Math.max(...values, target) + 0.015;
  const d = linePath(values, 900, 150, low, high);
  const ty = 150 - (target - low) / Math.max(1e-9, high - low) * 150;
  return <div className="hmi-selected-trace"><div className="hmi-selected-head"><div><b>Канал {item?.channel ?? "—"} · динамика свежих кадров</b><span>{operation?.title}</span></div><div><strong>{item ? `${item.volts.toFixed(4)} В` : "—"}</strong><span>{item ? `${item.deviation >= 0 ? "+" : ""}${item.deviation.toFixed(3)} % FS` : ""}</span></div></div>
    <div className="hmi-big-chart"><svg viewBox="0 0 900 150" preserveAspectRatio="none">{[0,1,2,3].map((n) => <line key={n} className="grid" x1="0" x2="900" y1={n * 50} y2={n * 50} />)}<line className="reference" x1="0" x2="900" y1={ty} y2={ty} /><path className={item?.failed ? "bad" : ""} d={d} /></svg><div className="legend"><span>эталон</span><span>измерение</span></div></div>
  </div>;
}

function Histogram({ current, operation, selected, setSelected }) {
  const overload = operation?.overload;
  const contact = operation?.contact;
  const rows = current.map((item) => {
    const metric = overload ? item.deltaCode : contact ? item.signal : item.deviation;
    const label = overload ? `${item.deltaCode >= 0 ? "+" : ""}${item.deltaCode}` : contact ? String(item.signal) : `${item.deviation >= 0 ? "+" : ""}${item.deviation.toFixed(2)}`;
    const top = overload ? `${item.volts.toFixed(3)} В` : `${item.volts.toFixed(3)} В`;
    return { ...item, metric, label, top };
  });
  const maxAbs = Math.max(0.01, ...rows.map((x) => Math.abs(x.metric || 0)));
  return <div className="hmi-hist-wrap"><div className="hmi-hist-head"><div><b>ВСЕ КАНАЛЫ · {operation?.title || "текущая проверка"}</b><span>{overload ? "Δcode относительно baseline" : contact ? "контактное состояние и измеренное В" : "отклонение от текущей точки · значения над столбцами"}</span></div><div><span>легенда:</span><b> зелёный — норма · красный — не норма</b></div></div>
    <div className="hmi-hist-scroll"><div className="hmi-histogram">{rows.map((item) => {
      const height = 12 + Math.abs(item.metric || 0) / maxAbs * 80;
      return <button key={item.channel} className={`${selected === item.channel ? "selected" : ""} ${item.failed ? "bad" : ""}`} onClick={() => setSelected(item.channel)} title={`Канал ${item.channel} · ${item.top} · ${item.label}`}><span className="value">{item.top.replace(" В", "")}</span><i style={{ height: `${height}%` }} /><span className="metric">{item.label}</span><b>{item.channel}</b></button>;
    })}</div></div>
  </div>;
}

function ResultTable({ current, operation, selected, setSelected }) {
  const target = typeof operation?.point === "number" ? operation.point : null;
  return <div className="hmi-result-table"><div className="head"><span>КАНАЛ</span><span>ТОЧКА</span><span>ЗАДАНО</span><span>RAW</span><span>ИЗМЕРЕНО, В</span><span>ОТКЛОНЕНИЕ</span><span>СИГНАЛ</span><span>ИТОГ</span></div><div className="body">{current.map((item) => <button key={item.channel} className={selected === item.channel ? "selected" : ""} onClick={() => setSelected(item.channel)}><b>{item.channel}</b><span>{operation?.overload ? operation.polarity : target === null ? "—" : `${String(target).replace(".", ",")} В`}</span><span>{target === null ? "—" : `${target.toFixed(1)} В`}</span><span>{item.raw ?? "—"}</span><span>{item.volts.toFixed(4)}</span><span>{operation?.overload ? `${item.deltaCode >= 0 ? "+" : ""}${item.deltaCode} code` : `${item.deviation >= 0 ? "+" : ""}${item.deviation.toFixed(3)} % FS`}</span><span>{item.signal ?? "—"}</span><Status value={item.failed ? "НЕ НОРМА" : "НОРМА"} /></button>)}</div></div>;
}

function YtpView({ tick, point = 0 }) {
  const rows = Array.from({ length: 30 }, (_, i) => ({ channel: i + 1, value: point + Math.sin(tick * .17 + i * .41) * .12 }));
  return <div className="hmi-special"><div className="hmi-special-head"><div><b>ЯТП · {point} Ω</b><span>30 каналов · DEMO DATA</span></div><strong>Р4831</strong></div><div className="hmi-ytp-grid">{rows.map((r) => <div key={r.channel}><span>К{r.channel}</span><b>{r.value.toFixed(2)} Ω</b><MiniTrace values={[r.value-.04,r.value+.03,r.value-.02,r.value]} min={point-.3} max={point+.3} /></div>)}</div></div>;
}

function YvpView({ tick }) {
  const [freq, setFreq] = useState(20);
  const rows = Array.from({ length: 8 }, (_, i) => ({ channel: i + 1, value: 1 + Math.sin(i * .61 + tick * .09) * .015 }));
  return <div className="hmi-special"><div className="hmi-special-head"><div><b>ЯВП-8 · АЧХ / коэффициент</b><span>Измерительные значения DEMO; нормативный verdict не выдумывается</span></div><strong>{freq} Hz</strong></div><div className="hmi-frequency">{YVP_FREQUENCIES.map((f) => <button key={f} className={freq === f ? "active" : ""} onClick={() => setFreq(f)}>{f} Hz</button>)}</div><div className="hmi-yvp-grid">{rows.map((r) => <div key={r.channel}><span>Вход {r.channel}</span><b>{r.value.toFixed(4)}</b><span>DEMO</span></div>)}</div></div>;
}

function Consumption({ values }) {
  const latest = values.at(-1);
  const arr = values.map((x) => x.currentA);
  const d = linePath(arr, 380, 80, .2, .42);
  return <div className="hmi-consumption"><div><b>ПОТРЕБЛЕНИЕ УБСИ</b><span>граница 400 мА</span></div><svg viewBox="0 0 380 80" preserveAspectRatio="none"><line x1="0" x2="380" y1="8" y2="8" className="limit" />{d && <path d={d} />}</svg><footer><span>U <b>{latest ? `${latest.voltage} В` : "—"}</b></span><span>I <b>{latest ? `${Math.round(latest.currentA*1000)} мА` : "—"}</b></span></footer></div>;
}

function StepList({ route, index, done, jump }) {
  return <div className="hmi-step-list"><div className="head"><span>ЭТАП / ПРОВЕРКА</span><span>ИТОГ</span></div>{route.map((r, i) => <button key={`${r.id}-${i}`} className={`${i === index ? "current" : ""} ${done.has(i) ? "done" : ""}`} onClick={() => jump(i)}><div><span>{String(i+1).padStart(2,"0")}</span><b>{r.title}</b></div><span>{i === index ? "ВЫПОЛНЯЕТСЯ" : done.has(i) ? "ЗАВЕРШЕНО" : "—"}</span></button>)}</div>;
}

function Readiness({ fault, tu }) {
  if (tu) return <div className="hmi-tu-ready"><CheckCircle weight="fill" /><div><b>СТЕНД ГОТОВ</b><span>низкоуровневые детали доступны через F12</span></div></div>;
  const bad = fault === "stand-error";
  const rows = [["Адаптер", bad ? "НЕ ГОТОВО" : "ГОТОВО"],["ИСД", bad ? "НЕ ГОТОВО" : "ГОТОВО"],["АКИП-1160/6","ГОТОВО"],["Поток данных", fault === "stale" ? "STALE" : "ГОТОВО"]];
  return <div className="hmi-readiness"><div className="head"><span>УСТРОЙСТВО</span><span>СОСТОЯНИЕ</span></div>{rows.map(([n,s]) => <div key={n}><span>{n}</span><b className={s === "ГОТОВО" ? "ok" : "bad"}>{s}</b></div>)}</div>;
}

function ManualR4831({ operation, close, confirm }) {
  const [actual, setActual] = useState(String(operation.pointOhm ?? 120).replace(".", ","));
  const [checked, setChecked] = useState(false);
  return <div className="hmi-modal-bg"><div className="hmi-modal"><header><div><span>РУЧНОЕ ДЕЙСТВИЕ</span><h2>Р4831 · {operation.pointOhm} Ω</h2></div><Warning weight="fill" /></header><p>Установите сопротивление физически, внесите фактическое значение и подтвердите действие оператора.</p><label><span>ФАКТИЧЕСКИ, Ω</span><input value={actual} onChange={(e) => setActual(e.target.value)} /></label><label className="hmi-check"><input type="checkbox" checked={checked} onChange={(e) => setChecked(e.target.checked)} /> Р4831 установлен</label><footer><Btn onClick={close}>ОТМЕНА</Btn><Btn primary disabled={!checked || !actual.trim()} onClick={() => confirm(actual)}>ПОДТВЕРДИТЬ ТОЧКУ</Btn></footer></div></div>;
}

function ScenarioDrawer({ fault, setFault, route, jump, close }) {
  return <aside className="hmi-scenarios"><header><div><span>ТОЛЬКО ПРОТОТИП</span><b>Нештатные сценарии</b></div><button onClick={close}><X /></button></header><p>Панель нужна для UX-проверки без оборудования.</p>{[["normal","НОРМА"],["not-normal","НЕ НОРМА"],["stale","STALE DATA"],["stand-error","ОШИБКА СТЕНДА"],["overload-fail","OVERLOAD FAIL"]].map(([id,label]) => <button key={id} className={fault === id ? "active" : ""} onClick={() => setFault(id)}>{label}</button>)}<hr/><span>ПЕРЕЙТИ К ПРОВЕРКЕ</span><div className="steps">{route.map((r,i) => <button key={`${r.id}-${i}`} onClick={() => jump(i)}>{i+1}. {r.title}</button>)}</div></aside>;
}

function Run({ product, route, scopeLabel, tu, finish }) {
  const [index, setIndex] = useState(0);
  const [selected, setSelected] = useState(YALK_CHANNELS[0]);
  const [running, setRunning] = useState(true);
  const [stopped, setStopped] = useState(false);
  const [fault, setFault] = useState("normal");
  const [drawer, setDrawer] = useState(false);
  const [manual, setManual] = useState(false);
  const [done, setDone] = useState(() => new Set());
  const [archive, setArchive] = useState([]);
  const operation = route[index] || route[0];
  const telemetry = useOperatorTelemetry({ running: running && !stopped, operation, fault, selectedChannel: selected });
  const rawYalk = telemetry.current;
  const view = operation?.overload ? telemetry.overload.map((x) => ({ ...x, raw: Math.round(x.volts/6.2*1023), signal: x.volts >= 2.5 ? 1 : 0, deviation: x.deltaCode })) : rawYalk;
  const blocked = stopped || fault === "stale" || fault === "stand-error";
  const verdict = stopped ? "ОСТАНОВЛЕНО" : fault === "stand-error" ? "ОШИБКА СТЕНДА" : fault === "not-normal" || fault === "overload-fail" ? "НЕ НОРМА" : "ВЫПОЛНЯЕТСЯ";

  useEffect(() => {
    setSelected(operation?.overload ? 1 : YALK_CHANNELS[0]);
    telemetry.resetHistory();
    if (operation?.manual) { setManual(true); setRunning(false); }
  }, [operation?.id]);

  const saveCurrent = () => setArchive((xs) => [...xs.filter((x) => x.index !== index), { index, operation, histories: telemetry.history.map((h) => [...h]), current: view.map((x) => ({ ...x })) }]);
  const go = (nextIndex) => { saveCurrent(); setDone((s) => new Set([...s, index])); setIndex(nextIndex); setFault("normal"); setRunning(true); };
  const next = () => index < route.length - 1 && go(index + 1);
  const finishRun = () => {
    const finalArchive = [...archive.filter((x) => x.index !== index), { index, operation, histories: telemetry.history.map((h) => [...h]), current: view.map((x) => ({ ...x })) }];
    finish({ scopeLabel, verdict: fault === "not-normal" || fault === "overload-fail" ? "НЕ НОРМА" : fault === "stand-error" ? "НЕПОЛНАЯ ПРОВЕРКА" : stopped ? "НЕПОЛНАЯ ПРОВЕРКА" : "НОРМА", archive: finalArchive, consumption: [...telemetry.consumption] });
  };

  return <main className="hmi-run-screen">
    <div className="hmi-run-title"><div><span>{tu ? "ПРОВЕРКА ПО ТУ" : "ПРОИЗВОДСТВО"} · {scopeLabel}</span><h1>Проверка УБСИ · {product.serial}</h1><p>Этап изделия: {tu ? "не применяется" : product.stage} · текущая операция: {operation?.title}</p></div><div><Status value={verdict} /><button className="hmi-proto" onClick={() => setDrawer(!drawer)}><Bug /> СЦЕНАРИИ ПРОТОТИПА</button></div></div>
    <div className="hmi-run-grid">
      <aside className="hmi-run-left"><Panel title={tu ? "ГОТОВНОСТЬ" : "ГОТОВНОСТЬ ВЫБРАННОЙ ПРОЦЕДУРЫ"}><Readiness fault={fault} tu={tu} /></Panel><Panel title="ХОД ПРОВЕРКИ" className="hmi-steps-panel"><StepList route={route} index={index} done={done} jump={(i) => go(i)} /></Panel></aside>
      <section className="hmi-workspace"><Panel title="ХОД И РЕЗУЛЬТАТ" meta={`${operation?.title} · DEMO DATA`}>
        {operation?.yalk ? <SelectedTrace histories={telemetry.history} current={view} selected={selected} operation={operation} /> : operation?.ytp ? <YtpView tick={telemetry.tick} point={operation.pointOhm || 0} /> : operation?.yvp ? <YvpView tick={telemetry.tick} /> : <Consumption values={telemetry.consumption} />}
      </Panel>
      <Panel title="ВСЕ КАНАЛЫ" meta="гистограмма + живые колебания всех каналов">
        {operation?.yalk && <Histogram current={view} operation={operation} selected={selected} setSelected={setSelected} />}
        {!operation?.yalk && <div className="hmi-monitor-note"><b>ЯЛК · непрерывный монитор</b><span>Сырые колебания каналов остаются видимыми во время остальных частей проверки УБСИ.</span></div>}
        <LiveMatrix histories={telemetry.history} current={rawYalk} selected={selected} setSelected={setSelected} stale={fault === "stale"} />
      </Panel>
      <Panel title="ВЕДОМОСТЬ КАНАЛОВ" meta="клик по строке → канал на графике">{operation?.yalk ? <ResultTable current={view} operation={operation} selected={selected} setSelected={setSelected} /> : <div className="hmi-table-placeholder">Для выбранной процедуры здесь формируется собственная ведомость результатов. В prototype детально реализована ЯЛК-часть; ЯТП/ЯВП показаны для проверки маршрута и компоновки.</div>}</Panel>
      </section>
    </div>
    <div className="hmi-command"><div className="progress"><i style={{ width: `${Math.round((index + .45) / Math.max(1, route.length) * 100)}%` }} /></div><div className="hmi-command-row"><div><Status value={verdict} /><span>{blocked ? "Продолжение заблокировано до восстановления достоверных данных" : operation?.title}</span></div><div><Btn onClick={() => setRunning(!running)} disabled={stopped}>{running ? <Pause /> : <Play />}{running ? "ПАУЗА" : "ПРОДОЛЖИТЬ"}</Btn><Btn danger onClick={() => { setStopped(true); setRunning(false); }}><Stop weight="fill" /> БЕЗОПАСНО ОСТАНОВИТЬ</Btn>{index < route.length - 1 ? <Btn primary disabled={blocked} onClick={next}>СЛЕДУЮЩАЯ ПРОВЕРКА <ArrowRight /></Btn> : <Btn primary disabled={blocked} onClick={finishRun}><FileText /> ЗАВЕРШИТЬ / ОТЧЁТ</Btn>}</div></div></div>
    {drawer && <ScenarioDrawer fault={fault} setFault={setFault} route={route} close={() => setDrawer(false)} jump={(i) => go(i)} />}
    {manual && <ManualR4831 operation={operation} close={() => { setManual(false); setRunning(true); }} confirm={() => { setManual(false); setRunning(true); next(); }} />}
  </main>;
}

function Report({ product, result, tu, back }) {
  const [point, setPoint] = useState(0);
  const entry = result?.archive?.[point] || result?.archive?.[0];
  const current = entry?.current || [];
  const histories = entry?.histories || [];
  const [selected, setSelected] = useState(current[0]?.channel || YALK_CHANNELS[0]);
  useEffect(() => setSelected(current[0]?.channel || YALK_CHANNELS[0]), [point]);
  const downloadCsv = () => {
    const rows = ["channel,volts,deviation,signal,verdict", ...current.map((x) => `${x.channel},${x.volts ?? ""},${x.deviation ?? ""},${x.signal ?? ""},${x.failed ? "НЕ НОРМА" : "НОРМА"}`)];
    const blob = new Blob([rows.join("\n")], { type: "text/csv;charset=utf-8" }); const url = URL.createObjectURL(blob); const a = document.createElement("a"); a.href = url; a.download = `${product.serial}-prototype.csv`; a.click(); URL.revokeObjectURL(url);
  };
  if (tu) return <main className="hmi-page hmi-tu-report"><article><h1>ПРОТОКОЛ ПРИЁМО-СДАТОЧНОЙ ПРОВЕРКИ</h1><dl><dt>Блок / УБСИ №</dt><dd>{product.serial}</dd><dt>Результат</dt><dd><b>{result?.verdict || "НОРМА"}</b></dd></dl><table><tbody>{["Питание / потребление","ЯЛК","ЯТП","ЯВП","Безопасный сброс"].map((x) => <tr key={x}><td>{x}</td><td>DEMO / ПРОТОТИП</td></tr>)}</tbody></table><p>Оператор ____________________</p></article><div className="hmi-report-buttons"><Btn onClick={() => window.print()}><Printer /> ПЕЧАТЬ</Btn><Btn onClick={back}><ArrowLeft /> НАЗАД</Btn></div></main>;
  return <main className="hmi-page"><div className="hmi-title hmi-title-row"><div><span>ПРОИЗВОДСТВЕННЫЙ ОТЧЁТ</span><h1>{product.serial}</h1><p>{result?.scopeLabel} · этап {product.stage}</p></div><Status value={result?.verdict || "НОРМА"} /></div>
    <Panel title="ВЫПОЛНЕННЫЕ ПРОВЕРКИ" meta="выберите точку для просмотра сохранённых данных"><div className="hmi-report-points">{result?.archive?.map((x, i) => <button key={`${x.operation?.id}-${i}`} className={point === i ? "active" : ""} onClick={() => setPoint(i)}>{i+1}. {x.operation?.title}</button>)}</div></Panel>
    {entry?.operation?.yalk && <><Panel title="ГИСТОГРАММА КАНАЛОВ · СОХРАНЁННЫЙ РЕЗУЛЬТАТ"><Histogram current={current} operation={entry.operation} selected={selected} setSelected={setSelected} /></Panel><Panel title="КОЛЕБАНИЯ КАНАЛОВ · СОХРАНЁННЫЙ TRACE"><LiveMatrix histories={histories} current={current} selected={selected} setSelected={setSelected} /></Panel><Panel title="ПОДРОБНОСТИ"><SelectedTrace histories={histories} current={current} selected={selected} operation={entry.operation} /></Panel></>}
    <Panel title="ИТОГ"><div className="hmi-report-summary"><div><span>ЭТАП</span><b>{product.stage}</b></div><div><span>СОСТАВ ПРОВЕРКИ</span><b>{result?.scopeLabel}</b></div><div><span>РЕЗУЛЬТАТ</span><Status value={result?.verdict || "НОРМА"} /></div></div></Panel>
    <div className="hmi-report-buttons"><Btn onClick={downloadCsv}><FileText /> CSV</Btn><Btn onClick={() => window.print()}><Printer /> ПЕЧАТЬ</Btn><Btn onClick={back}><ArrowLeft /> НАЗАД</Btn></div>
  </main>;
}

function TuEntry({ start }) {
  const [serial, setSerial] = useState("УБСИ-468157-009");
  const [state, setState] = useState("preparing");
  useEffect(() => { const t = setTimeout(() => setState("ready"), 900); return () => clearTimeout(t); }, []);
  return <main className="hmi-page"><div className="hmi-title"><span>ПРИЁМО-СДАТОЧНАЯ ПРОВЕРКА ПО ТУ</span><h1>УБСИ</h1><p>Единый нормативный маршрут без выбора production stage.</p></div><Panel title="ЗАПУСК"><div className="hmi-tu-entry"><label><span>БЛОК / УБСИ №</span><input value={serial} onChange={(e) => setSerial(e.target.value)} /></label>{state === "preparing" ? <div className="preparing"><i /><b>ПОДГОТОВКА СТЕНДА...</b></div> : <div className="ready"><CheckCircle weight="fill" /><div><b>СТЕНД ГОТОВ</b><span>Можно запускать проверку</span></div><Btn primary onClick={() => start(serial)}>НАЧАТЬ ПРОВЕРКУ</Btn></div>}</div></Panel></main>;
}

function Admin({ products, replace }) {
  return <main className="hmi-page"><div className="hmi-title"><span>АДМИНИСТРИРОВАНИЕ</span><h1>Состав и история УБСИ</h1><p>Базовый контур регистрации состава и замен.</p></div>{products.map((p) => <Panel key={p.id} title={p.serial} meta={p.stage}><div className="hmi-admin-product"><div className="hmi-composition">{Object.entries(p.cells).map(([name,sn]) => <div key={name}><span>{name}</span><b>{sn || "не задан"}</b><Btn compact onClick={() => replace(p,name)}><ArrowsClockwise /> ЗАМЕНИТЬ</Btn></div>)}</div><div className="hmi-history"><b>История</b><span>Последний production run: {p.result}</span><span>Замены и повторные проверки сохраняются в карточке изделия.</span></div></div></Panel>)}</main>;
}

function ReplaceModal({ product, cell, close, save }) {
  const [sn, setSn] = useState(product.cells[cell] || "");
  return <div className="hmi-modal-bg"><div className="hmi-modal"><header><div><span>ИЗМЕНЕНИЕ СОСТАВА</span><h2>Замена {cell}</h2></div><button onClick={close}><X /></button></header><label><span>НОВЫЙ SN</span><input value={sn} onChange={(e) => setSn(e.target.value)} /></label><footer><Btn onClick={close}>ОТМЕНА</Btn><Btn primary onClick={() => save(sn)}>СОХРАНИТЬ ИСТОРИЮ ЗАМЕНЫ</Btn></footer></div></div>;
}

function Engineering({ open, close }) {
  if (!open) return null;
  return <aside className="hmi-engineering"><header><b>F12 · инженерный слой</b><button onClick={close}><X /></button></header><p>Внутренние адреса, диагностика транспорта и commissioning не смешиваются с операторским UI.</p><dl><dt>ЯЛК normal</dt><dd>80 рабочих каналов</dd><dt>Overload</dt><dd>88 физических каналов</dd><dt>Калибровка</dt><dd>97 / 99</dd></dl></aside>;
}

export function UbsiHmiPrototype() {
  const [route, setRoute] = useState("home");
  const [products, setProducts] = useState(PRODUCTS);
  const [product, setProduct] = useState(PRODUCTS[0]);
  const [runRoute, setRunRoute] = useState(FULL_ROUTE);
  const [scopeLabel, setScopeLabel] = useState("ПОЛНАЯ ПРОВЕРКА УБСИ");
  const [result, setResult] = useState(null);
  const [creating, setCreating] = useState(false);
  const [replacement, setReplacement] = useState(null);
  const [engineering, setEngineering] = useState(false);
  const [tuProduct, setTuProduct] = useState(null);

  useEffect(() => { const h = (e) => { if (e.key === "F12") { e.preventDefault(); setEngineering((v) => !v); } if (e.key === "Escape") { setEngineering(false); setCreating(false); setReplacement(null); } }; window.addEventListener("keydown", h); return () => window.removeEventListener("keydown", h); }, []);
  const updateProduct = (next) => { setProduct(next); setProducts((xs) => xs.map((x) => x.id === next.id ? next : x)); };
  const back = () => { const map = { production: "home", setup: "production", run: "setup", report: "setup", tu: "home", "tu-run": "tu", "tu-report": "tu", admin: "home" }; setRoute(map[route] || "home"); };
  const title = route.startsWith("tu") ? "ПРОВЕРКА ПО ТУ" : route === "admin" ? "АДМИНИСТРИРОВАНИЕ" : route === "home" ? "КТМА" : "ПРОИЗВОДСТВО";
  const subtitle = route.startsWith("tu") ? tuProduct?.serial || "УБСИ" : route === "home" ? "текущая поставка" : product.serial;

  return <div className="hmi-app"><AppHeader route={route} title={title} subtitle={subtitle} back={back} engineering={engineering} setEngineering={setEngineering} />
    {route === "home" && <Home go={setRoute} />}
    {route === "production" && <Registry products={products} select={(p) => { setProduct(p); setRoute("setup"); }} create={() => setCreating(true)} />}
    {route === "setup" && <Setup product={product} update={updateProduct} start={(r,label) => { setRunRoute(r); setScopeLabel(label); setResult(null); setRoute("run"); }} />}
    {route === "run" && <Run product={product} route={runRoute} scopeLabel={scopeLabel} finish={(r) => { setResult(r); updateProduct({ ...product, result: r.verdict }); setRoute("report"); }} />}
    {route === "report" && <Report product={product} result={result} back={back} />}
    {route === "tu" && <TuEntry start={(serial) => { const p = products.find((x) => x.serial === serial) || { ...PRODUCTS[0], id: "tu-temp", serial }; setTuProduct(p); setRoute("tu-run"); }} />}
    {route === "tu-run" && tuProduct && <Run product={tuProduct} route={FULL_ROUTE} scopeLabel="ПОЛНЫЙ МАРШРУТ ПО ТУ" tu finish={(r) => { setResult(r); setRoute("tu-report"); }} />}
    {route === "tu-report" && tuProduct && <Report product={tuProduct} result={result} tu back={back} />}
    {route === "admin" && <Admin products={products} replace={(p,cell) => setReplacement({ product:p, cell })} />}
    {creating && <ProductModal close={() => setCreating(false)} save={(serial,cells) => { const p = { id:`u-${Date.now()}`, serial, stage:"Первичное", result:"ГОТОВО", cells }; setProducts((xs) => [p,...xs]); setProduct(p); setCreating(false); setRoute("setup"); }} />}
    {replacement && <ReplaceModal product={replacement.product} cell={replacement.cell} close={() => setReplacement(null)} save={(sn) => { const p = replacement.product; const next = { ...p, cells:{ ...p.cells, [replacement.cell]:sn } }; setProducts((xs) => xs.map((x) => x.id === p.id ? next : x)); if (product.id === p.id) setProduct(next); setReplacement(null); }} />}
    <Engineering open={engineering} close={() => setEngineering(false)} />
  </div>;
}
