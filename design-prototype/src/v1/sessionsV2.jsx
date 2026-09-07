import { useEffect, useRef, useState } from "react";
import {
  CheckCircle,
  FileText,
  Gear,
  Warning,
} from "@phosphor-icons/react";
import {
  CommandButton,
  Panel,
  ProductHeader,
  SessionCommandBar,
  StatusBadge,
} from "./designSystem.jsx";

const steps = [
  ["01", "Назначение", "изделие и методика"],
  ["02", "Готовность", "оборудование и безопасное состояние"],
  ["03", "Калибровка", "ЯЛК · адреса 97 / 99"],
  ["04", "Каналы ЯЛК", "измерения по методике"],
  ["05", "ЯТП · 0 Ω", "30 каналов"],
  ["06", "ЯТП · 120 Ω", "ручное действие Р4831"],
  ["07", "ЯТП · 240 Ω", "30 каналов"],
  ["08", "Итог", "безопасный сброс и отчёт"],
];

function Metric({ label, value, note }) {
  return <div className="metric"><span>{label}</span><b>{value}</b>{note && <small>{note}</small>}</div>;
}

function ManualActionV2({ onClose, onConfirm }) {
  const [checked, setChecked] = useState(false);
  const dialogRef = useRef(null);
  const previousFocusRef = useRef(null);

  useEffect(() => {
    previousFocusRef.current = document.activeElement;
    const dialog = dialogRef.current;
    if (!dialog) return undefined;
    const getFocusable = () => Array.from(dialog.querySelectorAll('button:not([disabled]), input:not([disabled]), [href], [tabindex]:not([tabindex="-1"])'));
    getFocusable()[0]?.focus();
    const onKeyDown = (event) => {
      if (event.key === "Escape") {
        event.preventDefault();
        event.stopPropagation();
        onClose();
        return;
      }
      if (event.key !== "Tab") return;
      const focusable = getFocusable();
      if (!focusable.length) return;
      const first = focusable[0];
      const last = focusable[focusable.length - 1];
      if (event.shiftKey && document.activeElement === first) {
        event.preventDefault();
        last.focus();
      } else if (!event.shiftKey && document.activeElement === last) {
        event.preventDefault();
        first.focus();
      }
    };
    dialog.addEventListener("keydown", onKeyDown);
    return () => {
      dialog.removeEventListener("keydown", onKeyDown);
      previousFocusRef.current?.focus?.();
    };
  }, [onClose]);

  return <div className="manual-overlay" role="dialog" aria-modal="true" aria-label="Ручное действие Р4831"><div className="manual-dialog" ref={dialogRef}>
    <header><div><span className="ds-eyebrow">MANUAL ACTION · Р4831</span><h2>Установите 120 Ω</h2><p>ЯТП · канал 01 / 30 · общий разъём X123 · DEMO</p></div><StatusBadge status="ACTION" /></header>
    <div className="manual-values"><Metric label="ЦЕЛЕВОЕ ЗНАЧЕНИЕ" value="120 Ω" /><Metric label="ФАКТИЧЕСКОЕ ЗНАЧЕНИЕ" value="120,34 Ω" note="DEMO" /><Metric label="ДОПУСК" value="±0,50 %" /></div>
    <div className="manual-instruction"><Warning weight="fill" /><div><b>Физически установите сопротивление на магазине.</b><span>Подтверждение действия не заменяет измерительный результат.</span></div></div>
    <label className="manual-confirm"><input type="checkbox" checked={checked} onChange={(event) => setChecked(event.target.checked)} /><span>Р4831 физически установлен на 120 Ω</span></label>
    <div className="manual-audit"><span>Оператор: <b>Иванов И.И. · DEMO</b></span><span>Сеанс: <b>RUN-DEMO-005184</b></span></div>
    <footer><CommandButton onClick={onClose}>ОТМЕНА</CommandButton><CommandButton primary disabled={!checked} onClick={onConfirm}>ПОДТВЕРДИТЬ ДЕЙСТВИЕ</CommandButton></footer>
  </div></div>;
}

