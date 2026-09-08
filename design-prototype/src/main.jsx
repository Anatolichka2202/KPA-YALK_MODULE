import React from "react";
import { createRoot } from "react-dom/client";
import { UbsiSupplyApp } from "./ubsi/UbsiSupplyApp.jsx";
import "./ubsi/ubsi-supply.css";
import "./ubsi/ubsi-supply-overrides.css";

createRoot(document.getElementById("root")).render(
  <React.StrictMode>
    <UbsiSupplyApp />
  </React.StrictMode>,
);
