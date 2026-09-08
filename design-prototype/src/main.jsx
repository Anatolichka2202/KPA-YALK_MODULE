import React from "react";
import { createRoot } from "react-dom/client";
import { UbsiHmiPrototype } from "./ubsi/UbsiHmiPrototype.jsx";
import "./ubsi/ubsi-hmi-prototype.css";

createRoot(document.getElementById("root")).render(
  <React.StrictMode>
    <UbsiHmiPrototype />
  </React.StrictMode>,
);
