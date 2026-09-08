import { useEffect, useMemo, useRef, useState } from "react";

export const YALK_ADDRESSES = [
  ...Array.from({ length: 28 }, (_, i) => i + 1),
  ...Array.from({ length: 12 }, (_, i) => i + 32),
  ...Array.from({ length: 26 }, (_, i) => i + 45),
  ...Array.from({ length: 14 }, (_, i) => i + 74),
];

export const PRODUCTION_STAGES = [
  "Первичное",
  "Климат НУ",
  "Климат +",
  "Климат −",
  "Заливка · климат НУ",
  "Заливка · климат +",
  "Заливка · климат −",
];

export const YVP_FREQUENCIES = [0.15, 20, 250, 500, 1800, 2000, 4000];

export const RUN_OPERATIONS = [
  { id: "power", title: "Питание / потребление", short: "ПИТАНИЕ" },
  { id: "yalk-cal", title: "ЯЛК · калибровка 97 / 99", short: "КАЛИБРОВКА", point: 6.2 },
  { id: "yalk-zero", title: "ЯЛК · аналоговые входы · 0 В", short: "ЯЛК 0 В", point: 0 },
  { id: "yalk-mid", title: "ЯЛК · аналоговые входы · 3,1 В", short: "ЯЛК 3,1 В", point: 3.1 },
  { id: "yalk-full", title: "ЯЛК · аналоговые входы · 6,2 В", short: "ЯЛК 6,2 В", point: 6.2 },
  { id: "yalk-contact", title: "ЯЛК · контактные пороги 0 / 0,9 / 2,5 В", short: "КОНТАКТЫ" },
  { id: "yalk-open", title: "ЯЛК · обрыв", short: "ОБРЫВ", point: 0 },
  { id: "yalk-overload", title: "ЯЛК · перегрузка ±12 В", short: "±12 В", overload: true },
  { id: "yalk-ref", title: "ЯЛК · эталон 6,2 ±0,03 В", short: "ЭТАЛОН 6,2", point: 6.2 },
  { id: "ytp", title: "ЯТП · 0 / 120 / 240 Ω", short: "ЯТП", manual: true },
  { id: "yvp", title: "ЯВП-8 · коэффициент / АЧХ", short: "ЯВП", commissioning: true },
  { id: "cleanup", title: "Безопасный сброс", short: "СБРОС" },
];

function deterministicNoise(index, tick, span = 1) {
  const a = Math.sin(index * 0.73 + tick * 0.31);
  const b = Math.sin(index * 1.91 + tick * 0.11) * 0.45;
  return (a + b) * span;
}

function pointForOperation(operation, tick) {
  if (operation?.id === "yalk-contact") {
    const points = [0, 0.9, 2.5];
    return points[Math.floor(tick / 12) % points.length];
  }
  if (typeof operation?.point === "number") return operation.point;
  return 3.1;
}

export function useDemoTelemetry({ running, operation, faultMode = "normal", selectedAddress = 47 }) {
  const [tick, setTick] = useState(0);
  const [history, setHistory] = useState([]);
  const [background, setBackground] = useState([]);
  const [consumption, setConsumption] = useState([]);
  const runningRef = useRef(running);
  runningRef.current = running;

  useEffect(() => {
    const timer = window.setInterval(() => {
      if (runningRef.current && faultMode !== "stale" && faultMode !== "stand-error") {
        setTick((value) => value + 1);
      }
    }, 280);
    return () => window.clearInterval(timer);
  }, [faultMode]);

  const stimulusPoint = pointForOperation(operation, tick);
  const channels = useMemo(() => {
    return YALK_ADDRESSES.map((address, index) => {
      const baseDeviation = deterministicNoise(index, tick, 0.055);
      const drift = Math.sin(tick * 0.035 + index * 0.17) * 0.018;
      let deviation = baseDeviation + drift;
      let isFault = false;
      if (faultMode === "not-normal" && address === selectedAddress) {
        deviation = 0.62 + Math.sin(tick * 0.2) * 0.04;
        isFault = true;
      }
      const actual = stimulusPoint + (deviation / 100) * 6.2;
      return {
        address,
        actual,
        deviation,
        signal: stimulusPoint >= 2.5,
        raw: Math.round(510 + actual / 6.2 * 510 + deterministicNoise(index, tick, 1.2)),
        code: Math.round(actual / 6.2 * 1023),
        isFault,
      };
    });
  }, [faultMode, selectedAddress, stimulusPoint, tick]);

  const overload = useMemo(() => {
    const stressed = ((tick >> 2) % 88) + 1;
    const polarity = Math.floor(tick / (88 * 4)) % 2 === 0 ? "+12 В" : "−12 В";
    return Array.from({ length: 88 }, (_, index) => {
      const channel = index + 1;
      let delta = Math.round(deterministicNoise(index, tick, 1.15));
      if (faultMode === "overload-fail" && channel === 41) delta = 4;
      return { channel, delta, stressed, polarity, failed: Math.abs(delta) > 2 };
    });
  }, [faultMode, tick]);

  useEffect(() => {
    if (!running || faultMode === "stale" || faultMode === "stand-error") return;
    const selected = channels.find((item) => item.address === selectedAddress) || channels[0];
    setHistory((items) => [...items.slice(-59), {
      tick,
      value: selected.actual,
      deviation: selected.deviation,
    }]);
    setBackground((rows) => [...rows.slice(-17), channels.map((item) => item.deviation)]);

    const phase = tick % 110;
    let voltage = 27;
    if (phase < 10) voltage = 19;
    else if (phase < 30) voltage = 24;
    else if (phase < 55) voltage = 27;
    else if (phase < 75) voltage = 35;
    else if (phase < 88) voltage = 37;
    const current = 0.275 + Math.sin(tick * 0.18) * 0.018 + (voltage >= 35 ? 0.025 : 0);
    setConsumption((items) => [...items.slice(-79), { tick, voltage, current }]);
  }, [channels, faultMode, running, selectedAddress, tick]);

  const reset = () => {
    setTick(0);
    setHistory([]);
    setBackground([]);
    setConsumption([]);
  };

  return { tick, stimulusPoint, channels, overload, history, background, consumption, reset };
}
