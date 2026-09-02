# Design QA — минимальная поставка УЛК

- Source visual truth: `C:\Users\МилТех\Downloads\ChatGPT Image 2 сент. 2026 г., 15_33_26.png`
- Implementation screenshot: `C:\qt_repos_2\liborbita\build\ui-operator-ytp120-v2.png`
- Combined comparison: `C:\qt_repos_2\liborbita\build\design-comparison.png`
- Viewport and pixels: source 1664×935; implementation QWidget 1664×935; density 1:1
- State: operator mode, ЯТП, fixed 120 Ω, equipment ready simulation

**Findings**

- [P1] A trustworthy Windows capture of the current build is missing.
  The Qt offscreen renderer substituted Cyrillic glyphs with boxes. The previous
  installed UI proves that Windows itself has working Cyrillic fonts, but the
  current light build has not been visually captured from the real desktop.
  Fix: open the installed release on the stand and capture the operator screen.
- [P2] The reference presents selection and active-run states as separate panels;
  the implementation intentionally combines selection, readiness, graph and
  results in one adaptive operator page. This keeps the live channel graph visible
  and avoids extra navigation, but must be confirmed by the operator.
- [P3] Card icon treatment is simpler than the circular colored icons in the mock.
  Existing project vector icons are reused; no substitute raster assets were added.

**Required fidelity surfaces**

- Fonts and typography: Segoe/Qt system hierarchy and sizes implemented; visual
  verification blocked by offscreen Cyrillic font substitution.
- Spacing and layout rhythm: three equal mode cards, compact left status/stage rail
  and 70% results area match the target hierarchy.
- Colors and visual tokens: white canvas, navy text, blue primary, purple ЯТП,
  teal combined and green statuses implemented.
- Image quality and assets: existing vector icons are used; no raster placeholders.
- Copy and content: narrowed to ЯЛК, ЯТП and combined mode; adapter/ISD status,
  fixed 120 Ω, live channels and report are explicit.

**Comparison history**

1. Initial comparison found a dark universal screen, irrelevant equipment and
   large unused readiness space.
2. Fixed by introducing the light card layout, hiding legacy toolbar in operator
   mode, limiting equipment, moving stage progress left, and expanding live results.
3. Post-fix comparison confirms the intended hierarchy and palette; final font and
   actual-Windows rendering remain unverified.

**Implementation checklist**

- Capture the installed operator screen on Windows.
- Confirm the single-page interpretation of the four-panel presentation mock.
- Re-run ЯТП after adapter power cycle and ЯЛК after stable ISD HTTP response.

final result: blocked
