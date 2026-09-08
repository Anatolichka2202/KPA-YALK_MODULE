import { useEffect, useMemo, useState } from "react";
import {
  ArrowLeft, ArrowRight, Bug, CheckCircle, Circuitry, FileText, Gear,
  MagnifyingGlass, Pause, Play, Plus, ShieldCheck, Stop, Warning,
  Wrench, X, ArrowsClockwise,
} from "@phosphor-icons/react";
import {
  CHECK_GROUPS, FULL_ROUTE, PRODUCTION_STAGES, YALK_CHANNELS,
  routeForSelection, useOperatorTelemetry,
} from "./operatorTelemetry.js";

const PRODUCTS = [
  {
    id: "u7",
    serial: "УБСИ-0007",
    stage: "Климат НУ",
    result: "НОРМА",
    cells: { "ЯЛК-96": "96-00427", ЯТП: "ТП-00184", "ЯВП-8": "ВП-00041", "ЯП-П": "ПП-00018" },
  },
  {
    id: "u12",
    serial: "УБСИ-0012",
    stage: "Первичное",
    result: "НЕПОЛНАЯ",
    cells: { "ЯЛК-96": "96-00431", ЯТП: "ТП-00192", "ЯВП-8": "ВП-00045", "ЯП-П": "ПП-00020" },
  },
];

const YVP_FREQUENCIES = [0.15, 20, 250, 500, 1800, 2000, 4000];

function Button({ children, primary = false, danger = false, compact = false, className = "", ...props }) {
  return (
    <button
      className={`rp-btn ${primary ? "primary" : ""} ${danger ? "danger" : ""} ${compact ? "compact" : ""} ${className}`}
      {...props}
    >
      {children}
    </button>
  );
}

function Status({ value }) {
  const tone = value === "НОРМА" || value === "ГОТОВО"
    ? "ok"
    : value === "НЕ НОРМА"
      ? "bad"
      : value === "ОШИБКА СТЕНДА"
        ? "error"
        : value === "ВЫПОЛНЯЕТСЯ"
          ? "run"
          : "warn";
  return <span className={`rp-status ${tone}`}>{value}</span>;
}

function Header({ route, title, subtitle, onBack, engineering, setEngineering }) {
  return (
    <header className="rp-header">
      <div className="rp-brand">
        <Circuitry weight="fill" />
        <div><b>КТМА</b><span>УБСИ · интерактивный прототип</span></div>
      </div>
      <div className="rp-header-context">
        {route !== "home" && <button onClick={onBack} aria-label="Назад"><ArrowLeft /></button>}
        <div><b>{title}</b><span>{subtitle}</span></div>
      </div>
      <button className={engineering ? "active" : ""} onClick={() => setEngineering(!engineering)}>
        <Wrench /> F12
      </button>
    </header>
  );
}

function Panel({ title, meta, children, className = "" }) {
  return (
    <section className={`rp-panel ${className}`}>
      <header><b>{title}</b>{meta && <span>{meta}</span>}</header>
      {children}
    </section>
  );
}

function Home({ go }) {
  return (
    <main className="rp-page rp-home">
      <div className="rp-title">
        <span>КТМА · ПОСТАВКА УБСИ</span>
        <h1>Путь оператора</h1>
        <p>Общая концепция сохранена. В текущем прототипе предметно доведены производство УБСИ, ТУ и базовое администрирование.</p>
      </div>
      <div className="rp-home-grid">
        <button onClick={() => go("production")}><Circuitry /><div><b>ПРОИЗВОДСТВО</b><span>Полная или неполная проверка УБСИ</span></div><ArrowRight /></button>
        <button onClick={() => go("tu")}><ShieldCheck /><div><b>ПРОВЕРКА ПО ТУ</b><span>Простой операторский маршрут и короткий итог</span></div><ArrowRight /></button>
        <button onClick={() => go("admin")}><Gear /><div><b>АДМИНИСТРИРОВАНИЕ</b><span>Состав, замены, история прогонов</span></div><ArrowRight /></button>
      </div>
    </main>
  );
}

function ProductList({ products, onSelect, onCreate }) {
  const [query, setQuery] = useState("");
  const filtered = products.filter((item) => item.serial.toLowerCase().includes(query.toLowerCase()));
  return (
    <main className="rp-page">
      <div className="rp-title"><span>ПРОИЗВОДСТВО</span><h1>Выбор УБСИ</h1><p>Поиск существующего изделия или регистрация нового.</p></div>
      <Panel title="ИЗДЕЛИЯ">
        <div className="rp-search">
          <label><MagnifyingGlass /><input value={query} onChange={(e) => setQuery(e.target.value)} placeholder="Номер УБСИ / SN" /></label>
          <Button primary onClick={onCreate}><Plus /> ЗАРЕГИСТРИРОВАТЬ УБСИ</Button>
        </div>
        <div className="rp-products">
          <div className="head"><span>ИЗДЕЛИЕ</span><span>ЭТАП</span><span>ПОСЛЕДНИЙ РЕЗУЛЬТАТ</span><span /></div>
          {filtered.map((item) => (
            <button key={item.id} onClick={() => onSelect(item)}>
              <b>{item.serial}</b><span>{item.stage}</span><Status value={item.result} /><ArrowRight />
            </button>
          ))}
        </div>
      </Panel>
    </main>
  );
}

