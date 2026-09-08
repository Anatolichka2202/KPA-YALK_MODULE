import React from "react";
import { createRoot } from "react-dom/client";
import { UbsiOperatorApp } from "./ubsi/UbsiOperatorApp.jsx";
import "./ubsi/ubsi-operator.css";

createRoot(document.getElementById("root")).render(
  <React.StrictMode>
    <UbsiOperatorApp />
  </React.StrictMode>,
);
