import { useEffect, useState } from "react";
import {
  AcceptanceHome,
  EngineeringDrawer,
  KtmaHub,
  ProductionHome,
  ProductionSession,
  StationHome,
} from "./v1/screens.jsx";
import {
  AdminEditor,
  RecoveryLab,
  ReportViewer,
  ScenarioDock,
  SingleProductStartup,
} from "./v1/scenarios.jsx";
import { AcceptanceSessionV2 } from "./v1/sessionsV2.jsx";
import { AdminWorkspace } from "./v1/workspaces.jsx";

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
    "single-product": "station",
    recovery: "ktma",
    report: "acceptance",
    "admin-editor": "admin",
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
  else if (route === "admin") screen = <AdminWorkspace go={go} back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "production-session") screen = <ProductionSession back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "acceptance-session") screen = <AcceptanceSessionV2 go={go} back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "single-product") screen = <SingleProductStartup go={go} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "recovery") screen = <RecoveryLab back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "report") screen = <ReportViewer back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "admin-editor") screen = <AdminEditor back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else screen = <StationHome go={go} engineering={engineering} toggleEngineering={toggleEngineering} />;

  return <div className={`station-v1 ${engineering ? "station-v1--engineering" : ""}`}>
    {screen}
    <EngineeringDrawer open={engineering} route={route} onClose={() => setEngineering(false)} />
    <ScenarioDock route={route} go={go} />
  </div>;
}