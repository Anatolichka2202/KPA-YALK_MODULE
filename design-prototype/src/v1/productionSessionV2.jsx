import { useMemo, useState } from "react";
import { FileText } from "@phosphor-icons/react";
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

function ChannelOverview({ selected, onSelect }) {
  const data = useMemo(() => Array.from({ length: 96 }, (_, index) => {
    const channel = index + 1;
    const deviation = Number((((channel * 37) % 83) / 100 - 0.41).toFixed(2));
    const actual = Number((3.1 + deviation * 0.031).toFixed(3));
    return { channel, deviation, actual };
  }), []);
  return <div className="channel-overview">
    <div className="channel-axis"><span>+0,50%</span><span>0</span><span>−0,50%</span></div>
    <div className="channel-bars-v1">{data.map((item) => {
      const height = Math.max(8, Math.abs(item.deviation) / 0.5 * 46);
      return <button key={item.channel} className={`channel-bar-v1 ${item.channel === selected ? "channel-bar-v1--selected" : ""}`} onClick={() => onSelect(item.channel)} title={`Канал ${item.channel}: ${item.deviation > 0 ? "+" : ""}${item.deviation}%`}><span className="channel-bar-v1__value">{item.actual.toFixed(3)}</span><i className={item.deviation >= 0 ? "positive" : "negative"} style={{ height: `${height}%` }} /><small>{item.channel}</small></button>;
    })}</div>
    <div className="channel-caption"><span>Y: отклонение, %</span><span>X: каналы 1–96</span><span>значения и отклонения — DEMO</span></div>
  </div>;
}

function MiniTrend({ label, value, unit, power }) {
  const points = power ? "0,42 20,40 40,41 60,38 80,39 100,37 120,38 140,35 160,36 180,34 200,35 220,34 240,36 260,35" : "0,39 20,41 40,38 60,40 80,37 100,39 120,36 140,38 160,35 180,37 200,34 220,36 240,35 260,36";
  return <div className="mini-trend"><div><span>{label}</span><b>{value} {unit}</b></div><svg viewBox="0 0 260 72" preserveAspectRatio="none" aria-label={label}><polyline points={points} /></svg></div>;
}

export function ProductionSessionV2({ go, back, engineering, toggleEngineering }) {
  const [selected, setSelected] = useState(57);
  const [paused, setPaused] = useState(false);
  const [stopped, setStopped] = useState(false);
  const status = stopped ? "STOPPED" : paused ? "INCOMPLETE" : "RUNNING";
  return <>
    <ProductHeader product="КТМА · ПРОИЗВОДСТВО" title="УБСИ-468157-009 · контроль после сборки" subtitle="активный сеанс · все значения DEMO" engineering={engineering} onBack={back} onEngineering={toggleEngineering} />
    <div className="session-page">
      <div className="session-page__body">
        <div className="hero-row hero-row--compact"><div><span className="ds-eyebrow">ЭТАП 02 · НОРМАЛЬНЫЕ УСЛОВИЯ</span><h1>Контроль каналов ЯЛК</h1><p>Основной HMI — все каналы; справа контекст выбранного канала и потребление.</p></div><StatusBadge status={status} /></div>
        <div className="production-session-layout">
          <Panel title="ВСЕ КАНАЛЫ · ОТКЛОНЕНИЕ ОТ ЭТАЛОНА" badge={<span className="ds-counter">96 КАНАЛОВ</span>} className="channel-panel"><ChannelOverview selected={selected} onSelect={setSelected} /></Panel>
          <aside className="measurement-rail">
            <Panel title={`ВЫБРАН КАНАЛ ${String(selected).padStart(2, "0")}`}><div className="metric-grid"><Metric label="ЗАДАНО" value="3,100 В" /><Metric label="В7" value="3,107 В" note="DEMO" /><Metric label="ЯЛК" value="3,105 В" note="DEMO" /><Metric label="ОТКЛОНЕНИЕ" value="−0,032 %" note="допуск ±0,50 % · DEMO" /></div></Panel>
            <MiniTrend label="ВЫБРАННЫЙ КАНАЛ · 16 ОТСЧЁТОВ" value="3,105" unit="В" />
            <MiniTrend label="ПОТРЕБЛЕНИЕ · 24 В" value="0,31" unit="А" power />
            <Panel title="СОСТАВ"><div className="composition"><b>ЯЛК-96 № 96-00427 · DEMO</b><span>состав подтверждён</span><small>замен в текущем этапе нет</small></div></Panel>
          </aside>
        </div>
      </div>
      <SessionCommandBar status={status} progress={stopped ? "Сеанс остановлен безопасно" : paused ? "Пауза · поток не считается текущим" : `Канал ${selected} / 96 · поток активен`} onPause={() => !stopped && setPaused((value) => !value)} paused={paused} onStop={() => setStopped(true)} primaryLabel="ОТКРЫТЬ ВЕДОМОСТЬ" onPrimary={!stopped ? () => go("production-ledger") : undefined} />
    </div>
  </>;
}

const rows = [
  ["ЯЛК · канал 57", "В7 + ЯЛК", "3,105 В · DEMO", "−0,032 % · DEMO", "NORMAL"],
  ["Питание · 24 В", "АКИП · DEMO", "0,31 А · DEMO", "в пределах этапа · DEMO", "NORMAL"],
  ["Шум выбранного канала", "ЯЛК · DEMO", "0,06 % FS · DEMO", "по сценарию · DEMO", "NORMAL"],
  ["Замены состава", "карточка изделия", "нет", "—", "NORMAL"],
];

export function ProductionLedger({ back, engineering, toggleEngineering }) {
  return <>
    <ProductHeader product="КТМА · ПРОИЗВОДСТВО" title="Ведомость этапа" subtitle="УБСИ-468157-009 · контроль после сборки · DEMO" engineering={engineering} onBack={back} onEngineering={toggleEngineering} />
    <div className="station-page"><div className="station-page__body">
      <div className="hero-row"><div><span className="ds-eyebrow">ПРОИЗВОДСТВЕННАЯ ВЕДОМОСТЬ</span><h1>Этап 02 · нормальные условия</h1><p>Это производственная запись этапа, а не приёмо-сдаточный итог по ТУ.</p></div><StatusBadge status="NORMAL" label="ЭТАП ЗАВЕРШЁН" /></div>
      <Panel title="РЕЗУЛЬТАТЫ ЭТАПА" badge={<span className="ds-counter">4 ПРИМЕРА</span>}><div className="detail-ledger"><div className="detail-ledger__head"><span>ПРОВЕРКА</span><span>ИСТОЧНИК</span><span>ЗНАЧЕНИЕ</span><span>КРИТЕРИЙ</span><span>СТАТУС</span></div>{rows.map((row) => <div className="detail-ledger__row" key={row[0]}><b>{row[0]}</b><span>{row[1]}</span><span>{row[2]}</span><span>{row[3]}</span><StatusBadge status={row[4]} /></div>)}</div></Panel>
      <div className="production-ledger-note"><FileText /><div><b>Граница отчётности</b><span>Эта ведомость фиксирует конкретный производственный этап. Она не должна отображаться как «Результат полной проверки по п. 5.6 ТУ».</span></div></div>
      <div className="report-actions-v1"><CommandButton><FileText /> ЭКСПОРТ ЭТАПА · ПРОТОТИП</CommandButton></div>
    </div><Footer left="Production ledger v1 · отдельный артефакт от acceptance protocol." /></div>
  </>;
}