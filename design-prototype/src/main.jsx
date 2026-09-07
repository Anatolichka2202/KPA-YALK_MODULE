import React from "react";
import { createRoot } from "react-dom/client";
import { AppV1 } from "./AppV1.jsx";
import "./station-v1.css";
import "./station-v1-safety.css";
import "./station-v1-scenarios.css";

createRoot(document.getElementById("root")).render(
  <React.StrictMode>
    <AppV1 />
  </React.StrictMode>,
);