function CreateProduct({ onClose, onSave }) {
  const [serial, setSerial] = useState("");
  return (
    <div className="rp-modal-backdrop">
      <div className="rp-modal">
        <header><h2>Новый УБСИ</h2><button onClick={onClose}><X /></button></header>
        <label><span>НОМЕР / SN</span><input autoFocus value={serial} onChange={(e) => setSerial(e.target.value)} placeholder="УБСИ-...." /></label>
        <footer><Button onClick={onClose}>ОТМЕНА</Button><Button primary disabled={!serial.trim()} onClick={() => onSave(serial.trim())}>СОЗДАТЬ</Button></footer>
      </div>
    </div>
  );
}

function Setup({ product, onUpdate, onStart }) {
  const [full, setFull] = useState(true);
  const [groups, setGroups] = useState(["yalk-analog"]);
  const route = routeForSelection(full, groups);
  const toggle = (id) => setGroups((items) => items.includes(id) ? items.filter((x) => x !== id) : [...items, id]);
  return (
    <main className="rp-page">
      <div className="rp-title rp-title-row">
        <div><span>ПРОИЗВОДСТВО</span><h1>{product.serial}</h1><p>УБСИ — изделие. ЯЛК, ЯТП, ЯВП и питание — части выбранного маршрута проверки.</p></div>
        <Status value={product.result} />
      </div>
      <div className="rp-two-col">
        <Panel title="ЭТАП ПРОИЗВОДСТВА">
          <div className="rp-stage-grid">
            {PRODUCTION_STAGES.map((stage) => (
              <button key={stage} className={stage === product.stage ? "active" : ""} onClick={() => onUpdate({ ...product, stage })}>
                {stage}{stage === product.stage && <CheckCircle weight="fill" />}
              </button>
            ))}
          </div>
        </Panel>
        <Panel title="СОСТАВ УБСИ">
          <div className="rp-composition">
            {Object.entries(product.cells).map(([name, serial]) => <div key={name}><span>{name}</span><b>{serial || "не задан"}</b></div>)}
          </div>
        </Panel>
      </div>
      <Panel title="СОСТАВ ПРОВЕРКИ" meta={full ? "полная" : "неполная"}>
        <div className="rp-mode-choice">
          <button className={full ? "active" : ""} onClick={() => setFull(true)}><b>ПОЛНАЯ ПРОВЕРКА УБСИ</b><span>последовательно выполнить весь маршрут</span></button>
          <button className={!full ? "active" : ""} onClick={() => setFull(false)}><b>НЕПОЛНАЯ ПРОВЕРКА</b><span>например, только аналоговые ЯЛК после ремонта</span></button>
        </div>
        {!full && (
          <div className="rp-check-grid">
            {CHECK_GROUPS.map((item) => (
              <label key={item.id} className={groups.includes(item.id) ? "checked" : ""}>
                <input type="checkbox" checked={groups.includes(item.id)} onChange={() => toggle(item.id)} />
                <div><b>{item.title}</b><span>{item.note || ""}</span></div>
              </label>
            ))}
          </div>
        )}
        <div className="rp-route-preview">
          <b>МАРШРУТ</b>
          <ol>{route.map((item) => <li key={item.id}>{item.title}</li>)}</ol>
          {!full && groups.some((id) => id.startsWith("yalk-")) && <p>Служебная подготовка и калибровка ЯЛК добавляются автоматически.</p>}
        </div>
        <div className="rp-actions-right"><Button primary disabled={!route.length} onClick={() => onStart(route, full ? "ПОЛНАЯ ПРОВЕРКА УБСИ" : "НЕПОЛНАЯ ПРОВЕРКА")}><Play /> НАЧАТЬ</Button></div>
      </Panel>
    </main>
  );
}

function pathFor(values, width, height, min, max) {
  if (!values?.length) return "";
  const span = Math.max(0.000001, max - min);
  return values.map((value, index) => {
    const x = values.length === 1 ? width / 2 : index / (values.length - 1) * width;
    const y = height - ((value - min) / span) * height;
    return `${index ? "L" : "M"}${x.toFixed(2)},${Math.max(0, Math.min(height, y)).toFixed(2)}`;
  }).join(" ");
}

function Sparkline({ values, min, max, failed = false }) {
  const width = 100;
  const height = 28;
  const d = pathFor(values?.length ? values : [min, max], width, height, min, max);
  return <svg className={`rp-spark ${failed ? "failed" : ""}`} viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none"><path d={d} /></svg>;
}

function channelView(current, overload) {
  if (overload) {
    return current.map((item) => ({
      channel: item.channel,
      volts: item.volts,
      failed: item.failed,
      detail: `${item.deltaCode >= 0 ? "+" : ""}${item.deltaCode} code`,
      raw: Math.round(item.volts / 6.2 * 1023),
      signal: item.volts >= 2.5 ? 1 : 0,
    }));
  }
  return current;
}

function commonRange(histories, items) {
  const values = [];
  histories.slice(0, items.length).forEach((history, index) => {
    if (history?.length) values.push(...history);
    else if (items[index]) values.push(items[index].volts);
  });
  if (!values.length) return [0, 6.2];
  const low = Math.min(...values);
  const high = Math.max(...values);
  const minimumSpan = 0.01;
  const center = (low + high) / 2;
  const span = Math.max(minimumSpan, high - low) * 1.35;
  return [center - span / 2, center + span / 2];
}

