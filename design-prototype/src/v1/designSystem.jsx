import {
  ArrowLeft,
  CheckCircle,
  CircleNotch,
  Clock,
  Lock,
  Pause,
  Play,
  ShieldCheck,
  Stop,
  Warning,
  Wrench,
  XCircle,
} from "@phosphor-icons/react";

export const STATUS = {
  READY: { label: "ГОТОВО", tone: "ready", icon: CheckCircle },
  RUNNING: { label: "ВЫПОЛНЯЕТСЯ", tone: "running", icon: CircleNotch },
  ACTION: { label: "ТРЕБУЕТСЯ ДЕЙСТВИЕ", tone: "action", icon: Warning },
  NORMAL: { label: "НОРМА", tone: "normal", icon: CheckCircle },
  NOT_NORMAL: { label: "НЕ НОРМА", tone: "danger", icon: XCircle },
  ERROR: { label: "ОШИБКА", tone: "danger", icon: Warning },
  INCOMPLETE: { label: "НЕПОЛНАЯ", tone: "action", icon: Clock },
  STOPPED: { label: "ОСТАНОВЛЕНО", tone: "neutral", icon: Stop },
  UNAVAILABLE: { label: "НЕДОСТУПНО", tone: "neutral", icon: Lock },
  BUSY: { label: "ЗАНЯТО", tone: "busy", icon: Lock },
};

export function StatusBadge({ status = "READY", label }) {
  const item = STATUS[status] || STATUS.READY;
  const Icon = item.icon;
  return (
    <span className={`ds-status ds-status--${item.tone}`} data-status={status}>
      <Icon weight={status === "RUNNING" ? "bold" : "fill"} />
      <span>{label || item.label}</span>
    </span>
  );
}

export function CommandButton({ children, primary, danger, quiet, disabled, onClick }) {
  const classes = [
    "ds-button",
    primary ? "ds-button--primary" : "",
    danger ? "ds-button--danger" : "",
    quiet ? "ds-button--quiet" : "",
  ].filter(Boolean).join(" ");
  return <button className={classes} disabled={disabled} onClick={onClick}>{children}</button>;
}

export function Panel({ title, badge, children, className = "" }) {
  return (
    <section className={`ds-panel ${className}`.trim()}>
      {(title || badge) && (
        <header className="ds-panel__header">
          <h2>{title}</h2>
          {badge}
        </header>
      )}
      {children}
    </section>
  );
}

export function ProductHeader({ product = "СТАНЦИЯ", title, subtitle, engineering, onBack, onEngineering }) {
  return (
    <header className="station-topbar">
      <div className="station-brand">
        <img src="/assets/brand/current/miltech-mark-ui.png" alt="МилТех" />
        <div>
          <b>МИЛТЕХ СТАНЦИЯ</b>
          <small>{product}</small>
        </div>
      </div>
      <div className="station-context">
        <div className="station-context__copy">
          <b>{title}</b>
          <small>{subtitle}</small>
        </div>
        <span className="demo-chip">ПРОТОТИП · ДЕМО-ДАННЫЕ</span>
        {engineering && <span className="engineering-chip"><Wrench weight="fill" /> ИНЖЕНЕРНЫЙ РЕЖИМ</span>}
      </div>
      <div className="station-topbar__actions">
        {onBack && <CommandButton quiet onClick={onBack}><ArrowLeft /> НАЗАД</CommandButton>}
        {onEngineering && <CommandButton quiet onClick={onEngineering}><Wrench /> F12 · ИНЖЕНЕРНЫЙ</CommandButton>}
      </div>
    </header>
  );
}

export function Footer({ left, right = "MilTech Station · UX prototype" }) {
  return <footer className="station-footer"><span>{left}</span><span>{right}</span></footer>;
}

export function SessionCommandBar({ status = "RUNNING", progress, onBack, onStop, onPrimary, primaryLabel = "СЛЕДУЮЩИЙ ШАГ", paused, onPause }) {
  return (
    <div className="session-commandbar">
      <div className="session-commandbar__state">
        <StatusBadge status={status} />
        <b>{progress}</b>
      </div>
      {onBack && <CommandButton onClick={onBack}><ArrowLeft /> НАЗАД</CommandButton>}
      {onPause && <CommandButton onClick={onPause}>{paused ? <Play /> : <Pause />} {paused ? "ПРОДОЛЖИТЬ" : "ПАУЗА"}</CommandButton>}
      <CommandButton danger onClick={onStop}><ShieldCheck /> БЕЗОПАСНО ОСТАНОВИТЬ</CommandButton>
      {onPrimary && <CommandButton primary onClick={onPrimary}>{primaryLabel}</CommandButton>}
    </div>
  );
}
