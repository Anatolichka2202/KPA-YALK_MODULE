import { useMemo, useState } from "react";
import { FileText, Warning } from "@phosphor-icons/react";
import {
  CommandButton,
  Footer,
  Panel,
  ProductHeader,
  SessionCommandBar,
  StatusBadge,
} from "./designSystem.jsx";

function Metric({ label, value, note }) {
  return <div className="metric"><span>{label}</span><b>{value}</b>{note && <small>{note}</small>}</div>;
}

function ChannelOverview({ selected, onSelect, mode = "normal" }) {
  const data = useMemo(() => Array.from({ length: 96 }, (_, index) => {
    const channel = index + 1;
    const baseline = Number((((channel * 37) % 83) / 100 - 0.41).toFixed(2));
    const deviation = mode === "not-normal" && channel === 57 ? 0.71 : baseline;
    const actual = Number((3.1 + deviation * 0.031).toFixed(3));
    return { channel, deviation, actual, fault: mode === "not-normal" && channel === 57 };
  }), [mode]);

  return <div className="channel-overview">
    <div className="channel-axis"><span>+0,50%</span><span>0</span><span>−0,50%</span></div>
    <div className="channel-bars-v1">{data.map((item) => {
      const height = Math.max(8, Math.abs(item.deviation) / 0.5 * 46);
      return <button
        key={item.channel}
        className={`channel-bar-v1 ${item.channel === selected ? "channel-bar-v1--selected" : ""} ${item.fault ? "channel-bar-v1--fault" : ""}`}
        onClick={() => onSelect(item.channel)}
        title={`Канал ${item.channel}: ${item.deviation > 0 ? "+" : ""}${item.deviation}%`}
      >
        <span className="channel-bar-v1__value">{item.actual.toFixed(3)}</span>
        <i className={item.deviation >= 0 ? "positive" : "negative"} style={{ height: `${height}%` }} />
        <small>{item.channel}</small>
      </button>;
    })}</div>
    <div className="channel-caption"><span>Y: отклонение, % шкалы</span><span>X: каналы 1–96</span><span>над столбцом: фактическое значение, В</span></div>
  </div>;
}

function MiniTrend({ label, value, unit, power, muted }) {
  const points = power
    ? "0,42 20,40 40,41 60,38 80,39 100,37 120,38 140,35 160,36 180,34 200,35 220,34 240,36 260,35"
    : "0,39 20,41 40,38 60,40 80,37 100,39 120,36 140,38 160,35 180,37 200,34 220,36 240,35 260,36";
  return <div className={`mini-trend ${muted ? "mini-trend--muted" : ""}`}>
    <div><span>{label}</span><b>{value} {unit}</b></div>
    <svg viewBox="0 0 260 72" preserveAspectRatio="none" aria-label={label}><polyline points={points} /></svg>
  </div>;
}

function HmiOverlay({ mode }) {
  if (mode === "stale") return <div className="hmi-overlay hmi-overlay--stale"><Warning weight="fill" /><div><StatusBadge status="STALE" /><b>Последний кадр не считается текущим</b><span>Продолжение по stale data недоступно.</span></div></div>;
  if (mode === "error") return <div className="hmi-overlay hmi-overlay--error"><Warning weight="fill" /><div><StatusBadge status="ERROR" label="ОШИБКА СТЕНДА" /><b>Нет свежих данных эталона В7</b><span>Нормативный результат текущего измерения не определяется.</span></div></div>;
  return null;
}