function AllChannels({ histories, current, selected, onSelect, overload = false, stale = false, readonly = false }) {
  const items = channelView(current, overload);
  const [min, max] = commonRange(histories, items);
  return (
    <div className={`rp-all-channels ${stale ? "stale" : ""}`}>
      <div className="rp-all-title">
        <div><b>ВСЕ КАНАЛЫ · LIVE</b><span>свежие отсчёты потока → пересчёт в В · общий масштаб mini-trace</span></div>
        <div><strong>{items.length}</strong><span>{overload ? "физических каналов" : "рабочих каналов"}</span></div>
      </div>
      {stale && <div className="rp-stale-banner"><Warning /> ДАННЫЕ УСТАРЕЛИ — линии заморожены, нормативный результат не формируется</div>}
      <div className="rp-channel-grid">
        {items.map((item, index) => {
          const values = histories[index]?.length ? histories[index] : [item.volts];
          const detail = overload
            ? item.detail
            : `${item.deviation >= 0 ? "+" : ""}${item.deviation.toFixed(3)} % FS`;
          return (
            <button
              key={item.channel}
              className={`${selected === item.channel ? "selected" : ""} ${item.failed ? "failed" : ""}`}
              onClick={() => !readonly && onSelect(item.channel)}
              title={`Канал ${item.channel}: ${item.volts.toFixed(4)} В`}
            >
              <div><b>К{item.channel}</b><strong>{item.volts.toFixed(3)} В</strong></div>
              <Sparkline values={values} min={min} max={max} failed={item.failed} />
              <span>{detail}</span>
            </button>
          );
        })}
      </div>
      <div className="rp-scale"><span>{max.toFixed(4)} В</span><span>общий масштаб для визуального сравнения колебаний</span><span>{min.toFixed(4)} В</span></div>
    </div>
  );
}

function SelectedChannel({ histories, current, selected, operation, overload = false }) {
  const items = channelView(current, overload);
  const index = items.findIndex((item) => item.channel === selected);
  const item = items[index >= 0 ? index : 0];
  const values = histories[index >= 0 ? index : 0]?.length ? histories[index >= 0 ? index : 0] : [item?.volts ?? 0];
  const target = operation?.overload ? 3.1 : typeof operation?.point === "number" ? operation.point : item?.volts ?? 0;
  const low = Math.min(...values, target) - 0.015;
  const high = Math.max(...values, target) + 0.015;
  const d = pathFor(values, 900, 155, low, high);
  const targetY = 155 - ((target - low) / Math.max(0.000001, high - low)) * 155;
  return (
    <div className="rp-selected-channel">
      <div className="rp-selected-head">
        <div><b>Канал {item?.channel ?? "—"} · подробная динамика</b><span>последние свежие отсчёты текущего шага</span></div>
        <div><strong>{item ? `${item.volts.toFixed(4)} В` : "—"}</strong><span>{operation?.overload ? item?.detail : item ? `${item.deviation >= 0 ? "+" : ""}${item.deviation.toFixed(3)} % FS` : ""}</span></div>
      </div>
      <div className="rp-big-chart">
        <svg viewBox="0 0 900 155" preserveAspectRatio="none">
          {[0, 1, 2, 3].map((n) => <line key={n} className="grid" x1="0" x2="900" y1={n * 155 / 3} y2={n * 155 / 3} />)}
          <line className="reference" x1="0" x2="900" y1={targetY} y2={targetY} />
          <path d={d} className={item?.failed ? "failed" : ""} />
        </svg>
        <div className="rp-chart-legend"><span>эталон / baseline</span><span>ЯЛК, В</span></div>
      </div>
      <div className="rp-selected-metrics">
        <div><span>RAW</span><b>{item?.raw ?? "—"}</b></div>
        <div><span>СИГНАЛ</span><b>{item?.signal ?? "—"}</b></div>
        <div><span>ТЕКУЩАЯ ТОЧКА</span><b>{operation?.overload ? operation.polarity : typeof operation?.point === "number" ? `${String(operation.point).replace(".", ",")} В` : "—"}</b></div>
        <div><span>ИТОГ</span><Status value={item?.failed ? "НЕ НОРМА" : "НОРМА"} /></div>
      </div>
    </div>
  );
}

function Consumption({ values }) {
  const current = values.map((item) => item.currentA);
  const d = pathFor(current, 380, 90, 0.2, 0.42);
  const latest = values.at(-1);
  return (
    <Panel title="ПОТРЕБЛЕНИЕ УБСИ" meta="граница 400 мА" className="rp-consumption-panel">
      <div className="rp-consumption-chart">
        <svg viewBox="0 0 380 90" preserveAspectRatio="none">
          <line className="limit" x1="0" x2="380" y1="8" y2="8" />
          {d && <path d={d} />}
        </svg>
        <div><span>U <b>{latest ? `${latest.voltage} В` : "—"}</b></span><span>I <b>{latest ? `${Math.round(latest.currentA * 1000)} мА` : "—"}</b></span></div>
      </div>
    </Panel>
  );
}

