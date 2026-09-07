import { useState } from "react";
import {
  ArrowRight,
  CheckCircle,
  Circuitry,
  ClockCounterClockwise,
  Lock,
  ShieldCheck,
  Warning,
  Wrench,
} from "@phosphor-icons/react";
import { CommandButton, Footer, Panel, ProductHeader, StatusBadge } from "./designSystem.jsx";

function Page({ children, footer }) {
  return <div className="station-page edge-page"><div className="station-page__body">{children}</div><Footer left={footer} /></div>;
}

function DemoNotice() {
  return <div className="scenario-demo-notice"><Warning weight="fill" /><span>Edge-case prototype: данные и идентификаторы демонстрационные; никаких запросов к стенду, backend или Qt-коду не выполняется.</span></div>;
}

function MultiProductState() {
  const [state, setState] = useState("partial");
  const products = state === "loading" ? [
    ["КТМА", "загрузка", "RUNNING"], ["ППБ", "ожидание", "UNAVAILABLE"], ["СУС", "ожидание", "UNAVAILABLE"], ["ВОРОНЕЖ", "ожидание", "UNAVAILABLE"],
  ] : state === "error" ? [
    ["КТМА", "загружена", "READY"], ["ППБ", "не описан в текущей UX-спеке", "UNAVAILABLE"], ["СУС", "ошибка загрузки продукта · DEMO", "ERROR"], ["ВОРОНЕЖ", "не описан в текущей UX-спеке", "UNAVAILABLE"],
  ] : [
    ["КТМА", "загружена", "READY"], ["ППБ", "предметный контур не раскрыт", "UNAVAILABLE"], ["СУС", "предметный контур не раскрыт", "UNAVAILABLE"], ["ВОРОНЕЖ", "предметный контур не раскрыт", "UNAVAILABLE"],
  ];
  return <div>
    <div className="edge-state-switch"><button className={state === "partial" ? "active" : ""} onClick={() => setState("partial")}>ЧАСТИЧНАЯ ПОСТАВКА</button><button className={state === "loading" ? "active" : ""} onClick={() => setState("loading")}>ЗАГРУЗКА</button><button className={state === "error" ? "active" : ""} onClick={() => setState("error")}>ОШИБКА ПРОДУКТА</button></div>
    <Panel title="ПРОДУКТЫ СТАНЦИИ" badge={<span className="ds-counter">4 КОНТУРА</span>}><div className="edge-product-list">{products.map(([name, detail, status]) => <div key={name}><Circuitry /><div><b>{name}</b><span>{detail}</span></div><StatusBadge status={status} /></div>)}</div></Panel>
    <div className="edge-rule"><ShieldCheck /><div><b>Shell продолжает работать при локальной ошибке продукта</b><span>Ошибка одной надстройки не должна автоматически маскировать состояние остальных продуктов или активных сеансов.</span></div></div>
  </div>;
}

function PermissionState() {
  const [requested, setRequested] = useState(false);
  return <div className="permission-layout">
    <Panel title="ПОПЫТКА ДЕЙСТВИЯ" badge={<StatusBadge status="UNAVAILABLE" label="НЕДОСТАТОЧНО ДОПУСКА" />}>
      <div className="permission-card"><Lock weight="fill" /><div><span>ЗАБЛОКИРОВАННОЕ ДЕЙСТВИЕ</span><h2>Опубликовать изменённый сценарий</h2><p>Рабочая задача остаётся открытой. Недостаток допуска не переключает пользователя в другой режим и не прерывает активный сеанс.</p></div></div>
      <div className="permission-facts"><div><span>ТЕКУЩИЙ УРОВЕНЬ</span><b>оператор · DEMO</b></div><div><span>ТРЕБУЕТСЯ</span><b>инженер/администратор · DEMO</b></div><div><span>F12</span><b>не является повышением прав</b></div></div>
      <div className="recovery-actions"><CommandButton onClick={() => setRequested(true)}><Wrench /> ЗАПРОСИТЬ ДОПУСК · ПРОТОТИП</CommandButton><CommandButton>ВЕРНУТЬСЯ БЕЗ ИЗМЕНЕНИЙ</CommandButton></div>
      {requested && <div className="edge-request-result"><CheckCircle weight="fill" /><span>Запрос визуально зафиксирован только внутри прототипа. Реальная схема авторизации/согласования должна быть подтверждена отдельно.</span></div>}
    </Panel>
    <Panel title="ПРАВИЛО"><div className="classification-list"><div><StatusBadge status="UNAVAILABLE" /><p>Permission denied сообщает о праве выполнить действие, а не о состоянии оборудования или качестве изделия.</p></div><div><StatusBadge status="ERROR" /><p>Не использовать ERROR стенда для отказа по правам.</p></div><div><StatusBadge status="BUSY" /><p>Не использовать ЗАНЯТО для отсутствия допуска.</p></div></div></Panel>
  </div>;
}