export function ProductionSessionV3({ go, back, engineering, toggleEngineering, stage }) {
  const [selected, setSelected] = useState(57);
  const [paused, setPaused] = useState(false);
  const [stopped, setStopped] = useState(false);
  const [hmiMode, setHmiMode] = useState("normal");
  const blocked = hmiMode === "stale" || hmiMode === "error";
  const status = stopped ? "STOPPED" : paused ? "INCOMPLETE" : hmiMode === "not-normal" ? "NOT_NORMAL" : hmiMode === "stale" ? "STALE" : hmiMode === "error" ? "ERROR" : "RUNNING";
  const selectedFault = hmiMode === "not-normal" && selected === 57;

  return <>
    <ProductHeader
      product="КТМА · ПРОИЗВОДСТВО"
      title="УБСИ-468157-009 · активный производственный run"
      subtitle={`этап изделия: ${stage} · DEMO`}
      engineering={engineering}
      onBack={back}
      onEngineering={toggleEngineering}
    />
    <div className="session-page">
      <div className="session-page__body">
        <div className="hero-row hero-row--compact">
          <div>
            <span className="ds-eyebrow">ЭТАП ИЗДЕЛИЯ · {stage}</span>
            <h1>Текущая операция: ЯЛК · канал {selected} / 96</h1>
            <p>Production stage остаётся metadata run. Калибровка, каналы, точки Р4831 и cleanup — операции выбранной процедуры, а не отдельные этапы изделия.</p>
          </div>
          <StatusBadge status={status} />
        </div>

        <div className="hmi-demo-controls" aria-label="Демонстрационные состояния HMI">
          <span>DEMO STATE</span>
          <button className={hmiMode === "normal" ? "active" : ""} onClick={() => setHmiMode("normal")}>ПОТОК НОРМА</button>
          <button className={hmiMode === "not-normal" ? "active" : ""} onClick={() => { setHmiMode("not-normal"); setSelected(57); }}>НЕ НОРМА · CH57</button>
          <button className={hmiMode === "stale" ? "active" : ""} onClick={() => setHmiMode("stale")}>STALE DATA</button>
          <button className={hmiMode === "error" ? "active" : ""} onClick={() => setHmiMode("error")}>ОШИБКА СТЕНДА</button>
        </div>

        <div className="production-session-layout">
          <Panel
            title="ВСЕ КАНАЛЫ · ОТКЛОНЕНИЕ ОТ ЭТАЛОНА"
            badge={<span className="ds-counter">96 КАНАЛОВ</span>}
            className={`channel-panel ${blocked ? "channel-panel--blocked" : ""}`}
          >
            <div className="hmi-layer"><ChannelOverview selected={selected} onSelect={setSelected} mode={hmiMode} /><HmiOverlay mode={hmiMode} /></div>
          </Panel>

          <aside className="measurement-rail">
            <Panel title="КОНТЕКСТ RUN" badge={<StatusBadge status="RUNNING" label={stage.toUpperCase()} />}>
              <div className="metric-grid">
                <Metric label="ЭТАП ИЗДЕЛИЯ" value={stage} />
                <Metric label="ПРОЦЕДУРА" value="ЯЛК" />
                <Metric label="ТЕКУЩАЯ ОПЕРАЦИЯ" value={`канал ${selected} / 96`} />
                <Metric label="RUN" value="RUN-DEMO-005184" />
              </div>
            </Panel>
            <Panel title={`ВЫБРАН КАНАЛ ${String(selected).padStart(2, "0")}`} badge={selectedFault ? <StatusBadge status="NOT_NORMAL" /> : undefined}>
              <div className="metric-grid">
                <Metric label="ЗАДАНО" value="3,100 В" />
                <Metric label="В7" value={hmiMode === "error" ? "—" : "3,107 В"} note="DEMO" />
                <Metric label="ЯЛК" value={selectedFault ? "3,122 В" : "3,105 В"} note="DEMO" />
                <Metric label="ОТКЛОНЕНИЕ" value={selectedFault ? "+0,71 %" : hmiMode === "error" ? "—" : "−0,032 %"} note="допуск ±0,50 % · DEMO" />
              </div>
            </Panel>
            <MiniTrend label="ВЫБРАННЫЙ КАНАЛ · 16 ОТСЧЁТОВ" value={hmiMode === "error" ? "—" : selectedFault ? "3,122" : "3,105"} unit="В" muted={blocked} />
            <MiniTrend label="ПОТРЕБЛЕНИЕ · ТЕКУЩЕЕ" value="0,31" unit="А" power muted={hmiMode === "error"} />
            <Panel title="СОСТАВ"><div className="composition"><b>ЯЛК-96 № 96-00427 · DEMO</b><span>состав подтверждён</span><small>этап: {stage}</small></div></Panel>
          </aside>
        </div>
      </div>

      <SessionCommandBar
        status={status}
        progress={stopped ? "Сеанс остановлен безопасно" : paused ? "Пауза · поток не считается текущим" : blocked ? "Ожидание достоверных данных" : `${stage} · ЯЛК · канал ${selected} / 96`}
        onPause={() => !stopped && !blocked && setPaused((value) => !value)}
        paused={paused}
        onStop={() => setStopped(true)}
        primaryLabel="ОТКРЫТЬ ВЕДОМОСТЬ"
        onPrimary={!stopped && !blocked ? () => go(hmiMode === "not-normal" ? "production-ledger-fault" : "production-ledger") : undefined}
      />
    </div>
  </>;
}