function StepList({ route, index, completed, onJump }) {
  return (
    <div className="rp-step-list">
      <div className="head"><span>ЭТАП / ПРОВЕРКА</span><span>ИТОГ</span></div>
      {route.map((item, i) => (
        <button key={`${item.id}-${i}`} className={`${i === index ? "current" : ""} ${completed.has(i) ? "done" : ""}`} onClick={() => onJump(i)}>
          <div><span>{String(i + 1).padStart(2, "0")}</span><b>{item.title}</b></div>
          <span>{i === index ? "ВЫПОЛНЯЕТСЯ" : completed.has(i) ? "ЗАВЕРШЕНО" : "—"}</span>
        </button>
      ))}
    </div>
  );
}

function Equipment({ fault, operation }) {
  const error = fault === "stand-error";
  const stale = fault === "stale";
  const rows = [
    ["Адаптер", error ? "НЕ ГОТОВО" : stale ? "STALE" : "ГОТОВО"],
    ["ИСД", error ? "НЕ ГОТОВО" : "ГОТОВО"],
    ["АКИП-1160/6", "ГОТОВО"],
    ...(operation?.ytp ? [["Магазин Р4831", "ПОДТВЕРЖДЕНО"]] : []),
  ];
  return (
    <div className="rp-equipment">
      <div className="head"><span>УСТРОЙСТВО</span><span>СОСТОЯНИЕ</span></div>
      {rows.map(([name, state]) => <div key={name}><span>{name}</span><b className={state === "ГОТОВО" || state === "ПОДТВЕРЖДЕНО" ? "ok" : "bad"}>{state}</b></div>)}
    </div>
  );
}

function YtpPanel({ pointOhm = 120, tick }) {
  const rows = Array.from({ length: 30 }, (_, index) => {
    const channel = index + 1;
    const noise = Math.sin(channel * 0.7 + tick * 0.2) * 0.12;
    return { channel, value: pointOhm + noise };
  });
  return (
    <div className="rp-procedure-special">
      <div className="rp-special-title"><div><b>ЯТП · {pointOhm} Ω</b><span>30 каналов · DEMO-значения для UX</span></div><strong>Р4831</strong></div>
      <div className="rp-ytp-grid">{rows.map((item) => <div key={item.channel}><span>К{item.channel}</span><b>{item.value.toFixed(2)} Ω</b><Sparkline values={[item.value - 0.05, item.value + 0.02, item.value - 0.01, item.value]} min={pointOhm - 0.3} max={pointOhm + 0.3} /></div>)}</div>
    </div>
  );
}

function YvpPanel({ frequency, setFrequency, tick }) {
  const rows = Array.from({ length: 8 }, (_, index) => ({
    channel: index + 1,
    value: 1 + Math.sin((index + 1) * 0.6 + tick * 0.11) * 0.015,
  }));
  return (
    <div className="rp-procedure-special">
      <div className="rp-special-title"><div><b>ЯВП-8 · АЧХ / коэффициент</b><span>UI маршрута; измерительные значения DEMO, нормативный verdict здесь не выдумывается</span></div><strong>{frequency} Hz</strong></div>
      <div className="rp-frequency-row">{YVP_FREQUENCIES.map((item) => <button key={item} className={frequency === item ? "active" : ""} onClick={() => setFrequency(item)}>{item} Hz</button>)}</div>
      <div className="rp-yvp-grid">{rows.map((item) => <div key={item.channel}><span>Вход {item.channel}</span><b>{item.value.toFixed(4)}</b><span>DEMO</span></div>)}</div>
    </div>
  );
}

function PowerPanel({ values }) {
  const latest = values.at(-1);
  return (
    <div className="rp-procedure-special rp-power-special">
      <div className="rp-special-title"><div><b>Питание / потребление</b><span>переходы 19 / 24 / 27 / 35 / 37 В и контроль тока</span></div><strong>{latest ? `${latest.voltage} В` : "—"}</strong></div>
      <Consumption values={values} />
    </div>
  );
}

function ManualR4831({ operation, onConfirm, onCancel }) {
  return (
    <div className="rp-modal-backdrop">
      <div className="rp-modal rp-manual">
        <header><div><span>РУЧНОЕ ДЕЙСТВИЕ</span><h2>Р4831 · {operation.pointOhm} Ω</h2></div><Warning weight="fill" /></header>
        <p>Установите требуемое сопротивление на магазине Р4831 и подтвердите физическое действие.</p>
        <footer><Button onClick={onCancel}>ОТМЕНА</Button><Button primary onClick={onConfirm}>УСТАНОВЛЕНО · ПРОДОЛЖИТЬ</Button></footer>
      </div>
    </div>
  );
}

function ScenarioDrawer({ fault, setFault, route, onJump, onClose }) {
  const options = [
    ["normal", "НОРМА"], ["not-normal", "НЕ НОРМА"], ["stale", "STALE DATA"],
    ["stand-error", "ОШИБКА СТЕНДА"], ["overload-fail", "OVERLOAD FAIL"],
  ];
  return (
    <aside className="rp-scenario-drawer">
      <header><div><span>ТОЛЬКО ПРОТОТИП</span><b>Нештатные сценарии</b></div><button onClick={onClose}><X /></button></header>
      <p>Эта панель нужна только для проверки UX без оборудования. В целевом операторском UI её не будет.</p>
      <div className="rp-scenario-options">{options.map(([id, label]) => <button key={id} className={fault === id ? "active" : ""} onClick={() => setFault(id)}>{label}</button>)}</div>
      <hr />
      <span>ПЕРЕЙТИ К ПРОЦЕДУРЕ</span>
      <div className="rp-scenario-steps">{route.map((item, index) => <button key={`${item.id}-${index}`} onClick={() => onJump(index)}>{index + 1}. {item.title}</button>)}</div>
    </aside>
  );
}

