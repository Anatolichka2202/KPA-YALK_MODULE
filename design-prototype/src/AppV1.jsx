import { useEffect, useState } from "react";
import {
  AcceptanceHome,
  AcceptanceSession,
  AdminHome,
  EngineeringDrawer,
  KtmaHub,
  ProductionHome,
  ProductionSession,
  StationHome,
} from "./v1/screens.jsx";

export function AppV1() {
  const [route, setRoute] = useState("station");
  const [engineering, setEngineering] = useState(false);
  const history = {
    station: null,
    ktma: "station",
    production: "ktma",
    acceptance: "ktma",
    admin: "ktma",
    "production-session": "production",
    "acceptance-session": "acceptance",
  };

  const go = (next) => setRoute(next);
  const back = () => setRoute(history[route] || "station");
  const toggleEngineering = () => setEngineering((value) => !value);

  useEffect(() => {
    const onKeyDown = (event) => {
      if (event.key === "F12") {
        event.preventDefault();
        toggleEngineering();
      }
      if (event.key === "Escape" && engineering) {
        setEngineering(false);
      }
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [engineering]);

  let screen;
  if (route === "station") screen = <StationHome go={go} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "ktma") screen = <KtmaHub go={go} back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "production") screen = <ProductionHome go={go} back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "acceptance") screen = <AcceptanceHome go={go} back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "admin") screen = <AdminHome back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "production-session") screen = <ProductionSession back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else screen = <AcceptanceSession back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;

  return <div className={`station-v1 ${engineering ? "station-v1--engineering" : ""}`}>
    {screen}
    <EngineeringDrawer open={engineering} route={route} onClose={() => setEngineering(false)} />
  </div>;
}
