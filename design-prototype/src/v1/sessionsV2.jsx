import { useEffect, useRef, useState } from "react";
import {
  CheckCircle,
  Gear,
  ShieldCheck,
  Warning,
} from "@phosphor-icons/react";
import {
  CommandButton,
  Panel,
  ProductHeader,
  SessionCommandBar,
  StatusBadge,
} from "./designSystem.jsx";

const routeChecks = [
  ["Подготовка стенда", "готовность и safe state", "NORMAL"],
  ["Питание / потребление", "24 / 27 / 35 В; выдержки 19 / 37 В", "NORMAL"],
  ["ЯЛК-96", "каналы, контакты, обрыв, ±12 В", "RUNNING"],
  ["ЯТП", "30 каналов · 0 / 120 / 240 Ω", "READY"],
  ["ЯВП-8", "коэффициенты и АЧХ", "READY"],
  ["Безопасный сброс", "cleanup после завершения/Stop", "READY"],
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
    <header><div><span className="ds-eyebrow">РУЧНОЕ ДЕЙСТВИЕ · Р4831</span><h2>Установите 120 Ω</h2><p>ЯТП · общий разъём X123 · DEMO</p></div><StatusBadge status="ACTION" /></header>
    <div className="manual-values"><Metric label="ТРЕБУЕТСЯ" value="120 Ω" /><Metric label="ФАКТИЧЕСКОЕ" value="120,34 Ω" note="DEMO" /><Metric label="СОСТОЯНИЕ" value="ожидает подтверждения" /></div>
    <div className="manual-instruction"><Warning weight="fill" /><div><b>Физически установите сопротивление на магазине.</b><span>Подтверждение действия не заменяет измерительный результат.</span></div></div>
    <label className="manual-confirm"><input type="checkbox" checked={checked} onChange={(event) => setChecked(event.target.checked)} /><span>Р4831 физически установлен на 120 Ω</span></label>
    <div className="manual-audit"><span>Оператор: <b>Иванов И.И. · DEMO</b></span><span>Сеанс: <b>RUN-DEMO-005184</b></span></div>
    <footer><CommandButton onClick={onClose}>ОТМЕНА</CommandButton><CommandButton primary disabled={!checked} onClick={onConfirm}>ПОДТВЕРДИТЬ</CommandButton></footer>
  </div></div>;
}

export function AcceptanceSessionV2({ go, back, engineering, toggleEngineering }) {
  const [manual, setManual] = useState(false);
  const [stopped, setStopped] = useState(false);
  const [manualConfirmed, setManualConfirmed] = useState(false);
  const status = stopped ? "STOPPED" : manual ? "ACTION" : "RUNNING";

  return <>
    <ProductHeader
      product="КТМА · ПСИ ПО ТУ"
      title="УБСИ-468157-012 · активный прогон"
      subtitle="единый нормативный маршрут · без production stage"
      engineering={engineering}
      onBack={back}
      onEngineering={toggleEngineering}
    />
    <div className="session-page">
      <div className="session-page__body">
        <div className="hero-row hero-row--compact">
          <div>
            <span className="ds-eyebrow">ПРИЁМО-СДАТОЧНАЯ ПРОВЕРКА</span>
            <h1>{stopped ? "Проверка остановлена" : manual ? "Требуется действие оператора" : "Выполняется ЯЛК-96"}</h1>
            <p>{stopped ? "Стенд переведён в безопасное состояние." : manual ? "После подтверждения Р4831 движок продолжит тот же run." : "Текущая операция: канал 48 / 80. Внутренние операции сценария не являются производственными этапами."}</p>
          </div>
          <StatusBadge status={status} />
        </div>

        <div className="acceptance-body acceptance-body--v3">
          <div className="acceptance-main-v3">
            <Panel title="НАЗНАЧЕНИЕ" badge={<StatusBadge status="READY" label="ПОДТВЕРЖДЕНО" />}>
              <div className="form-readonly">
                <label><span>БЛОК</span><b>УБСИ-468157-012 · DEMO</b></label>
                <label><span>МЕТОДИКА</span><b>ПСИ · текущая утверждённая методика</b></label>
                <label><span>СОСТАВ</span><b>ЯЛК-96 · ЯТП · ЯВП-8 · ЯП-П · DEMO</b></label>
                <label><span>PRODUCTION STAGE</span><b>не применяется к ПСИ</b></label>
              </div>
            </Panel>

            <Panel title="ХОД ПРОВЕРКИ" badge={<span className="ds-counter">ЕДИНЫЙ RUN</span>}>
              <div className="procedure-route-v3">
                {routeChecks.map(([title, detail, itemStatus]) => {
                  const shownStatus = stopped && itemStatus === "RUNNING" ? "STOPPED" : itemStatus;
                  return <div className={`procedure-route-v3__row ${itemStatus === "RUNNING" ? "current" : ""}`} key={title}>
                    <div className="procedure-route-v3__icon">{itemStatus === "NORMAL" ? <CheckCircle weight="fill" /> : <ShieldCheck />}</div>
                    <div><b>{title}</b><span>{detail}</span></div>
                    <StatusBadge status={shownStatus} />
                  </div>;
                })}
              </div>
            </Panel>

            <Panel title="РУЧНЫЕ ДЕЙСТВИЯ">
              <div className="required-action">
                <Gear />
                <div>
                  <b>Р4831 появляется только когда его запросит ЯТП</b>
                  <p>{manualConfirmed ? "DEMO: действие 120 Ω уже подтверждено и сохранено в run." : "Это не отдельный экран этапа. Движок приостанавливает текущую процедуру и ждёт подтверждение оператора."}</p>
                </div>
                <CommandButton disabled={stopped || manualConfirmed} onClick={() => setManual(true)}>DEMO · ПОКАЗАТЬ 120 Ω</CommandButton>
              </div>
            </Panel>
          </div>

          <Panel title="ХОД ПРОЦЕДУРЫ">
            <div className="progress-v1"><strong>{stopped ? "—" : "58%"}</strong><div><i style={{ width: stopped ? "58%" : "58%" }} /></div></div>
            <dl className="session-meta">
              <dt>Изделие</dt><dd>УБСИ-012 · DEMO</dd>
              <dt>Текущая операция</dt><dd>{stopped ? "остановлено" : manual ? "Р4831 · 120 Ω" : "ЯЛК · 48 / 80"}</dd>
              <dt>Стенд</dt><dd>Станция 01 · DEMO</dd>
              <dt>Ошибки стенда</dt><dd>0</dd>
              <dt>Этап производства</dt><dd>не применяется</dd>
            </dl>
          </Panel>
        </div>
      </div>

      <SessionCommandBar
        status={status}
        progress={stopped ? "Сеанс остановлен безопасно" : manual ? "Ожидается подтверждение Р4831" : "ЯЛК · канал 48 / 80"}
        onStop={() => { setStopped(true); setManual(false); }}
        primaryLabel="ПРОТОКОЛ ПОСЛЕ ЗАВЕРШЕНИЯ"
        onPrimary={undefined}
      />
    </div>
    {manual && <ManualActionV2 onClose={() => setManual(false)} onConfirm={() => { setManual(false); setManualConfirmed(true); }} />}
  </>;
}
