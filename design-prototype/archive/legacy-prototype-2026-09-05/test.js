const root = document.querySelector('#test-root');
const footerResult = document.querySelector('#footer-result');
const footerStep = document.querySelector('#footer-step');
const footerProgress = document.querySelector('#footer-progress');
const resistanceLayer = document.querySelector('#resistance-layer');
const resistanceConfirm = document.querySelector('#resistance-confirm');
const confirmResistance = document.querySelector('#confirm-resistance');

const stage = (label, value, state = '') => `<div class="stage ${state}"><i>${state === 'done' ? '✓' : state === 'current' ? '●' : '○'}</i><span>${label}</span><b>${value}</b></div>`;

function shell(active, content, note) {
  const modes = [
    ['yalk', 'ЯЛК-96', '80 каналов · точки 0 / 3,1 / 6,2 В'],
    ['ytp', 'ЯТП', '30 каналов · ручные точки Р4831'],
    ['combined', 'ЯЛК-96 + ЯТП', 'Последовательный полный прогон']
  ];
  return `<div class="session-head"><div><div class="eyebrow">ИСПЫТАТЕЛЬНАЯ СЕССИЯ УБСИ-01</div><h1>УБСИ · электрическая проверка</h1><p>Текущий этап изделия: разбор · методика ТУ 5.6</p></div><div class="object-data"><div><small>ЗАВОДСКОЙ НОМЕР</small><b>УБСИ-468157-009</b></div><div><small>РАБОЧЕЕ МЕСТО</small><b>Стенд 01</b></div><div><small>СОСТОЯНИЕ</small><b class="ok">ГОТОВ</b></div></div></div><div class="procedure-selector">${modes.map(([id,title,sub]) => `<button class="procedure ${active === id ? 'active' : ''}" data-mode="${id}"><b>${title}</b><small>${sub}</small></button>`).join('')}</div><div class="operator-note"><b>ОПЕРАТОРУ:</b> ${note}</div>${content}`;
}

function yalk() {
  const content = `<div class="work-grid"><div class="stack"><section class="panel"><div class="panel-head"><h2>ДИНАМИКА ТЕКУЩЕГО КАНАЛА</h2><span class="tag ok">АДРЕС 57 · ВЫПОЛНЯЕТСЯ</span></div><div class="plot"><span class="plot-label">В7 / ЯЛК · 16 СВЕЖИХ ОТСЧЁТОВ</span><svg viewBox="0 0 800 210" preserveAspectRatio="none" aria-label="График измерений"><polyline points="0,190 65,188 120,170 180,176 240,144 300,149 360,104 420,110 490,75 550,80 620,42 680,50 800,18" fill="none" stroke="#63a8ff" stroke-width="3"/><polyline points="0,194 65,192 120,174 180,180 240,148 300,153 360,108 420,114 490,79 550,84 620,46 680,54 800,22" fill="none" stroke="#55d29a" stroke-width="2" stroke-dasharray="6 5"/></svg></div><div class="legend"><span><i style="background:#63a8ff"></i>В7-78/1</span><span><i style="background:#55d29a"></i>ЯЛК-96</span></div></section><section class="panel"><div class="panel-head"><h2>РЕЗУЛЬТАТЫ КАНАЛОВ</h2><span class="eyebrow">ВИДНЫ ТОЛЬКО ОПЕРАТОРСКИЕ ДАННЫЕ</span></div><table class="table"><tr><th>АДРЕС</th><th>ТОЧКА</th><th>ЗАДАНО</th><th>В7, В</th><th>ЯЛК, В</th><th>ПОГРЕШНОСТЬ</th><th>ИТОГ</th></tr><tr class="selected"><td>57</td><td>3,1 В</td><td>3,100</td><td>3,107</td><td>3,105</td><td>-0,032 %</td><td class="ok">НОРМА</td></tr><tr><td>56</td><td>3,1 В</td><td>3,100</td><td>3,104</td><td>3,102</td><td>+0,018 %</td><td class="ok">НОРМА</td></tr><tr><td>59</td><td>3,1 В</td><td>3,100</td><td>3,112</td><td>3,110</td><td>+0,212 %</td><td class="ok">НОРМА</td></tr></table></section></div><aside class="stack"><section class="panel"><div class="panel-head"><h2>ЭТАПЫ ЯЛК-96</h2><span class="tag ok">57 / 80</span></div>${stage('Готовность стенда','НОРМА','done')}${stage('Калибровка','97 / 99','done')}${stage('Каналы ЯЛК','57 / 80','current')}${stage('Контактное состояние','ожидание')}${stage('Безопасный сброс','ожидание')}<div class="measurement"><small>ТЕКУЩЕЕ ИЗМЕРЕНИЕ</small><h3>АДРЕС 57</h3><div class="reading"><span>Задано</span><b>3,100 В</b><span>В7</span><b>3,107 В</b><span>ЯЛК</span><b>3,105 В</b><span>Погрешность</span><b class="ok">−0,032 %</b><span>Допуск</span><b>±0,5 %</b></div></div></section><section class="panel"><div class="panel-head"><h2>СОСТОЯНИЕ СЕССИИ</h2><span class="tag ok">ДАННЫЕ СВЕЖИЕ</span></div><div class="session-state"><div><small>Процедура</small><b>ЯЛК-96</b></div><div><small>Прогресс</small><b>71 %</b></div><div><small>Источник</small><b>адаптер + В7</b></div><div><small>Ошибки стенда</small><b class="ok">0</b></div></div></section></aside></div>`;
  return shell('yalk', content, 'идёт измерение адреса 57. Не изменяйте подключение до завершения точки.');
}

