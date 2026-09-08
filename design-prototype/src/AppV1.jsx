import { useEffect, useState } from "react";
import {
  AcceptanceHome,
  EngineeringDrawer,
  KtmaHub,
  StationHome,
} from "./v1/screens.jsx";
import {
  AdminEditor,
  RecoveryLab,
  ReportViewer,
  SingleProductStartup,
} from "./v1/scenarios.jsx";
import { EdgeCaseLab } from "./v1/edgeCasesV2.jsx";
import { ScenarioDockV2 } from "./v1/scenarioDockV2.jsx";
import { AcceptanceSessionV2 } from "./v1/sessionsV2.jsx";
import { ProductionLedger, ProductionSessionV2 } from "./v1/productionSessionV2.jsx";
import { ProductionHomeV3 } from "./v1/productionHomeV3.jsx";
import { AdminWorkspace } from "./v1/workspaces.jsx";
import "./production-stage-v3.css";

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
    "production-ledger": "production-session",
    "production-ledger-fault": "production-session",
    "acceptance-session": "acceptance",
    "single-product": "station",
    recovery: "ktma",
    "edge-cases": "station",
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
  else if (route === "production") screen = <ProductionHomeV3 go={go} back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "acceptance") screen = <AcceptanceHome go={go} back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "admin") screen = <AdminWorkspace go={go} back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "production-session") screen = <ProductionSessionV2 go={go} back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "production-ledger") screen = <ProductionLedger back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "production-ledger-fault") screen = <ProductionLedger fault back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "acceptance-session") screen = <AcceptanceSessionV2 go={go} back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "single-product") screen = <SingleProductStartup go={go} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "recovery") screen = <RecoveryLab back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "edge-cases") screen = <EdgeCaseLab back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "report") screen = <ReportViewer back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else if (route === "admin-editor") screen = <AdminEditor back={back} engineering={engineering} toggleEngineering={toggleEngineering} />;
  else screen = <StationHome go={go} engineering={engineering} toggleEngineering={toggleEngineering} />;

  return <div className={`station-v1 ${engineering ? "station-v1--engineering" : ""}`}>
    {screen}
    <EngineeringDrawer open={engineering} route={route} onClose={() => setEngineering(false)} />
    <ScenarioDockV2 route={route} go={go} />
  </div>;
}
