# Prototype Instructions

Run the local server yourself and open the preview in the browser available to this environment. Do not give the user server-start instructions when you can run it.

Before making substantial visual changes, use the Product Design plugin's `get-context` skill when the visual source is unclear or no longer matches the current goal. When the user gives durable prototype-specific design feedback, preferences, or decisions, record them in `AGENTS.md`.

## Confirmed product and design direction

- This is native desktop industrial software, not a website: no burger menu, browser-like tabs, rounded SaaS cards, glass, or decorative gradients.
- Use the first generated direction for production and test screens; use the third generated direction for engineering/admin screens.
- «МилТех Станция» is the core framework. «КТМА» is a loaded overlay, not the core name. «Орбита» appears only as a specific plugin/source.
- Delivery means staged development and deployment, not commercial pricing tiers.
- Show separate screens/windows for Station, KTMA, test stages, database, reports, visual scenario constructor, full scenario editor, and address diagnostics.
- The full KTMA scope includes UBSI, BSI, individual cells, intermediate measurements, and report automation.
- Test mode uses a blue-black industrial theme. Administrative/engineering mode may use a dense engineering layout.
- The operator can use a simplified puzzle-like scenario constructor. A full editor exists separately.

When implementing from a selected generated mock, treat that image as the source of truth for layout, component anatomy, density, spacing, color, typography, visible content, and hierarchy.

Build app UI in `src/`. Keep `.openai/hosting.json`, `worker/index.js`, `scripts/prepare-sites-build.mjs`, and `tests/sites-worker.test.mjs` intact so the same local prototype can be handed to Sites. Before a Sites handoff, run `npm run build` and `npm run test:sites`; the build must leave `dist/client/index.html`, `dist/server/index.js`, and `dist/.openai/hosting.json`.