function Run({ product, route, scopeLabel, mode, onFinish }) {
  const [index, setIndex] = useState(0);
  const [selected, setSelected] = useState(YALK_CHANNELS[0]);
  const [running, setRunning] = useState(true);
  const [stopped, setStopped] = useState(false);
  const [fault, setFault] = useState("normal");
  const [drawer, setDrawer] = useState(false);
  const [manual, setManual] = useState(false);
  const [manualConfirmed, setManualConfirmed] = useState(false);
  const [completed, setCompleted] = useState(() => new Set());
  const [archive, setArchive] = useState([]);
  const [yvpFrequency, setYvpFrequency] = useState(20);

  const operation = route[index] || route[0];
  const telemetry = useOperatorTelemetry({ running: running && !stopped, operation, fault, selectedChannel: selected });
  const current = operation?.overload ? telemetry.overload : telemetry.current;
  const blocked = stopped || fault === "stale" || fault === "stand-error";
  const verdict = stopped
    ? "ОСТАНОВЛЕНО"
    : fault === "stand-error"
      ? "ОШИБКА СТЕНДА"
      : fault === "not-normal" || fault === "overload-fail"
        ? "НЕ НОРМА"
        : "ВЫПОЛНЯЕТСЯ";

  useEffect(() => {
    setSelected(operation?.overload ? 1 : YALK_CHANNELS[0]);
    telemetry.resetHistory();
    setManualConfirmed(false);
    if (operation?.manual) {
      setManual(true);
      setRunning(false);
    }
  }, [operation?.id]);

  const capture = () => {
    if (!operation?.yalk) return;
    const snapshot = {
      id: operation.id,
      title: operation.title,
      operation: { ...operation },
      histories: telemetry.history.map((row) => [...row]),
      current: channelView(current, operation.overload).map((item) => ({ ...item })),
      verdict: fault === "not-normal" || fault === "overload-fail" ? "НЕ НОРМА" : "НОРМА",
    };
    setArchive((rows) => [...rows.filter((row) => row.id !== snapshot.id), snapshot]);
  };

  const jump = (nextIndex) => {
    capture();
    setIndex(nextIndex);
    setFault("normal");
    setStopped(false);
    setRunning(true);
  };

  const next = () => {
    capture();
    setCompleted((rows) => new Set([...rows, index]));
    if (index < route.length - 1) {
      setIndex(index + 1);
      setFault("normal");
      setRunning(true);
    }
  };

  const finish = () => {
    capture();
    const finalArchive = operation?.yalk
      ? [...archive.filter((row) => row.id !== operation.id), {
          id: operation.id,
          title: operation.title,
          operation: { ...operation },
          histories: telemetry.history.map((row) => [...row]),
          current: channelView(current, operation.overload).map((item) => ({ ...item })),
          verdict: fault === "not-normal" || fault === "overload-fail" ? "НЕ НОРМА" : "НОРМА",
        }]
      : archive;
    onFinish({
      verdict: stopped ? "ОСТАНОВЛЕНО" : fault === "stand-error" ? "НЕПОЛНАЯ" : fault === "not-normal" || fault === "overload-fail" ? "НЕ НОРМА" : "НОРМА",
      scopeLabel,
      archive: finalArchive,
      completed: [...completed, index],
    });
  };

  const stepMeta = operation?.overload
    ? `${operation.polarity} · 88 физических каналов · |Δcode| ≤ 2`
    : operation?.yalk && typeof operation.point === "number"
      ? `${String(operation.point).replace(".", ",")} В · 80 каналов`
      : operation?.title;

  return (
    <main className="rp-run-screen">
      <div className="rp-run-title">
        <div><span>{mode === "tu" ? "ПРОВЕРКА ПО ТУ" : "ПРОИЗВОДСТВО"} · {scopeLabel}</span><h1>{product.serial}</h1><p>{product.stage} · {stepMeta}</p></div>
        <div><Status value={verdict} /><button className="rp-prototype-toggle" onClick={() => setDrawer(!drawer)}><Bug /> СЦЕНАРИИ ПРОТОТИПА</button></div>
      </div>

      <div className="rp-run-layout">
        <aside className="rp-run-left">
          <Panel title="ГОТОВНОСТЬ ВЫБРАННОЙ ПРОЦЕДУРЫ"><Equipment fault={fault} operation={operation} /></Panel>
          <Panel title="ХОД ПРОВЕРКИ" className="rp-steps-panel"><StepList route={route} index={index} completed={completed} onJump={jump} /></Panel>
        </aside>

        <section className="rp-run-workspace">
          {operation?.yalk ? (
            <>
              <Panel title="ВЫБРАННЫЙ КАНАЛ" meta="подробно"><SelectedChannel histories={telemetry.history} current={current} selected={selected} operation={operation} overload={operation.overload} /></Panel>
              <Panel title="ВСЕ КАНАЛЫ" meta="главное изменение относительно текущего экрана">
                <AllChannels histories={telemetry.history} current={current} selected={selected} onSelect={setSelected} overload={operation.overload} stale={fault === "stale"} />
                {fault === "stand-error" && <div className="rp-error-banner"><Warning /><div><b>ОШИБКА СТЕНДА</b><span>Последние значения остаются видимыми, но результат изделия не формируется.</span></div></div>}
              </Panel>
            </>
          ) : operation?.ytp ? (
            <Panel title="ХОД И РЕЗУЛЬТАТ"><YtpPanel pointOhm={operation.pointOhm} tick={telemetry.tick} /></Panel>
          ) : operation?.yvp ? (
            <Panel title="ХОД И РЕЗУЛЬТАТ"><YvpPanel frequency={yvpFrequency} setFrequency={setYvpFrequency} tick={telemetry.tick} /></Panel>
          ) : operation?.group === "power" ? (
            <Panel title="ХОД И РЕЗУЛЬТАТ"><PowerPanel values={telemetry.consumption} /></Panel>
          ) : (
            <Panel title="ХОД И РЕЗУЛЬТАТ"><div className="rp-service-step"><CheckCircle weight="fill" /><div><b>{operation?.title}</b><span>Служебная операция. В рабочем UI не требует отдельного мастер-экрана.</span></div></div></Panel>
          )}

          <div className="rp-result-table-wrap">
            <div className="rp-result-table-head"><span>КАНАЛ</span><span>ТОЧКА</span><span>RAW</span><span>ИЗМЕРЕНО</span><span>{operation?.overload ? "Δ CODE" : "ОТКЛОНЕНИЕ"}</span><span>СИГНАЛ</span><span>ИТОГ</span></div>
            {operation?.yalk ? channelView(current, operation.overload).slice(0, operation?.overload ? 88 : 80).map((item) => (
              <button key={item.channel} className={item.channel === selected ? "selected" : ""} onClick={() => setSelected(item.channel)}>
                <b>{item.channel}</b>
                <span>{operation?.overload ? operation.polarity : `${String(operation.point ?? "—").replace(".", ",")} В`}</span>
                <span>{item.raw}</span>
                <span>{item.volts.toFixed(4)} В</span>
                <span>{operation?.overload ? item.detail : `${item.deviation >= 0 ? "+" : ""}${item.deviation.toFixed(3)} % FS`}</span>
                <span>{item.signal}</span>
                <Status value={item.failed ? "НЕ НОРМА" : "НОРМА"} />
              </button>
            )) : <div className="rp-table-empty">Таблица каналов появляется для измерительной процедуры.</div>}
          </div>
        </section>
      </div>

      <div className="rp-progress"><i style={{ width: `${((index + 1) / route.length) * 100}%` }} /></div>
      <div className="rp-command-bar">
        <div><Status value={verdict} /><span>{blocked ? "Продолжение заблокировано до восстановления достоверных данных" : operation?.title}</span></div>
        <div>
          <Button disabled={stopped || fault === "stand-error" || fault === "stale" || (operation?.manual && !manualConfirmed)} onClick={() => setRunning(!running)}>{running ? <Pause /> : <Play />}{running ? "ПАУЗА" : "ПРОДОЛЖИТЬ"}</Button>
          <Button danger onClick={() => { setStopped(true); setRunning(false); }}><Stop weight="fill" /> БЕЗОПАСНО ОСТАНОВИТЬ</Button>
          {index < route.length - 1
            ? <Button primary disabled={blocked || (operation?.manual && !manualConfirmed)} onClick={next}>СЛЕДУЮЩИЙ ШАГ <ArrowRight /></Button>
            : <Button primary disabled={blocked} onClick={finish}><FileText /> ЗАВЕРШИТЬ / ОТЧЁТ</Button>}
        </div>
      </div>

      {drawer && <ScenarioDrawer fault={fault} setFault={setFault} route={route} onJump={jump} onClose={() => setDrawer(false)} />}
      {manual && <ManualR4831 operation={operation} onCancel={() => { setManual(false); setRunning(false); }} onConfirm={() => { setManual(false); setManualConfirmed(true); setRunning(true); }} />}
    </main>
  );
}

