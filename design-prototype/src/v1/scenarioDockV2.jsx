export function ScenarioDockV2({ route, go }) {
  const items = [
    ["station", "ОСНОВНОЙ UX"],
    ["single-product", "1 ПРОДУКТ"],
    ["recovery", "ОШИБКИ"],
    ["edge-cases", "EDGE+"],
    ["report", "ОТЧЁТ"],
    ["admin-editor", "ADMIN EDIT"],
  ];
  return <nav className="scenario-dock" aria-label="Навигация по сценариям UX-прототипа">
    <span>UX SCENARIOS</span>
    {items.map(([id, label]) => <button key={id} className={route === id ? "active" : ""} onClick={() => go(id)}>{label}</button>)}
  </nav>;
}