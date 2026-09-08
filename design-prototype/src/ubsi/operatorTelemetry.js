import { useEffect, useMemo, useRef, useState } from "react";

export const YALK_CHANNELS = [
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

export const CHECK_GROUPS = [
  { id: "power", title: "Питание / потребление" },
  { id: "yalk-analog", title: "ЯЛК · аналоговые", note: "0 / 3,1 / 6,2 В" },
  { id: "yalk-contact", title: "ЯЛК · контактные", note: "0 / 0,9 / 2,5 В" },
  { id: "yalk-open", title: "ЯЛК · обрыв" },
  { id: "yalk-overload", title: "ЯЛК · перегрузка ±12 В" },
  { id: "ytp", title: "ЯТП", note: "0 / 120 / 240 Ω" },
  { id: "yvp", title: "ЯВП-8", note: "коэффициент / АЧХ" },
];

export const FULL_ROUTE = [
  { id: "power", group: "power", title: "Питание / потребление" },
  { id: "yalk-cal", group: "yalk-analog", title: "ЯЛК · калибровка 97 / 99", yalk: true, point: 6.2 },
  { id: "yalk-0", group: "yalk-analog", title: "ЯЛК · аналоговые · точка 0 В", yalk: true, point: 0 },
  { id: "yalk-31", group: "yalk-analog", title: "ЯЛК · аналоговые · точка 3,1 В", yalk: true, point: 3.1 },
  { id: "yalk-62", group: "yalk-analog", title: "ЯЛК · аналоговые · точка 6,2 В", yalk: true, point: 6.2 },
  { id: "yalk-contact-0", group: "yalk-contact", title: "ЯЛК · контакты · 0 В → 0", yalk: true, point: 0, contact: true, expectedSignal: 0 },
  { id: "yalk-contact-09", group: "yalk-contact", title: "ЯЛК · контакты · 0,9 В → 0", yalk: true, point: 0.9, contact: true, expectedSignal: 0 },
  { id: "yalk-contact-25", group: "yalk-contact", title: "ЯЛК · контакты · 2,5 В → 1", yalk: true, point: 2.5, contact: true, expectedSignal: 1 },
  { id: "yalk-open", group: "yalk-open", title: "ЯЛК · обрыв", yalk: true, point: 0, open: true },
  { id: "yalk-overload-plus", group: "yalk-overload", title: "ЯЛК · перегрузка +12 В", yalk: true, overload: true, polarity: "+12 В" },
  { id: "yalk-overload-minus", group: "yalk-overload", title: "ЯЛК · перегрузка −12 В", yalk: true, overload: true, polarity: "−12 В" },
  { id: "yalk-ref", group: "yalk-analog", title: "ЯЛК · эталон 6,2 ±0,03 В", yalk: true, point: 6.2, reference: true },
  { id: "ytp-0", group: "ytp", title: "ЯТП · 0 Ω", ytp: true, pointOhm: 0 },
  { id: "ytp-120", group: "ytp", title: "ЯТП · 120 Ω", ytp: true, pointOhm: 120, manual: true },
  { id: "ytp-240", group: "ytp", title: "ЯТП · 240 Ω", ytp: true, pointOhm: 240, manual: true },
  { id: "yvp", group: "yvp", title: "ЯВП-8 · коэффициент / АЧХ", yvp: true },
  { id: "cleanup", group: "system", title: "Безопасный сброс" },
];

export function routeForSelection(full, selectedGroups) {
  if (full) return FULL_ROUTE;
  const selected = new Set(selectedGroups);
  const route = FULL_ROUTE.filter((item) => selected.has(item.group));
  if (route.length && !route.some((item) => item.id === "cleanup")) route.push(FULL_ROUTE[FULL_ROUTE.length - 1]);
  return route;
}

function noise(channel, tick, scale) {
  return (Math.sin(channel * 0.81 + tick * 0.32) + Math.sin(channel * 0.17 + tick * 0.09) * 0.45) * scale;
}

function clamp(value, low, high) {
  return Math.max(low, Math.min(high, value));
}

export function useOperatorTelemetry({ running, operation, fault = "normal", selectedChannel }) {
  const [tick, setTick] = useState(0);
  const [history, setHistory] = useState(() => YALK_CHANNELS.map(() => []));
  const [consumption, setConsumption] = useState([]);
  const runningRef = useRef(running);
  runningRef.current = running;

  useEffect(() => {
    const timer = window.setInterval(() => {
      if (runningRef.current && fault !== "stale" && fault !== "stand-error") setTick((value) => value + 1);
    }, 260);
    return () => window.clearInterval(timer);
  }, [fault]);

  const current = useMemo(() => YALK_CHANNELS.map((channel) => {
    const point = typeof operation?.point === "number" ? operation.point : 3.1;
    const drift = noise(channel, tick, 0.0028);
    let volts = clamp(point + drift, 0, 6.25);
    let deviation = (volts - point) / 6.2 * 100;
    let failed = false;
    if (fault === "not-normal" && channel === selectedChannel && operation?.yalk && !operation?.overload) {
      volts = clamp(point + 0.039, 0, 6.25);
      deviation = (volts - point) / 6.2 * 100;
      failed = true;
    }
    const signal = operation?.contact ? (operation.expectedSignal ? 1 : 0) : (volts >= 2.5 ? 1 : 0);
    return { channel, volts, deviation, raw: Math.round(volts / 6.2 * 1023), signal, failed };
  }), [fault, operation, selectedChannel, tick]);

  const overload = useMemo(() => {
    const stressedChannel = (Math.floor(tick / 5) % 88) + 1;
    return Array.from({ length: 88 }, (_, index) => {
      const channel = index + 1;
      let deltaCode = Math.round(noise(channel, tick, 1.1));
      if (fault === "overload-fail" && channel === 41) deltaCode = 4;
      const baselineV = 3.1 + noise(channel, 0, 0.0015);
      const volts = baselineV + deltaCode * (6.2 / 1023);
      return { channel, deltaCode, volts, stressedChannel, failed: Math.abs(deltaCode) > 2 };
    });
  }, [fault, tick]);

  useEffect(() => {
    if (!running || !operation?.yalk || fault === "stale" || fault === "stand-error") return;
    if (operation.overload) {
      setHistory((rows) => Array.from({ length: 88 }, (_, index) => [...(rows[index] || []).slice(-79), overload[index].volts]));
    } else {
      setHistory((rows) => current.map((item, index) => [...(rows[index] || []).slice(-79), item.volts]));
    }
  }, [current, fault, operation, overload, running, tick]);

  useEffect(() => {
    if (!running || fault === "stale" || fault === "stand-error") return;
    const phase = tick % 120;
    const voltage = phase < 12 ? 19 : phase < 34 ? 24 : phase < 70 ? 27 : phase < 96 ? 35 : 37;
    const currentA = 0.28 + Math.sin(tick * 0.16) * 0.016 + (voltage >= 35 ? 0.018 : 0);
    setConsumption((rows) => [...rows.slice(-99), { voltage, currentA, tick }]);
  }, [fault, running, tick]);

  const resetHistory = () => setHistory(operation?.overload ? Array.from({ length: 88 }, () => []) : YALK_CHANNELS.map(() => []));

  return { tick, current, overload, history, consumption, resetHistory };
}