function ProductionReport({ product, result, onBack }) {
  const [tab, setTab] = useState("summary");
  const yalkRows = result?.archive || [];
  const [stepId, setStepId] = useState(yalkRows[0]?.id || "");
  const selectedStep = yalkRows.find((item) => item.id === stepId) || yalkRows[0];
  const [selectedChannel, setSelectedChannel] = useState(1);

  useEffect(() => {
    if (selectedStep?.current?.length) setSelectedChannel(selectedStep.current[0].channel);
  }, [selectedStep?.id]);

  return (
    <main className="rp-page rp-report-page">
      <div className="rp-title rp-title-row"><div><span>ПРОИЗВОДСТВЕННЫЙ ОТЧЁТ</span><h1>{product.serial}</h1><p>{result?.scopeLabel}</p></div><Status value={result?.verdict || "НОРМА"} /></div>
      <div className="rp-report-tabs"><button className={tab === "summary" ? "active" : ""} onClick={() => setTab("summary")}>СВОДКА</button><button className={tab === "traces" ? "active" : ""} onClick={() => setTab("traces")}>ЯЛК · СОХРАНЁННЫЕ КОЛЕБАНИЯ</button><button className={tab === "events" ? "active" : ""} onClick={() => setTab("events")}>ХОД ПРОВЕРКИ</button></div>
      {tab === "summary" && <Panel title="ИТОГ"><div className="rp-report-summary"><div><span>ИЗДЕЛИЕ</span><b>{product.serial}</b></div><div><span>ЭТАП</span><b>{product.stage}</b></div><div><span>СОСТАВ</span><b>{result?.scopeLabel}</b></div><div><span>РЕЗУЛЬТАТ</span><Status value={result?.verdict || "НОРМА"} /></div></div></Panel>}
      {tab === "traces" && (
        <>
          <Panel title="ВЫПОЛНЕННАЯ ТОЧКА" meta="выберите то, что нужно визуально проверить после испытания">
            <div className="rp-report-step-select">{yalkRows.map((row) => <button key={row.id} className={row.id === selectedStep?.id ? "active" : ""} onClick={() => setStepId(row.id)}>{row.title}<Status value={row.verdict} /></button>)}</div>
          </Panel>
          {selectedStep ? (
            <>
              <Panel title="ВЫБРАННЫЙ КАНАЛ"><SelectedChannel histories={selectedStep.histories} current={selectedStep.current} selected={selectedChannel} operation={selectedStep.operation} overload={selectedStep.operation?.overload} /></Panel>
              <Panel title="ВСЕ КАНАЛЫ · СОХРАНЁННЫЙ TRACE"><AllChannels histories={selectedStep.histories} current={selectedStep.current} selected={selectedChannel} onSelect={setSelectedChannel} overload={selectedStep.operation?.overload} /></Panel>
            </>
          ) : <div className="rp-empty-report">В этом прогоне не было ЯЛК-процедур.</div>}
        </>
      )}
      {tab === "events" && <Panel title="ПОСЛЕДОВАТЕЛЬНОСТЬ"><div className="rp-report-events">{result?.completed?.map((value, i) => <div key={`${value}-${i}`}><CheckCircle weight="fill" /><span>Шаг {value + 1}</span><b>ЗАВЕРШЕНО</b></div>)}</div></Panel>}
      <Button onClick={onBack}><ArrowLeft /> НАЗАД К ИЗДЕЛИЮ</Button>
    </main>
  );
}