function RecoveryState() {
  const [action, setAction] = useState("none");
  return <div className="reopen-layout">
    <Panel title="НАЙДЕН НЕЗАВЕРШЁННЫЙ СЕАНС" badge={<StatusBadge status="INCOMPLETE" />}>
      <div className="reopen-head"><ClockCounterClockwise weight="duotone" /><div><h2>RUN-DEMO-005184</h2><p>КТМА · Производство · УБСИ-468157-009 · этап 02 · состояние сохранено до закрытия UI.</p></div></div>
      <div className="reopen-facts"><div><span>ПОСЛЕДНИЙ ШАГ</span><b>канал 57 / 96 · DEMO</b></div><div><span>РЕСУРСЫ</span><b>зарезервированы за сеансом · DEMO</b></div><div><span>ПОСЛЕДНИЙ СТАТУС</span><b>пауза/неполная</b></div></div>
      <div className="reopen-warning"><Warning weight="fill" /><span>Prototype не подтверждает реальный механизм persistence. Экран задаёт только UX-контракт: нельзя автоматически начинать новый сеанс поверх незавершённого.</span></div>
      <div className="recovery-actions"><CommandButton primary onClick={() => setAction("reopen")}><ArrowRight /> ВОССТАНОВИТЬ РАБОЧЕЕ ОКНО</CommandButton><CommandButton danger onClick={() => setAction("stop")}><ShieldCheck /> БЕЗОПАСНО ЗАВЕРШИТЬ</CommandButton></div>
      {action === "reopen" && <div className="edge-request-result"><CheckCircle weight="fill" /><span>UX-результат: Station возвращает оператора к сохранённому контексту и только после проверки свежести данных разрешает продолжение.</span></div>}
      {action === "stop" && <div className="edge-request-result edge-request-result--warn"><ShieldCheck weight="fill" /><span>UX-результат: сеанс переводится в остановленное состояние; ресурсы могут освобождаться только после подтверждённого безопасного состояния.</span></div>}
    </Panel>
    <Panel title="НЕЛЬЗЯ ДЕЛАТЬ"><div className="edge-prohibitions"><span>Не создавать молча новый RUN вместо найденного.</span><span>Не считать старые измерительные данные свежими после перезапуска.</span><span>Не освобождать ресурс визуально раньше подтверждения safe-state.</span><span>Не скрывать причину незавершённости сеанса.</span></div></Panel>
  </div>;
}

export function EdgeCaseLab({ back, engineering, toggleEngineering }) {
  const [tab, setTab] = useState("launcher");
  return <>
    <ProductHeader product="STATION · UX EDGE CASES" title="Системные исключения" subtitle="launcher, permissions и восстановление сеанса" engineering={engineering} onBack={back} onEngineering={toggleEngineering} />
    <Page footer="Edge cases v1 · проектируется UX-контракт; техническая реализация persistence/permissions остаётся вне design-prototype.">
      <div className="hero-row"><div><span className="ds-eyebrow">STATION SHELL · EDGE CASES</span><h1>Системные состояния вне happy-path</h1><p>Проверяем, что глобальный shell остаётся понятным при частичной загрузке, ограничении прав и незавершённом сеансе.</p></div></div>
      <div className="scenario-tabs edge-tabs"><button className={tab === "launcher" ? "active" : ""} onClick={() => setTab("launcher")}><StatusBadge status="READY" label="LAUNCHER" /><span>Несколько продуктов</span></button><button className={tab === "permission" ? "active" : ""} onClick={() => setTab("permission")}><StatusBadge status="UNAVAILABLE" /><span>Нет допуска</span></button><button className={tab === "reopen" ? "active" : ""} onClick={() => setTab("reopen")}><StatusBadge status="INCOMPLETE" /><span>Восстановить сеанс</span></button></div>
      {tab === "launcher" && <MultiProductState />}
      {tab === "permission" && <PermissionState />}
      {tab === "reopen" && <RecoveryState />}
      <DemoNotice />
    </Page>
  </>;
}