import React from "react";
import { createRoot } from "react-dom/client";
import { UbsiReviewPrototype } from "./ubsi/UbsiReviewPrototype.jsx";
import "./ubsi/ubsi-review-prototype.css";

createRoot(document.getElementById("root")).render(
  <React.StrictMode>
    <UbsiReviewPrototype />
  </React.StrictMode>,
);