function TuEntry({ onStart }) {
  const [serial, setSerial] = useState("УБСИ-0012");
  const [state, setState] = useState("idle");
  const prepare = () => {
    setState("preparing");
    window.setTimeout(() => setState("ready"), 900);
  };
  return (
    <main className="rp-page">
      <div className="rp-title"><span>ПРИЁМО-СДАТОЧНАЯ ПРОВЕРКА ПО ТУ</span><h1>УБСИ</h1><p>Оператор вводит изделие, стенд готовится автоматически, затем выполняется единый run.</p></div>
      <Panel title="ЗАПУСК">
        <div className="rp-tu-entry">
          <label><span>БЛОК / УБСИ №</span><input value={serial} onChange={(e) => setSerial(e.target.value)} /></label>
          {state === "idle" && <Button primary onClick={prepare}>ПОДГОТОВИТЬ СТЕНД</Button>}
          {state === "preparing" && <div className="rp-preparing"><i /><b>ПОДГОТОВКА СТЕНДА...</b></div>}
          {state === "ready" && <div className="rp-ready"><CheckCircle weight="fill" /><b>СТЕНД ГОТОВ</b><Button primary onClick={() => onStart(serial)}>НАЧАТЬ ПРОВЕРКУ</Button></div>}
        </div>
      </Panel>
    </main>
  );
}

function TuReport({ product, result, onBack }) {
  return (
    <main className="rp-page rp-tu-report">
      <article>
        <h1>ПРОТОКОЛ ПРИЁМО-СДАТОЧНОЙ ПРОВЕРКИ</h1>
        <dl><dt>Изделие</dt><dd>{product.serial}</dd><dt>Результат</dt><dd><b>{result?.verdict || "НОРМА"}</b></dd></dl>
        <table><tbody><tr><td>Питание / потребление</td><td>DEMO</td></tr><tr><td>ЯЛК</td><td>DEMO</td></tr><tr><td>ЯТП</td><td>DEMO</td></tr><tr><td>ЯВП</td><td>DEMO</td></tr><tr><td>Безопасный сброс</td><td>DEMO</td></tr></tbody></table>
        <p>Оператор ____________________</p>
      </article>
      <Button onClick={onBack}><ArrowLeft /> НАЗАД</Button>
    </main>
  );
}

function Admin({ products, onReplace }) {
  return (
    <main className="rp-page">
      <div className="rp-title"><span>АДМИНИСТРИРОВАНИЕ</span><h1>Изделия и состав</h1><p>Базовый предметный минимум для текущей поставки.</p></div>
      {products.map((product) => (
        <Panel key={product.id} title={product.serial} meta={product.stage}>
          <div className="rp-admin-product">
            <div className="rp-cell-list">{Object.entries(product.cells).map(([cell, serial]) => <div key={cell}><div><b>{cell}</b><span>SN {serial || "не задан"}</span></div><Button compact onClick={() => onReplace(product, cell)}><ArrowsClockwise /> ЗАМЕНИТЬ</Button></div>)}</div>
            <div className="rp-history"><b>ИСТОРИЯ</b><div><span>Production</span><strong>3 прогона</strong></div><div><span>ТУ</span><strong>1 прогон</strong></div><div><span>Последний итог</span><Status value={product.result} /></div></div>
          </div>
        </Panel>
      ))}
    </main>
  );
}

