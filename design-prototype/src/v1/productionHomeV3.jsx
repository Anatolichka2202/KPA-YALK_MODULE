import { useState } from "react";
import {
  ArrowRight,
  BatteryCharging,
  Circuitry,
  Gauge,
  Play,
  Waveform,
} from "@phosphor-icons/react";
import {
  CommandButton,
  Footer,
  Panel,
  ProductHeader,
  StatusBadge,
} from "./designSystem.jsx";

const productionStages = [
  "Первичное",
  "Климат НУ",
  "Климат +",
  "Климат −",
  "Заливка · климат НУ",
  "Заливка · климат +",
  "Заливка · климат −",
];

const packages = [
  { id: "full", title: "ПОЛНАЯ ПРОВЕРКА УБСИ", note: "питание · ЯЛК · ЯТП · ЯВП", icon: Circuitry },
  { id: "power", title: "ПИТАНИЕ / ПОТРЕБЛЕНИЕ", note: "рабочие точки и ток", icon: BatteryCharging },
  { id: "yalk", title: "ЯЛК", note: "каналы, контакты, обрыв, перегрузка", icon: Waveform },
  { id: "ytp", title: "ЯТП", note: "30 каналов · 0 / 120 / 240 Ω", icon: Gauge },
  { id: "yvp", title: "ЯВП", note: "8 каналов · коэффициент и АЧХ", icon: Waveform },
];

export function ProductionHomeV3({ go, back, engineering, toggleEngineering, stage, onStageChange }) {
  const [testPackage, setTestPackage] = useState("full");
  const selectedPackage = packages.find((item) => item.id === testPackage) || packages[0];

  return <>
    <ProductHeader
      product="КТМА · ПРОИЗВОДСТВО"
      title="УБСИ · производственная проверка"
      subtitle="изделие, этап и пакет проверки выбираются независимо"
      engineering={engineering}
      onBack={back}
      onEngineering={toggleEngineering}
    />
    <div className="station-page">
      <div className="station-page__body">
        <div className="hero-row">
          <div>
            <span className="ds-eyebrow">ПРОИЗВОДСТВО</span>
            <h1>Подготовить производственный запуск</h1>
            <p>Этап относится к lifecycle изделия. Измерительная процедура выполняется обычным непрерывным маршрутом и не превращается в лестницу из шагов.</p>
          </div>
          <StatusBadge status="READY" label="ГОТОВО К НАСТРОЙКЕ" />
        </div>

        <div className="two-column-grid">
          <Panel title="ИЗДЕЛИЕ И СОСТАВ" badge={<StatusBadge status="READY" label="СОСТАВ ПОДТВЕРЖДЁН" />}>
            <div className="form-readonly">
              <label><span>ИЗДЕЛИЕ</span><b>УБСИ-468157-009 · DEMO</b></label>
              <label><span>ЯЛК-96</span><b>SN 96-00427 · DEMO</b></label>
              <label><span>ЯТП</span><b>SN ТП-00184 · DEMO</b></label>
              <label><span>ЯВП-8</span><b>SN ВП-00041 · DEMO</b></label>
              <label><span>ЯП-П</span><b>SN ПП-00018 · DEMO</b></label>
            </div>
          </Panel>

          <Panel title="ЭТАП ПРОИЗВОДСТВА" badge={<span className="ds-counter">1 ВЫБРАН</span>}>
            <div className="stage-choice-v3">
              {productionStages.map((item) => (
                <button
                  key={item}
                  className={item === stage ? "active" : ""}
                  onClick={() => onStageChange(item)}
                >
                  <span>{item}</span>
                  {item === stage && <StatusBadge status="READY" label="ВЫБРАН" />}
                </button>
              ))}
            </div>
          </Panel>
        </div>

        <Panel title="ПРОВЕРКА / ПАКЕТ" badge={<span className="ds-counter">ОБЩИЕ PROCEDURES</span>}>
          <div className="package-choice-v3">
            {packages.map(({ id, title, note, icon: Icon }) => (
              <button
                key={id}
                className={id === testPackage ? "active" : ""}
                onClick={() => setTestPackage(id)}
              >
                <Icon />
                <div><b>{title}</b><span>{note}</span></div>
                {id === testPackage ? <StatusBadge status="READY" label="ВЫБРАНО" /> : <ArrowRight />}
              </button>
            ))}
          </div>
        </Panel>

        <div className="production-launch-summary-v3">
          <div>
            <span>ИЗДЕЛИЕ</span>
            <b>УБСИ-468157-009 · DEMO</b>
          </div>
          <div>
            <span>ЭТАП</span>
            <b>{stage}</b>
          </div>
          <div>
            <span>ПРОВЕРКА</span>
            <b>{selectedPackage.title}</b>
          </div>
          <CommandButton primary onClick={() => go("production-session")}>
            <Play /> НАЧАТЬ
          </CommandButton>
        </div>
      </div>
      <Footer left="Production v3 · этап изделия и ход измерительной процедуры разделены." />
    </div>
  </>;
}