export function ProductionLedgerV3({ back, engineering, toggleEngineering, fault = false, stage }) {
  const rows = [
    fault ? ["ЯЛК · канал 57", "В7 + ЯЛК", "3,122 В · DEMO", "+0,71 % при допуске ±0,50 % · DEMO", "NOT_NORMAL"] : ["ЯЛК · канал 57", "В7 + ЯЛК", "3,105 В · DEMO", "−0,032 % · DEMO", "NORMAL"],
    ["Питание · 24 В", "АКИП · DEMO", "0,31 А · DEMO", "производственное измерение · DEMO", "NORMAL"],
    ["Стабильность выборки", "ЯЛК · DEMO", "0,06 % FS · DEMO", "диагностический индикатор · DEMO", "NORMAL"],
    ["Замены состава", "Registrar", "нет", "—", "NORMAL"],
  ];

  return <>
    <ProductHeader product="КТМА · ПРОИЗВОДСТВО" title="Производственная ведомость" subtitle={`УБСИ-468157-009 · ${stage} · DEMO`} engineering={engineering} onBack={back} onEngineering={toggleEngineering} />
    <div className="station-page"><div className="station-page__body">
      <div className="hero-row"><div><span className="ds-eyebrow">ЭТАП ИЗДЕЛИЯ</span><h1>{stage}</h1><p>Ведомость фиксирует выбранный production stage и результаты выполненных процедур. Номера внутренних операций не являются этапами изделия.</p></div><StatusBadge status={fault ? "NOT_NORMAL" : "NORMAL"} label={fault ? "ЕСТЬ ОТКЛОНЕНИЕ" : "ПРОВЕРКА ЗАВЕРШЕНА"} /></div>
      <Panel title="РЕЗУЛЬТАТЫ ПРОЦЕДУР" badge={<span className="ds-counter">4 ПРИМЕРА</span>}><div className="detail-ledger"><div className="detail-ledger__head"><span>ПРОВЕРКА</span><span>ИСТОЧНИК</span><span>ЗНАЧЕНИЕ</span><span>КРИТЕРИЙ</span><span>СТАТУС</span></div>{rows.map((row) => <div className="detail-ledger__row" key={row[0]}><b>{row[0]}</b><span>{row[1]}</span><span>{row[2]}</span><span>{row[3]}</span><StatusBadge status={row[4]} /></div>)}</div></Panel>
      <div className="production-ledger-note"><FileText /><div><b>Граница отчётности</b><span>Это производственная ведомость этапа «{stage}», а не приёмо-сдаточный протокол по ТУ.</span></div></div>
      <div className="report-actions-v1"><CommandButton><FileText /> ЭКСПОРТ ЭТАПА · ПРОТОТИП</CommandButton></div>
    </div><Footer left="Production ledger v3 · stage metadata отдельно от procedure progress." /></div>
  </>;
}