function ReplaceCell({ product, cell, onClose, onSave }) {
  const [serial, setSerial] = useState(product.cells[cell] || "");
  const recommendation = cell === "ЯТП" ? "После замены: полный ЯТП 0 / 120 / 240 Ω" : cell === "ЯВП-8" ? "После замены: повтор ЯВП и связанных проверок" : cell === "ЯЛК-96" ? "После замены: повтор соответствующих проверок ЯЛК" : "После замены: повтор контроля питания";
  return (
    <div className="rp-modal-backdrop">
      <div className="rp-modal">
        <header><h2>Замена {cell}</h2><button onClick={onClose}><X /></button></header>
        <label><span>НОВЫЙ SN</span><input value={serial} onChange={(e) => setSerial(e.target.value)} /></label>
        <div className="rp-rerun-note"><b>ПРЕДЛОЖЕНИЕ ПОВТОРА</b><span>{recommendation}</span></div>
        <footer><Button onClick={onClose}>ОТМЕНА</Button><Button primary onClick={() => onSave(serial)}>СОХРАНИТЬ</Button></footer>
      </div>
    </div>
  );
}

function Engineering({ open, onClose }) {
  if (!open) return null;
  return (
    <aside className="rp-engineering">
      <header><b>F12 · инженерный слой</b><button onClick={onClose}><X /></button></header>
      <p>Внутренняя адресация и транспорт не должны попадать в обычный операторский язык.</p>
      <dl><dt>ЯЛК normal set</dt><dd>80 подтверждённых рабочих адресов</dd><dt>Overload</dt><dd>88 физических каналов</dd><dt>Калибровка</dt><dd>97 / 99</dd></dl>
    </aside>
  );
}

export function UbsiReviewPrototype() {
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

  useEffect(() => {
    const onKey = (event) => {
      if (event.key === "F12") { event.preventDefault(); setEngineering((value) => !value); }
      if (event.key === "Escape") { setEngineering(false); setCreating(false); setReplacement(null); }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, []);

  const updateProduct = (next) => {
    setProduct(next);
    setProducts((items) => items.map((item) => item.id === next.id ? next : item));
  };

  const backMap = {
    production: "home",
    "production-setup": "production",
    "production-run": "production-setup",
    "production-report": "production-setup",
    tu: "home",
    "tu-run": "tu",
    "tu-report": "tu",
    admin: "home",
  };
  const goBack = () => setRoute(backMap[route] || "home");

  let title = "КТМА · УБСИ";
  let subtitle = "кликабельный UX-прототип";
  if (route.startsWith("production")) { title = "ПРОИЗВОДСТВО"; subtitle = product.serial; }
  if (route.startsWith("tu")) { title = "ПРОВЕРКА ПО ТУ"; subtitle = tuProduct?.serial || "УБСИ"; }
  if (route === "admin") { title = "АДМИНИСТРИРОВАНИЕ"; subtitle = "состав и история"; }

  return (
    <div className="rp-app">
      <Header route={route} title={title} subtitle={subtitle} onBack={goBack} engineering={engineering} setEngineering={setEngineering} />
      {route === "home" && <Home go={setRoute} />}
      {route === "production" && <ProductList products={products} onSelect={(item) => { setProduct(item); setRoute("production-setup"); }} onCreate={() => setCreating(true)} />}
      {route === "production-setup" && <Setup product={product} onUpdate={updateProduct} onStart={(nextRoute, label) => { setRunRoute(nextRoute); setScopeLabel(label); setResult(null); setRoute("production-run"); }} />}
      {route === "production-run" && <Run product={product} route={runRoute} scopeLabel={scopeLabel} mode="production" onFinish={(next) => { setResult(next); updateProduct({ ...product, result: next.verdict }); setRoute("production-report"); }} />}
      {route === "production-report" && <ProductionReport product={product} result={result} onBack={goBack} />}
      {route === "tu" && <TuEntry onStart={(serial) => { const found = products.find((item) => item.serial === serial) || { ...PRODUCTS[0], id: "tu-temp", serial }; setTuProduct(found); setResult(null); setRoute("tu-run"); }} />}
      {route === "tu-run" && tuProduct && <Run product={tuProduct} route={FULL_ROUTE} scopeLabel="ПОЛНЫЙ МАРШРУТ ПО ТУ" mode="tu" onFinish={(next) => { setResult(next); setRoute("tu-report"); }} />}
      {route === "tu-report" && tuProduct && <TuReport product={tuProduct} result={result} onBack={goBack} />}
      {route === "admin" && <Admin products={products} onReplace={(p, cell) => setReplacement({ product: p, cell })} />}

      {creating && <CreateProduct onClose={() => setCreating(false)} onSave={(serial) => { const next = { id: `u-${Date.now()}`, serial, stage: "Первичное", result: "ГОТОВО", cells: { "ЯЛК-96": "", ЯТП: "", "ЯВП-8": "", "ЯП-П": "" } }; setProducts((items) => [next, ...items]); setProduct(next); setCreating(false); setRoute("production-setup"); }} />}
      {replacement && <ReplaceCell product={replacement.product} cell={replacement.cell} onClose={() => setReplacement(null)} onSave={(serial) => { const next = { ...replacement.product, cells: { ...replacement.product.cells, [replacement.cell]: serial } }; setProducts((items) => items.map((item) => item.id === next.id ? next : item)); if (product.id === next.id) setProduct(next); setReplacement(null); }} />}
      <Engineering open={engineering} onClose={() => setEngineering(false)} />
    </div>
  );
}