function ytp() {
  const points = [[1,'0 Ω','НОРМА','done'],[2,'120 Ω','ТРЕБУЕТСЯ','current'],[3,'240 Ω','ОЖИДАНИЕ','']];
  const content = `<div class="work-grid"><div class="stack"><section class="panel"><div class="panel-head"><h2>РУЧНЫЕ ТОЧКИ ЯТП</h2><span class="tag warn">ТОЧКА 2 / 3</span></div><div class="manual-points">${points.map(([n,v,s,state]) => `<div class="manual-point ${state}"><i>0${n}</i><span><b>${v}</b><small>${n === 2 ? 'магазин подключён к общему X123' : '30 каналов'}</small></span><strong class="${state === 'done' ? 'ok' : state === 'current' ? 'warn' : ''}">${s}</strong>${n === 2 ? '<button data-resistance>ВЫСТАВИТЬ СОПРОТИВЛЕНИЕ</button>' : ''}</div>`).join('')}</div></section><section class="panel"><div class="panel-head"><h2>РЕЗУЛЬТАТЫ КАНАЛОВ · 120 Ω</h2><span class="eyebrow">30 КАНАЛОВ</span></div><table class="table"><tr><th>КАНАЛ</th><th>ИЗМЕРЕНО, Ω</th><th>ОТКЛОНЕНИЕ</th><th>ДОПУСК</th><th>ИТОГ</th></tr><tr class="selected"><td>1</td><td>120,34</td><td>+0,28 %</td><td>±0,50 %</td><td class="ok">НОРМА</td></tr><tr><td>2</td><td>120,29</td><td>+0,24 %</td><td>±0,50 %</td><td class="ok">НОРМА</td></tr><tr><td>3</td><td>120,31</td><td>+0,26 %</td><td>±0,50 %</td><td class="ok">НОРМА</td></tr></table></section></div><aside class="stack"><section class="panel"><div class="panel-head"><h2>ЭТАПЫ ЯТП</h2><span class="tag warn">ОПЕРАТОР</span></div>${stage('Готовность стенда','НОРМА','done')}${stage('Точка 0 Ω','30 / 30','done')}${stage('Точка 120 Ω','Р4831','current')}${stage('Точка 240 Ω','ожидание')}${stage('Безопасный сброс','ожидание')}<div class="measurement"><small>СЛЕДУЮЩЕЕ ДЕЙСТВИЕ</small><h3>УСТАНОВИТЬ 120 Ω</h3><p>Откройте диалог, выставьте магазин и подтвердите фактическое значение.</p><button class="primary" data-resistance>ОТКРЫТЬ ДИАЛОГ Р4831</button></div></section><section class="panel"><div class="panel-head"><h2>СОСТОЯНИЕ СЕССИИ</h2><span class="tag ok">ПОТОК АКТИВЕН</span></div><div class="session-state"><div><small>Процедура</small><b>ЯТП</b></div><div><small>Прогресс</small><b>43 %</b></div><div><small>Источник</small><b>Р4831 + ЯТП</b></div><div><small>Ошибки стенда</small><b class="ok">0</b></div></div></section></aside></div>`;
  return shell('ytp', content, 'выставьте 120 Ω на магазине Р4831. Подтверждение откроется отдельным диалогом.');
}