function StepContent({ step, openManual }) {
  if (step === 0) return <Panel title="НАЗНАЧЕНИЕ И МЕТОДИКА" badge={<StatusBadge status="READY" label="ПОДТВЕРЖДЕНО" />}><div className="form-readonly"><label><span>ИЗДЕЛИЕ</span><b>УБСИ-468157-012 · DEMO</b></label><label><span>МЕТОДИКА</span><b>ПСИ · утверждённая версия</b></label><label><span>СОСТАВ</span><b>ЯЛК-96 № 96-00431 · ЯТП № ТП-00192 · DEMO</b></label><label><span>ПРОФИЛЬ СТЕНДА</span><b>Станция 01 · DEMO</b></label></div></Panel>;
  if (step === 1) return <Panel title="ГОТОВНОСТЬ ОБОРУДОВАНИЯ" badge={<StatusBadge status="READY" />}><div className="check-list-v1">{["Адаптер УБСИ", "ИСД", "В7-78/1", "Р4831", "Безопасное состояние выходов"].map((item) => <div key={item}><CheckCircle weight="fill" /><span>{item}</span><b>ГОТОВО</b></div>)}</div></Panel>;
  if (step === 2) return <Panel title="КАЛИБРОВКА ЯЛК-96" badge={<StatusBadge status="NORMAL" />}><div className="summary-strip"><Metric label="АДРЕС 97" value="0,512 В" note="DEMO" /><Metric label="АДРЕС 99" value="5,986 В" note="DEMO" /><Metric label="ИТОГ" value="НОРМА" /></div></Panel>;
  if (step === 3) return <Panel title="КАНАЛЫ ЯЛК-96" badge={<StatusBadge status="NORMAL" />}><div className="summary-strip"><Metric label="КАНАЛЫ" value="80 / 80" /><Metric label="МАКС. ОТКЛОНЕНИЕ" value="0,31 %" note="DEMO" /><Metric label="ИТОГ" value="НОРМА" /></div></Panel>;
  if (step === 4) return <Panel title="ЯТП · ТОЧКА 0 Ω" badge={<StatusBadge status="NORMAL" />}><div className="summary-strip"><Metric label="КАНАЛЫ" value="30 / 30" /><Metric label="МАКС. ОТКЛОНЕНИЕ" value="0,28 %" note="DEMO" /><Metric label="ИТОГ" value="НОРМА" /></div></Panel>;
  if (step === 5) return <Panel title="ЯТП · ТОЧКА 120 Ω" badge={<StatusBadge status="ACTION" />}><div className="required-action"><Warning weight="fill" /><div><b>Требуется ручное действие оператора</b><p>Установите 120 Ω на Р4831 и подтвердите физическое действие в отдельном диалоге.</p></div><CommandButton primary onClick={openManual}><Gear /> ОТКРЫТЬ MANUAL ACTION</CommandButton></div></Panel>;
  if (step === 6) return <Panel title="ЯТП · ТОЧКА 240 Ω" badge={<StatusBadge status="NORMAL" />}><div className="summary-strip"><Metric label="КАНАЛЫ" value="30 / 30" /><Metric label="МАКС. ОТКЛОНЕНИЕ" value="0,24 %" note="DEMO" /><Metric label="ИТОГ" value="НОРМА" /></div></Panel>;
  return <Panel title="ИТОГ И БЕЗОПАСНЫЙ СБРОС" badge={<StatusBadge status="INCOMPLETE" />}><div className="result-summary"><FileText /><div><span>РЕЗУЛЬТАТ ДЕМОНСТРАЦИОННОГО МАРШРУТА</span><h2>ЯЛК-96 И ЯТП — НОРМА</h2><p>Полный нормативный итог не определяется этим демонстрационным маршрутом. ReportViewer покажет, какие обязательные проверки отсутствуют, вместо ложного итогового НОРМА.</p></div></div></Panel>;
}

export function AcceptanceSessionV2({ go, back, engineering, toggleEngineering }) {
  const [step, setStep] = useState(0);
  const [manual, setManual] = useState(false);
  const [stopped, setStopped] = useState(false);
  const next = () => setStep((value) => Math.min(7, value + 1));
  const status = stopped ? "STOPPED" : step === 5 ? "ACTION" : step === 7 ? "INCOMPLETE" : "RUNNING";
  return <>
    <ProductHeader product="КТМА · ПСИ ПО ТУ" title="УБСИ-468157-012 · активный сеанс" subtitle={`шаг ${step + 1} из 8 · DEMO`} engineering={engineering} onBack={back} onEngineering={toggleEngineering} />
    <div className="session-page">
      <div className="session-page__body">
        <div className="hero-row hero-row--compact"><div><span className="ds-eyebrow">ПРИЁМО-СДАТОЧНЫЙ МАРШРУТ</span><h1>{steps[step][1]}</h1><p>{steps[step][2]}</p></div><StatusBadge status={status} /></div>
        <div className="step-rail">{steps.map(([num, title, note], index) => <button key={num} className={`${index === step ? "current" : ""} ${index < step ? "done" : ""}`} onClick={() => !stopped && !manual && setStep(index)}><span>{index < step ? <CheckCircle weight="fill" /> : num}</span><b>{title}</b><small>{note}</small></button>)}</div>
        <div className="acceptance-body"><StepContent step={step} openManual={() => setManual(true)} /><Panel title="ХОД ПРОЦЕДУРЫ"><div className="progress-v1"><strong>{Math.round(((step + 1) / 8) * 100)}%</strong><div><i style={{ width: `${((step + 1) / 8) * 100}%` }} /></div></div><dl className="session-meta"><dt>Изделие</dt><dd>УБСИ-012 · DEMO</dd><dt>Стенд</dt><dd>Станция 01 · DEMO</dd><dt>Ошибки стенда</dt><dd>0</dd><dt>F12</dt><dd>{engineering ? "инженерный" : "обычный"}</dd></dl></Panel></div>
      </div>
      <SessionCommandBar status={status} progress={stopped ? "Сеанс остановлен безопасно" : `Шаг ${step + 1} / 8`} onBack={step > 0 && !stopped ? () => setStep((value) => Math.max(0, value - 1)) : undefined} onStop={() => { setStopped(true); setManual(false); }} primaryLabel={step === 7 ? "ОТКРЫТЬ ПРОТОКОЛ" : "СЛЕДУЮЩИЙ ШАГ"} onPrimary={!stopped ? (step === 7 ? () => go("report") : next) : undefined} />
    </div>
    {manual && <ManualActionV2 onClose={() => setManual(false)} onConfirm={() => { setManual(false); next(); }} />}
  </>;
}