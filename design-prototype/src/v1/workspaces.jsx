import {
  ArrowRight,
  Database,
  FileText,
  ListChecks,
  ShieldCheck,
  SlidersHorizontal,
  TreeStructure,
} from "@phosphor-icons/react";
import { Footer, Panel, ProductHeader, StatusBadge } from "./designSystem.jsx";

function Page({ children, footer }) {
  return <div className="station-page"><div className="station-page__body">{children}</div><Footer left={footer} /></div>;
}

function AdminAction({ icon: Icon, title, detail, status = "READY", onClick }) {
  return <button className="admin-workspace-action" onClick={onClick} disabled={!onClick}>
    <Icon />
    <div><b>{title}</b><span>{detail}</span></div>
    <StatusBadge status={status} />
    <ArrowRight />
  </button>;
}

export function AdminWorkspace({ go, back, engineering, toggleEngineering }) {
  return <>
    <ProductHeader product="КТМА · АДМИНИСТРИРОВАНИЕ" title="Эксплуатационная конфигурация" subtitle="изделия, состав, история и профили" engineering={engineering} onBack={back} onEngineering={toggleEngineering} />
    <Page footer="Администрирование · нормальный эксплуатационный UI не раскрывает backend-схему и не смешивается с F12-диагностикой.">
      <div className="hero-row"><div><span className="ds-eyebrow">АДМИНИСТРИРОВАНИЕ</span><h1>Объекты и конфигурация</h1><p>Выберите эксплуатационную задачу. Инженерные диагностические инструменты остаются отдельным F12-слоем.</p></div><StatusBadge status="READY" label="КОНФИГУРАЦИЯ ДОСТУПНА" /></div>
      <div className="admin-workspace-grid">
        <Panel title="ИЗДЕЛИЯ И СОСТАВ">
          <AdminAction icon={TreeStructure} title="КАРТОЧКИ ИЗДЕЛИЙ" detail="серийные номера, состав, замены и связанные проверки" onClick={() => go("admin-editor")} />
          <AdminAction icon={ListChecks} title="ИСТОРИЯ ПРОВЕРОК" detail="запуски, результаты и повторно открытые проверки" onClick={() => go("report")} />
        </Panel>
        <Panel title="СТАНЦИЯ И ПРОФИЛИ">
          <AdminAction icon={SlidersHorizontal} title="ПРОФИЛИ СТЕНДА" detail="разрешённые ресурсы и эксплуатационные настройки" onClick={() => go("admin-editor")} />
          <AdminAction icon={Database} title="АРХИВ И ОТЧЁТЫ" detail="поиск по изделию и запуску без представления backend-таблиц" onClick={() => go("report")} />
        </Panel>
        <Panel title="ДОСТУП И ОТВЕТСТВЕННОСТЬ">
          <AdminAction icon={ShieldCheck} title="УРОВНИ ДОПУСКА" detail="права не являются рабочими режимами и не заменяют F12" status="ACTION" />
          <AdminAction icon={FileText} title="ЖУРНАЛ ИЗМЕНЕНИЙ" detail="кто, что и когда изменил в эксплуатационной конфигурации" onClick={() => go("admin-editor")} />
        </Panel>
      </div>
    </Page>
  </>;
}