function combined() {
  const content = `<div class="work-grid"><div class="stack"><section class="panel"><div class="panel-head"><h2>ПОСЛЕДОВАТЕЛЬНОСТЬ ПРОГОНА</h2><span class="tag ok">ЯЛК-96 ЗАВЕРШЕНА</span></div>${stage('ЯЛК-96 · готовность','НОРМА','done')}${stage('ЯЛК-96 · 80 каналов','НОРМА','done')}${stage('Переход к ЯТП','выполнен','done')}${stage('ЯТП · точка 120 Ω','оператор','current')}${stage('Общий отчёт','ожидание')}</section><section class="panel"><div class="panel-head"><h2>СВОДКА РЕЗУЛЬТАТОВ</h2><span class="eyebrow">ПОТОКИ ВЫПОЛНЯЮТСЯ ПОСЛЕДОВАТЕЛЬНО</span></div><table class="table"><tr><th>ПРОЦЕДУРА</th><th>ПОКРЫТИЕ</th><th>НЕ НОРМА</th><th>ОШИБКИ СТЕНДА</th><th>ИТОГ</th></tr><tr><td>ЯЛК-96</td><td>80 / 80</td><td>0</td><td>0</td><td class="ok">НОРМА</td></tr><tr class="selected"><td>ЯТП</td><td>30 / 30 · 1 / 3 точки</td><td>0</td><td>0</td><td class="warn">ВЫПОЛНЯЕТСЯ</td></tr></table></section></div><aside class="stack"><section class="panel"><div class="panel-head"><h2>ОБЩИЙ ПРОГРЕСС</h2><span class="tag warn">74 %</span></div><div class="measurement"><small>ТЕКУЩАЯ ПРОЦЕДУРА</small><h3>ЯТП · 120 Ω</h3><p>Для продолжения требуется ручная установка магазина сопротивлений.</p><button class="primary" data-resistance>ОТКРЫТЬ ДИАЛОГ Р4831</button></div></section><section class="panel"><div class="panel-head"><h2>ПРЕДВАРИТЕЛЬНЫЙ ИТОГ</h2><span class="tag warn">НЕ СФОРМИРОВАН</span></div><p>Итог появится после трёх точек ЯТП и безопасного сброса оборудования.</p></section></aside></div>`;
  return shell('combined', content, 'ЯЛК-96 завершена. Для продолжения ЯТП установите 120 Ω на магазине Р4831.');
}

const screens = { yalk, ytp, combined };

function render(mode) {
  root.innerHTML = screens[mode]();
  document.querySelectorAll('[data-mode]').forEach(button => button.addEventListener('click', () => render(button.dataset.mode)));
  document.querySelectorAll('[data-resistance]').forEach(button => button.addEventListener('click', openResistance));
  if (mode === 'yalk') { footerResult.textContent = 'ВЫПОЛНЯЕТСЯ'; footerStep.textContent = 'ЯЛК-96 · каналы 57 / 80'; footerProgress.style.width = '71%'; }
  if (mode === 'ytp') { footerResult.textContent = 'ТРЕБУЕТСЯ ОПЕРАТОР'; footerStep.textContent = 'ЯТП · точка 120 Ω · 13 / 30'; footerProgress.style.width = '43%'; }
  if (mode === 'combined') { footerResult.textContent = 'ВЫПОЛНЯЕТСЯ'; footerStep.textContent = 'ЯЛК-96 завершена · ЯТП 1 / 3'; footerProgress.style.width = '74%'; }
}

function openAdmin() {
  window.open('admin.html', 'miltech-station-admin', 'width=1500,height=900,resizable=yes,scrollbars=yes');
}

function openResistance() {
  resistanceLayer.classList.add('show');
  resistanceLayer.setAttribute('aria-hidden', 'false');
}

function closeResistance() {
  resistanceLayer.classList.remove('show');
  resistanceLayer.setAttribute('aria-hidden', 'true');
}

document.querySelector('#open-admin').addEventListener('click', openAdmin);
document.addEventListener('keydown', event => { if (event.key === 'F12') { event.preventDefault(); openAdmin(); } if (event.key === 'Escape') closeResistance(); });
document.querySelectorAll('[data-close-dialog]').forEach(button => button.addEventListener('click', closeResistance));
resistanceConfirm.addEventListener('change', () => { confirmResistance.disabled = !resistanceConfirm.checked; });
confirmResistance.addEventListener('click', () => { closeResistance(); footerResult.textContent = 'ВЫПОЛНЯЕТСЯ'; });
document.querySelector('#safe-stop').addEventListener('click', () => { footerResult.textContent = 'ОСТАНОВЛЕНО БЕЗОПАСНО'; footerStep.textContent = 'Оборудование переведено в безопасное состояние'; footerProgress.style.width = '0%'; });
document.querySelector('#start-test').addEventListener('click', () => { footerResult.textContent = 'ВЫПОЛНЯЕТСЯ'; });

render('yalk');
