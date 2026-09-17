
(() => {
  const app = document.getElementById('app');
  const modalRoot = document.getElementById('modal-root');

  const registry = [
    { serial:'345', type:'УБСИ', date:'12.04.2026' },
    { serial:'346', type:'УБСИ', date:'12.04.2026' },
    { serial:'347', type:'УБСИ', date:'11.04.2026' },
    { serial:'348', type:'УБСИ', date:'10.04.2026' },
    { serial:'349', type:'УБСИ', date:'09.04.2026' },
    { serial:'350', type:'УБСИ', date:'09.04.2026' },
    { serial:'351', type:'УБСИ', date:'08.04.2026' },
  ];

  const operators = ['Толмачёв А.Е.','Иванов И.И.','Петров П.С.'];
  const stages = ['Климат Н.У.','Климат +50'];
  const scopes = ['Полная УБСИ','ЯЛК-96','ЯТП','ЯВП-8'];

  const route = [
    { id:'prep', label:'Подготовка', sub:[] },
    { id:'power', label:'Питание', sub:[] },
    { id:'yalk', label:'ЯЛК-96', sub:[
      ['stream','Инициализация потока'],
      ['cal','Калибровка 97 / 99'],
      ['initial','Исходное состояние'],
      ['analog','Аналоговые каналы'],
      ['discrete','Контактные сигналы'],
      ['overload','Перегрузка ±12 В'],
      ['ref','Эталон 6,2 В'],
      ['yalk-clean','Завершение ЯЛК'],
    ]},
    { id:'ytp', label:'ЯТП', sub:[
      ['ytp-stream','Инициализация'],
      ['ytp-cal','Калибровка'],
      ['ytp-check','30 каналов'],
      ['ytp-clean','Завершение ЯТП'],
    ]},
    { id:'yvp', label:'ЯВП-8', sub:[
      ['yvp-mode','Инициализация'],
      ['yvp-check','8 каналов'],
      ['yvp-clean','Завершение ЯВП'],
    ]},
    { id:'finish', label:'Завершение', sub:[] },
  ];

  const state = {
    screen:'home',
    operator:operators[0],
    productionStage:stages[0],
    scope:'Полная УБСИ',
    selectedRegistry:'345',
    queue:[],
    queueSelected:null,
    activeQueueIndex:0,
    prepChecked:false,
    prepBusy:false,
    prepOk:false,
    run:null,
    protoOpen:false,
    tu:{
      serial:'345',
      ready:false,
      run:null,
      operator:operators[0],
      reportMode:'norma',
    },
    admin:{
      selected:'345',
      filter:'',
      historyFilter:'Все',
    },
    session:{
      addOperatorOpen:false,
    },
  };

  let clockTimer = null;
  let simTimer = null;
  let lastFrame = performance.now();

  function escapeHtml(s){
    return String(s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#039;'}[c]));
  }
  function clamp(v,a,b){ return Math.max(a,Math.min(b,v)); }
  function fmt(v,d=3){ return Number(v).toFixed(d).replace('.',','); }
  function nowText(){
    const d = new Date();
    return d.toLocaleDateString('ru-RU') + '  ' + d.toLocaleTimeString('ru-RU',{hour:'2-digit',minute:'2-digit',second:'2-digit'});
  }
  function formatDuration(sec){
    if(sec == null || !isFinite(sec)) return '—';
    sec = Math.max(0,Math.round(sec));
    const h = Math.floor(sec/3600), m=Math.floor((sec%3600)/60), s=sec%60;
    if(h) return `${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`;
    return `${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`;
  }

  function activeRun(){
    if(state.screen==='tu-runtime') return state.tu.run;
    return state.run;
  }
  function ensureZoom(run){
    if(!run) return null;
    if(!run.zoom){
      run.zoom={
        analog:{scale:1,shift:0,full:false},
        discrete:{scale:1,shift:0,full:false},
        overload:{scale:1,shift:0,full:false},
        ytp:{scale:1,shift:0,full:false},
      };
    }
    return run.zoom;
  }
  function resetZoom(run,kind){
    const z=ensureZoom(run);
    if(!z||!z[kind]) return;
    z[kind]={scale:1,shift:0,full:false};
  }
  function resolvedRange(kind,baseMin,baseMax){
    const run=activeRun(), z=run ? ensureZoom(run)?.[kind] : null;
    if(!z) return [baseMin,baseMax];
    if(kind==='analog' && z.full) return [-0.08,6.32];
    const baseWidth=Math.max(1e-9,baseMax-baseMin);
    const width=baseWidth/clamp(z.scale,1,12);
    const baseCenter=(baseMin+baseMax)/2;
    const center=baseCenter + z.shift*baseWidth;
    return [center-width/2,center+width/2];
  }
  function currentStageLabel(stage){
    return ({
      power:'Питание',
      analog:'ЯЛК-96 / Аналоговые каналы',
      discrete:'ЯЛК-96 / Контактные сигналы',
      overload:'ЯЛК-96 / Перегрузка ±12 В',
      ytp:'ЯТП / 30 каналов',
      yvp:'ЯВП-8 / 8 каналов'
    })[stage] || stage;
  }
  function setScreen(screen){
    if(screen!=='runtime' && screen!=='tu-runtime') stopSimulation();
    state.screen=screen; render();
  }

  function render(){
    clearInterval(clockTimer);
    app.innerHTML = '';
    if(state.screen==='home') renderHome();
    else if(state.screen==='session') renderSession();
    else if(state.screen==='prep') renderPreparation();
    else if(state.screen==='runtime') renderRuntime();
    else if(state.screen==='finish') renderFinish();
    else if(state.screen==='tu-entry') renderTuEntry();
    else if(state.screen==='tu-ready') renderTuReady();
    else if(state.screen==='tu-runtime') renderTuRuntime();
    else if(state.screen==='tu-operator') renderTuOperator();
    else if(state.screen==='tu-report') renderTuReport();
    else if(state.screen==='admin') renderAdmin();
    renderProtoPanel();
    clockTimer=setInterval(()=>{
      document.querySelectorAll('[data-clock]').forEach(el=>el.textContent=nowText());
    },1000);
  }

  function renderHome(){
    app.innerHTML = `
      <div class="app-shell">
        <div class="home">
          <div class="home-card">
            <div class="brand-kicker">MilTech Station / КТМА</div>
            <h1>УБСИ</h1>
            <p>Операторский интерфейс испытаний</p>
            <div class="mode-grid">
              <button class="mode-btn" id="go-production">
                <strong>ПРОИЗВОДСТВО</strong>
                <span>Очередь изделий, подготовка и производственный прогон.</span>
              </button>
              <button class="mode-btn" id="go-tu">
                <strong>ПРИЁМО-СДАТОЧНАЯ ПРОВЕРКА ПО ТУ</strong>
                <span>Один маршрут ТУ: готовность стенда, автоматическая проверка, ФИО перед отчётом.</span>
              </button>
              <button class="mode-btn" id="go-admin">
                <strong>АДМИНИСТРИРОВАНИЕ</strong>
                <span>Регистрация УБСИ, состав, замены, переносы и история.</span>
              </button>
            </div>
            <div class="phase-note">
              Прототип v1.2: Production, ТУ и Администрирование кликабельны. PROTO-контролы существуют только для демонстрации состояний.
            </div>
          </div>
        </div>
      </div>`;
    document.getElementById('go-production').onclick=()=>setScreen('session');
    document.getElementById('go-tu').onclick=()=>setScreen('tu-entry');
    document.getElementById('go-admin').onclick=()=>setScreen('admin');
  }

  function topbar(title, crumb=''){
    return `
      <div class="topbar">
        <div class="title">${escapeHtml(title)}</div>
        <div class="crumb">${escapeHtml(crumb)}</div>
        <div class="spacer"></div>
        <div class="clock" data-clock>${nowText()}</div>
      </div>`;
  }

  function componentCountForSerial(serial){
    const c=adminComponents?.[serial];
    if(!c)return 4;
    return ['yalk','ytp','yvp','ypp'].filter(k=>c[k]&&c[k]!=='—').length;
  }

  function renderSession(){
    const selected=registry.find(x=>x.serial===state.selectedRegistry);
    app.innerHTML = `
      <div class="app-shell page session-page-v11">
        <div class="session-brandbar">
          <button class="btn btn-small btn-ghost" id="back-home">← Главная</button>
          <div class="session-brandmark">◎</div>
          <div class="session-brandcopy"><strong>MilTechStation / КТМА</strong><span>УБСИ · производственный контур</span></div>
          <div class="spacer"></div>
          <span class="session-proto-chip">ПРОТОТИП</span>
          <span class="session-mode-chip">ПРОИЗВОДСТВО</span>
        </div>
        <div class="session-v11">
          <div class="session-title-v11"><h1>Производственная сессия</h1><p>Сформируйте очередь зарегистрированных изделий. Один оператор, этап и объём относятся ко всей сессии.</p></div>
          <div class="session-grid-v11">
            <section class="session-panel-v11 registry-panel-v11">
              <div class="session-panel-head-v11"><b>Зарегистрированные УБСИ</b><small>${registry.length}</small></div>
              <div class="session-search-v11"><input id="registry-search" class="input" placeholder="Поиск по заводскому №" /></div>
              <div class="session-list-v11" id="registry-list"></div>
            </section>
            <section class="session-panel-v11 queue-panel-v11">
              <div class="session-panel-head-v11"><b>Очередь текущей сессии</b><small>${state.queue.length}</small></div>
              <div class="session-list-v11" id="queue-list"></div>
              <div class="queue-toolbar-v11">
                <button class="btn btn-small" id="queue-up">↑</button>
                <button class="btn btn-small" id="queue-down">↓</button>
                <button class="btn btn-small btn-ghost" id="queue-clear">Очистить</button>
              </div>
            </section>
            <section class="session-panel-v11 config-panel-v11">
              <div class="session-panel-head-v11"><b>Параметры сессии</b></div>
              <div class="session-config-v11">
                <div class="field"><label>Оператор</label><div class="session-operator-v11"><select class="select" id="operator-select">${operators.map(o=>`<option ${o===state.operator?'selected':''}>${escapeHtml(o)}</option>`).join('')}</select><button class="btn" id="add-operator">+</button></div></div>
                <div class="field"><label>Производственный этап</label><select class="select" id="stage-select">${stages.map(s=>`<option ${s===state.productionStage?'selected':''}>${escapeHtml(s)}</option>`).join('')}</select></div>
                <div class="field"><label>Объём проверки</label><div class="session-scopes-v11">${scopes.map(s=>`<button class="session-scope-card-v11 ${s===state.scope?'active':''}" data-scope="${escapeHtml(s)}"><b>${escapeHtml(s)}</b><span>${s==='Полная УБСИ'?'Питание · ЯЛК-96 · ЯТП · ЯВП-8':s==='ЯЛК-96'?'Отдельная ячейка':s==='ЯТП'?'30 каналов':'8 каналов'}</span></button>`).join('')}</div></div>
                <div class="field"><label>Выбрано в базе</label><div class="session-selected-v11">${selected?`УБСИ ${escapeHtml(selected.serial)}`:'—'}</div></div>
              </div>
            </section>
          </div>
          <div class="session-bottom-v11"><div id="queue-summary" class="session-summary-v11"></div><button class="btn btn-primary" id="start-prep" ${state.queue.length?'':'disabled'}>Перейти к подготовке</button></div>
        </div>
      </div>`;
    document.getElementById('back-home').onclick=()=>setScreen('home');
    document.getElementById('operator-select').onchange=e=>state.operator=e.target.value;
    document.getElementById('add-operator').onclick=showAddOperatorModal;
    document.getElementById('stage-select').onchange=e=>{state.productionStage=e.target.value;paintQueue();};
    document.querySelectorAll('[data-scope]').forEach(btn=>btn.onclick=()=>{state.scope=btn.dataset.scope;renderSession();});
    const search=document.getElementById('registry-search');search.oninput=()=>paintRegistry(search.value);
    document.getElementById('queue-up').onclick=()=>moveSelectedQueue(-1);
    document.getElementById('queue-down').onclick=()=>moveSelectedQueue(1);
    document.getElementById('queue-clear').onclick=()=>{state.queue=[];state.queueSelected=null;paintQueue();paintRegistry(search.value)};
    document.getElementById('start-prep').onclick=()=>{state.activeQueueIndex=0;state.prepChecked=false;state.prepBusy=false;state.prepOk=false;setScreen('prep');};
    paintRegistry('');paintQueue();
  }

  function paintRegistry(q){
    const root=document.getElementById('registry-list');if(!root)return;
    const needle=q.trim().toLowerCase();
    const items=registry.filter(x=>!needle||x.serial.toLowerCase().includes(needle));
    root.innerHTML=items.map(x=>{const count=componentCountForSerial(x.serial),full=count===4,inQueue=state.queue.includes(x.serial);return `
      <div class="registry-row-v11 ${state.selectedRegistry===x.serial?'selected':''}" data-serial="${x.serial}">
        <div><b>УБСИ ${escapeHtml(x.serial)}</b><span>${count}/4 ячейки · ${full?'готово к производственной проверке':'состав неполный'}</span></div>
        <button class="btn btn-small" data-add="${x.serial}" ${!full||inQueue?'disabled':''}>${full?'Добавить →':'Недоступно'}</button>
      </div>`}).join('');
    root.querySelectorAll('.registry-row-v11').forEach(row=>row.onclick=e=>{if(e.target.closest('[data-add]'))return;state.selectedRegistry=row.dataset.serial;paintRegistry(q);const sel=document.querySelector('.session-selected-v11');if(sel)sel.textContent='УБСИ '+row.dataset.serial;});
    root.querySelectorAll('[data-add]').forEach(b=>b.onclick=e=>{e.stopPropagation();if(!state.queue.includes(b.dataset.add)){state.queue.push(b.dataset.add);state.queueSelected=b.dataset.add;}paintQueue();paintRegistry(q);});
  }

  function paintQueue(){
    const root=document.getElementById('queue-list');if(!root)return;
    if(!state.queue.length){root.innerHTML=`<div class="queue-empty-v11">Добавьте изделия из регистратора.<br><span>Очередь — отдельный набор, а не весь список базы.</span></div>`;}
    else root.innerHTML=state.queue.map((serial,i)=>`<div class="queue-row-v11 ${state.queueSelected===serial?'selected':''}" data-qserial="${serial}"><div><b>${i+1}. УБСИ ${escapeHtml(serial)}</b><span>${i<state.activeQueueIndex?'ЗАВЕРШЕНО':'ОЖИДАЕТ'}</span></div><button class="btn btn-small btn-ghost" data-qremove="${serial}">×</button></div>`).join('');
    root.querySelectorAll('[data-qserial]').forEach(row=>row.onclick=e=>{if(e.target.closest('[data-qremove]'))return;state.queueSelected=row.dataset.qserial;paintQueue();});
    root.querySelectorAll('[data-qremove]').forEach(b=>b.onclick=e=>{e.stopPropagation();state.queue=state.queue.filter(x=>x!==b.dataset.qremove);if(state.queueSelected===b.dataset.qremove)state.queueSelected=state.queue[0]||null;paintQueue();paintRegistry(document.getElementById('registry-search')?.value||'');});
    const idx=state.queue.indexOf(state.queueSelected);document.getElementById('queue-up').disabled=idx<=0;document.getElementById('queue-down').disabled=idx<0||idx>=state.queue.length-1;document.getElementById('queue-clear').disabled=!state.queue.length;document.getElementById('start-prep').disabled=!state.queue.length;
    const s=document.getElementById('queue-summary');if(s)s.innerHTML=`Оператор: <strong>${escapeHtml(state.operator)}</strong> · этап: <strong>${escapeHtml(state.productionStage)}</strong> · объём: <strong>${escapeHtml(state.scope)}</strong> · в очереди: <strong>${state.queue.length}</strong>`;
  }
  function moveSelectedQueue(dir){const i=state.queue.indexOf(state.queueSelected);if(i<0)return;const j=i+dir;if(j<0||j>=state.queue.length)return;[state.queue[i],state.queue[j]]=[state.queue[j],state.queue[i]];paintQueue();}

  function showAddOperatorModal(){
    modalRoot.innerHTML=`<div class="modal-backdrop"><div class="modal">
      <h3>Добавить оператора</h3>
      <div class="field" style="margin-top:12px"><label>ФИО</label><input class="input" id="new-operator-name" placeholder="Толмачёв А.Е." /></div>
      <div class="form-error" id="new-operator-error"></div>
      <p class="modal-note">Формат: фамилия, пробел, две заглавные инициалы с точками.</p>
      <div class="modal-actions"><button class="btn" id="new-operator-cancel">Отмена</button><button class="btn btn-primary" id="new-operator-ok">Добавить</button></div>
    </div></div>`;
    document.getElementById('new-operator-cancel').onclick=()=>modalRoot.innerHTML='';
    document.getElementById('new-operator-ok').onclick=()=>{
      const raw=document.getElementById('new-operator-name').value.trim();
      const ok=/^[А-ЯЁ][а-яё-]+\s[А-ЯЁ]\.[А-ЯЁ]\.$/.test(raw);
      if(!ok){document.getElementById('new-operator-error').textContent='Ожидается формат: Толмачёв А.Е.';return;}
      if(!operators.includes(raw)) operators.push(raw);
      state.operator=raw;state.tu.operator=raw;
      modalRoot.innerHTML='';
      renderSession();
    };
  }

  function equipmentForScope(scope){
    const common=[
      {id:'power',name:'Источник питания АКИП',required:true},
      {id:'rokt',name:'ROKT / поток данных',required:true},
    ];
    if(scope==='ЯЛК-96' || scope==='Полная УБСИ') return [
      ...common,
      {id:'isd',name:'ИСД / коммутация ЯЛК',required:true},
      {id:'v7',name:'Вольтметр В7',required:true},
    ];
    if(scope==='ЯТП') return [
      ...common,
      {id:'switch',name:'Коммутация ЯТП',required:true},
      {id:'r4831',name:'Магазин Р4831',required:false},
    ];
    if(scope==='ЯВП-8') return [
      ...common,
      {id:'yvp',name:'Тракт ЯВП-8',required:true},
      {id:'gen',name:'Генератор сигнала',required:true},
    ];
    return common;
  }

  function renderPreparation(){
    const serial=state.queue[state.activeQueueIndex] || '—';
    const eq=equipmentForScope(state.scope);
    const status = id=>{
      if(!state.prepChecked) return 'unchecked';
      if(state.prepBusy) return 'checking';
      return state.prepOk ? 'ready' : (id==='v7'?'error':'ready');
    };
    const label = (st,required)=>{
      if(!required) return 'НЕ ТРЕБУЕТСЯ';
      return ({unchecked:'НЕ ПРОВЕРЕНО',checking:'ПРОВЕРКА',ready:'ГОТОВО',error:'ОШИБКА'})[st];
    };
    app.innerHTML=`
      <div class="app-shell page">
        ${topbar('ПРОИЗВОДСТВО',`SN ${serial} · ${state.scope} · Подготовка`)}
        <div class="prep">
          <div class="prep-head">
            <h2>Подготовка к проверке</h2>
            <p>Проверяется только оборудование, требуемое выбранным сценарием.</p>
          </div>
          <div class="equipment-table">
            ${eq.map(item=>{
              const st=item.required?status(item.id):'na';
              return `<div class="eq-row">
                <div class="eq-name">${escapeHtml(item.name)}</div>
                <div class="eq-status"><span class="dot ${st==='unchecked'?'':st}"></span>${label(st,item.required)}</div>
              </div>`;
            }).join('')}
          </div>
          <div class="prep-foot">
            <button class="btn btn-ghost" id="prep-back">← В сессию</button>
            <div class="prep-message">${state.prepOk?'Оборудование готово. Можно запускать испытание.':state.prepBusy?'Выполняется проверка оборудования…':'Стенд ещё не проверен.'}</div>
            <div class="spacer"></div>
            <button class="btn" id="check-eq" ${state.prepBusy?'disabled':''}>${state.prepChecked?'Повторить проверку':'Проверить оборудование'}</button>
            <button class="btn btn-primary" id="start-run" ${state.prepOk?'':'disabled'}>Начать испытание</button>
          </div>
        </div>
      </div>`;
    document.getElementById('prep-back').onclick=()=>setScreen('session');
    document.getElementById('check-eq').onclick=()=>{
      state.prepChecked=true;state.prepBusy=true;state.prepOk=false;renderPreparation();
      setTimeout(()=>{state.prepBusy=false;state.prepOk=true;renderPreparation();},900);
    };
    document.getElementById('start-run').onclick=()=>startRun();
  }

  function makeRun(){
    const serial=state.queue[state.activeQueueIndex] || state.queue[0] || '345';
    const run = {
      serial, startedAt:Date.now(), displayElapsed:0, stage:'power', sub:'power',
      stopped:false, standError:false, productFail:false, operatorComment:'', historyCommitted:false,
      plan:[
        {id:'power',duration:26},
        {id:'analog',duration:45},
        {id:'discrete',duration:36},
        {id:'overload',duration:42},
        {id:'ytp',duration:34},
        {id:'yvp',duration:24},
      ],
      stageElapsed:0,
      power:{
        index:0,
        stepElapsed:0,
        steps:[
          {v:24,hold:.5,label:'settle 0,5 с',scenarioSeconds:.5},
          {v:27,hold:.5,label:'settle 0,5 с',scenarioSeconds:.5},
          {v:35,hold:.5,label:'settle 0,5 с',scenarioSeconds:.5},
          {v:19,hold:20,label:'5 мин → 20 с в прототипе',scenarioSeconds:300,prototypeCompressed:true},
          {v:27,hold:.5,label:'возврат 27 В',scenarioSeconds:.5,recovery:true},
          {v:37,hold:60,label:'1 мин',scenarioSeconds:60},
          {v:27,hold:.5,label:'возврат 27 В',scenarioSeconds:.5,recovery:true}
        ],
        yalkFrame:makeYalkFrame(3.08),
        yalkFresh:true,
        yalkObservationLabel:'пассивное наблюдение · не критерий питания',
      },
      yalk:{
        analogPoints:[0,3.1,6.2], analogPoint:0, analogChannel:0, analogFrame:makeYalkFrame(0),
        analogMin:null, analogMax:null, analogSnapshots:{}, viewingPoint:null,
        discretePoints:[0,0.9,2.5], discretePoint:0, discreteChannel:0, discreteFrame:makeDiscreteFrame(0),
        overloadChannel:0, overloadPolarity:12, overloadExposure:1,
        overloadBaseline:makeOverloadBaseline(), overloadCurrent:null,
        overloadDelta:Array(88).fill(0), overloadDeltaSnapshot:Array(88).fill(0),
        overloadPhase:'baseline',
        overloadPhaseElapsed:0,
        overloadLastSnapshot:null,
        overloadMaxDelta:0,
      },
      ytp:{
        pointIndex:0,channel:0,points:[0,120,240],frame:makeYtpFrame(0),min:null,max:null,waiting:false,
        phase:'settle',phaseElapsed:0,modalPoint:null
      },
      yvp:{
        channel:0,frame:makeYvpFrame(),gainIndex:0,frequencyIndex:0,pointElapsed:0,
        gains:[0.25,0.5,1,2,4,8,32],
        frequencies:[0.15,20,250,500,1800,2000,4000],
        currentMappingConfirmed:false
      },
      current:{value:.28,history:Array.from({length:90},(_,i)=>({x:i,y:.26+Math.sin(i/8)*.012}))},
      etaMode:'planned',
      finishedBlocks:{power:null,yalk:null,ytp:null,yvp:null},
      failChannel:null,
      currentFresh:true,
      viewStage:null,
      routeExpanded:null,
      zoom:{
        analog:{scale:1,shift:0,full:false},
        discrete:{scale:1,shift:0,full:false},
        overload:{scale:1,shift:0,full:false},
        ytp:{scale:1,shift:0,full:false},
      },
    };
    run.yalk.overloadCurrent=makeOverloadCurrent(run.yalk.overloadBaseline,0,12);
    run.yalk.analogMin=run.yalk.analogFrame.slice();
    run.yalk.analogMax=run.yalk.analogFrame.slice();
    run.ytp.min=run.ytp.frame.slice();
    run.ytp.max=run.ytp.frame.slice();
    return run;
  }
  function startRun(){
    state.run=makeRun();state.screen='runtime';render();startSimulation();
  }
  function stopSimulation(){if(simTimer){clearInterval(simTimer);simTimer=null}}
  function startSimulation(){
    stopSimulation(); lastFrame=performance.now();
    simTimer=setInterval(()=>{
      if(state.screen!=='runtime' || !state.run) return;
      const now=performance.now();
      const dt=Math.min(.25,(now-lastFrame)/1000);lastFrame=now;
      tickRun(state.run,dt);
      paintRuntimeLive();
    },120);
  }

  function makeYalkFrame(center){
    return Array.from({length:80},(_,i)=>center + Math.sin(i*.41)*.012 + (Math.random()-.5)*.010);
  }
  function makeDiscreteFrame(point){
    return Array.from({length:80},(_,i)=>{
      const analog=point + Math.sin(i*.23)*.035 + (Math.random()-.5)*.025;
      return {analog,min:analog,max:analog,logic:point>=2.5 ? 1 : 0,state:'pending'};
    });
  }
  function makeOverloadBaseline(){
    return Array.from({length:88},(_,i)=>3.08 + Math.sin(i*.25)*.025 + (Math.random()-.5)*.008);
  }
  function makeOverloadCurrent(base,stim,polarity){
    return base.map((v,i)=>v + (i===stim?0:(Math.random()-.5)*.015) + (Math.abs(i-stim)===2?polarity*.0004:0));
  }
  function makeYtpFrame(point){
    return Array.from({length:30},(_,i)=>point + Math.sin(i*.44)*.45 + (Math.random()-.5)*.4);
  }
  function makeYvpFrame(){
    return Array.from({length:8},(_,i)=>{
      const ref=1+.05*Math.sin(i*.7);
      return {actual:ref+.015,reference:ref,measured:ref+(Math.random()-.5)*.05,isd:ref-.02};
    });
  }

  function tickRun(r,dt){
    if(r.stopped || r.standError) return;
    r.displayElapsed += dt;
    r.stageElapsed += dt;

    // Общий ток УБСИ остаётся live даже во время обязательного ручного действия Р4831.
    if(r.currentFresh){
      const target=.28 + Math.sin(r.displayElapsed*.65)*.018 + (Math.random()-.5)*.009;
      r.current.value = clamp(target,.18,.46);
      r.current.history.push({x:r.displayElapsed,y:r.current.value});
      if(r.current.history.length>140)r.current.history.shift();
    }

    if(r.ytp.waiting) return;
    if(r.stage==='power') tickPower(r,dt);
    else if(r.stage==='analog') tickAnalog(r,dt);
    else if(r.stage==='discrete') tickDiscrete(r,dt);
    else if(r.stage==='overload') tickOverload(r,dt);
    else if(r.stage==='ytp') tickYtp(r,dt);
    else if(r.stage==='yvp') tickYvp(r,dt);
  }

  function tickPower(r,dt){
    const p=r.power;
    const step=p.steps[Math.min(p.index,p.steps.length-1)];
    if(!step){r.finishedBlocks.power='НОРМА';setStage(r,'analog');return}

    p.stepElapsed += dt;

    // Passive YALK observation. Readiness has already started the YALK stream.
    // In the real backend supply_range currently checks only alive; a full snapshot
    // still needs to be published by the backend. At 19 V the adapter is known to
    // be able to lose exchange, so the prototype explicitly shows stale data.
    const fresh = step.v !== 19;
    p.yalkFresh=fresh;
    if(fresh){
      const supplyDrift=(step.v-27)*0.0022;
      p.yalkFrame=p.yalkFrame.map((v,i)=>{
        const target=3.08+supplyDrift+Math.sin(i*.31)*.010;
        return v+(target-v)*.18+(Math.random()-.5)*.004;
      });
    }

    if(p.stepElapsed >= step.hold){
      p.index++;p.stepElapsed=0;
      if(p.index>=p.steps.length){
        r.finishedBlocks.power='НОРМА';
        setStage(r,'analog');
      }
    }
  }
  function tickAnalog(r,dt){
    const y=r.yalk;
    const nominal=y.analogPoints[y.analogPoint];
    const ref=nominal + (nominal===0?.004:(Math.sin(r.displayElapsed*.3)*.006));
    y.analogActual=ref;
    // all channels move live
    y.analogFrame = y.analogFrame.map((v,i)=>{
      const base=nominal + Math.sin(i*.37)*.012;
      const next=v+(base-v)*.24+(Math.random()-.5)*.006;
      return next;
    });
    if(r.failChannel!=null) y.analogFrame[r.failChannel]+= nominal===0 ? .12 : .16;
    if(!y.analogMin || y.analogMin.length!==80){y.analogMin=y.analogFrame.slice();y.analogMax=y.analogFrame.slice();}
    y.analogFrame.forEach((v,i)=>{y.analogMin[i]=Math.min(y.analogMin[i],v);y.analogMax[i]=Math.max(y.analogMax[i],v);});
    if(r.stageElapsed>=1.15){
      y.analogChannel++;
      r.stageElapsed=0;
      if(y.analogChannel>=80){
        y.analogSnapshots[nominal]={
          nominal,actual:ref,channels:y.analogFrame.slice(),mins:y.analogMin.slice(),maxs:y.analogMax.slice(),completedAt:r.displayElapsed
        };
        y.analogChannel=0;y.analogPoint++;
        if(y.analogPoint>=y.analogPoints.length){
          r.finishedBlocks.yalk='ВЫПОЛНЯЕТСЯ';setStage(r,'discrete');
        } else {
          y.analogFrame=makeYalkFrame(y.analogPoints[y.analogPoint]);
          y.analogMin=y.analogFrame.slice();y.analogMax=y.analogFrame.slice();
          resetZoom(r,'analog');
        }
      }
    }
  }
  function tickDiscrete(r,dt){
    const y=r.yalk;
    const point=y.discretePoints[y.discretePoint];
    y.discreteActual=point + (point===0?.004:Math.sin(r.displayElapsed*.25)*.007);
    y.discreteFrame=y.discreteFrame.map((c,i)=>{
      const analog=c.analog+(point-c.analog)*.22+(Math.random()-.5)*.013;
      return {...c,analog,min:Math.min(c.min??analog,analog),max:Math.max(c.max??analog,analog),logic:point>=2.5?1:0};
    });
    if(r.stageElapsed>=1.15){
      const idx=y.discreteChannel;
      const c=y.discreteFrame[idx];
      c.state='done';
      if(r.failChannel===idx){c.state='failed';c.logic=c.logic?0:1}
      y.discreteChannel++;r.stageElapsed=0;
      if(y.discreteChannel>=80){
        y.discreteChannel=0;y.discretePoint++;
        if(y.discretePoint>=y.discretePoints.length){
          setStage(r,'overload');
        } else {
          y.discreteFrame=makeDiscreteFrame(y.discretePoints[y.discretePoint]);
          resetZoom(r,'discrete');
        }
      }
    }
  }
  function tickOverload(r,dt){
    const y=r.yalk;
    y.overloadPhaseElapsed += dt;

    // Canonical current implementation:
    // baseline once -> safe reset/DAC off -> +12 V channels 1..88 ->
    // then -12 V channels 1..88; every impact is followed by safe reset.
    if(y.overloadPhase==='baseline'){
      // Reference staircase is present while baseline is captured.
      y.overloadCurrent=y.overloadBaseline.map((v,i)=>v+(Math.random()-.5)*.004);
      if(y.overloadPhaseElapsed>=1.0){
        y.overloadBaseline=y.overloadCurrent.slice();
        y.overloadLastSnapshot=y.overloadCurrent.slice();
        y.overloadPhase='safe';
        y.overloadPhaseElapsed=0;
      }
      return;
    }

    if(y.overloadPhase==='safe'){
      // RESET + source off + DAC off + confirmed cleanup settle.
      if(y.overloadPhaseElapsed>=0.3){
        y.overloadPhase='impact';
        y.overloadPhaseElapsed=0;
      }
      return;
    }

    if(y.overloadPhase==='impact'){
      const t=clamp(y.overloadPhaseElapsed/10,0,1);
      const sign=y.overloadPolarity>0?1:-1;
      y.overloadDelta=y.overloadBaseline.map((_,i)=>{
        if(i===y.overloadChannel) return 0; // stressed channel is excluded from criterion
        // Prototype models the backend quantity directly: integer delta_code.
        // A dense -2..+2 field is intentional so the 88-channel plane stays readable.
        const wave=Math.sin((i+1)*.71 + (y.overloadChannel+1)*.19 + t*1.9)
                  + .55*Math.cos((i+3)*.29 - (y.overloadChannel+1)*.11);
        let d=Math.round(clamp(wave*1.05,-2,2));
        if(((i+y.overloadChannel)%23)===7) d=sign*2;
        if(r.failChannel!=null && i===r.failChannel) d=sign*3;
        return d;
      });
      y.overloadCurrent=y.overloadBaseline.map((v,i)=>v+(y.overloadDelta[i]||0)*.02);
      y.overloadHold=y.overloadPhaseElapsed;
      y.overloadMaxDelta=Math.max(...y.overloadDelta.map((d,i)=>i===y.overloadChannel?0:Math.abs(d)));

      if(y.overloadPhaseElapsed>=10){
        y.overloadLastSnapshot=y.overloadCurrent.slice();
        y.overloadDeltaSnapshot=y.overloadDelta.slice();
        y.overloadPhase='snapshot';
        y.overloadPhaseElapsed=0;
      }
      return;
    }

    if(y.overloadPhase==='snapshot'){
      // Keep the just-captured current snapshot visible briefly.
      if(y.overloadPhaseElapsed>=0.15){
        y.overloadPhase='safe';
        y.overloadPhaseElapsed=0;

        // Advance AFTER snapshot. All +12 V channels first, then all -12 V.
        y.overloadExposure++;
        y.overloadChannel++;
        if(y.overloadChannel>=88){
          if(y.overloadPolarity===12){
            y.overloadPolarity=-12;
            y.overloadChannel=0;
          } else {
            r.finishedBlocks.yalk=r.productFail?'НЕ НОРМА':'НОРМА';
            setStage(r,'ytp');
          }
        }
      }
    }
  }
  function tickYtp(r,dt){
    const y=r.ytp;
    const point=y.points[Math.min(y.pointIndex,y.points.length-1)];
    if(y.waiting)return;

    y.phaseElapsed+=dt;
    y.frame=y.frame.map((v,i)=>v+(point-v)*.15+(Math.random()-.5)*.12);
    if(!y.min||y.min.length!==30){y.min=y.frame.slice();y.max=y.frame.slice();}
    y.frame.forEach((v,i)=>{y.min[i]=Math.min(y.min[i],v);y.max[i]=Math.max(y.max[i],v);});

    if(y.phase==='settle'){
      if(y.phaseElapsed>=0.8){
        y.phase='read';
        y.phaseElapsed=0;
      }
      return;
    }

    if(y.phase==='read' && y.phaseElapsed>=0.12){
      y.channel++;
      y.phaseElapsed=0;
      if(y.channel>=30){
        y.channel=0;
        y.pointIndex++;
        if(y.pointIndex>=y.points.length){
          r.finishedBlocks.ytp=r.productFail?'НЕ НОРМА':'НОРМА';
          setStage(r,'yvp');
        } else {
          y.waiting=true;
          y.phase='settle';
          y.phaseElapsed=0;
          y.frame=makeYtpFrame(y.points[y.pointIndex]);y.min=y.frame.slice();y.max=y.frame.slice();
          r.etaMode='waiting_operator';
          showR4831Modal(y.points[y.pointIndex]);
        }
      }
    }
  }
  function tickYvp(r,dt){
    const y=r.yvp;
    y.pointElapsed+=dt;
    y.frame=makeYvpFrame();

    if(y.pointElapsed<0.2)return;
    y.pointElapsed=0;

    y.frequencyIndex++;
    if(y.frequencyIndex>=y.frequencies.length){
      y.frequencyIndex=0;
      y.gainIndex++;
      if(y.gainIndex>=y.gains.length){
        y.gainIndex=0;
        y.channel++;
        if(y.channel>=8){
          r.finishedBlocks.yvp=r.productFail?'НЕ НОРМА':'НОРМА';
          finishRun();
        }
      }
    }
  }
  function setStage(r,stage){
    r.stage=stage;r.sub=stage;r.stageElapsed=0;r.viewStage=null;
    if(stage==='analog'){r.yalk.analogPoint=0;r.yalk.analogChannel=0}
    if(stage==='discrete'){r.yalk.discretePoint=0;r.yalk.discreteChannel=0}
    if(stage==='overload'){
      r.yalk.overloadChannel=0;r.yalk.overloadPolarity=12;r.yalk.overloadExposure=1;
      r.yalk.overloadPhase='baseline';r.yalk.overloadPhaseElapsed=0;r.yalk.overloadMaxDelta=0;
      r.yalk.overloadBaseline=makeOverloadBaseline();r.yalk.overloadCurrent=r.yalk.overloadBaseline.slice();
      r.yalk.overloadDelta=Array(88).fill(0);r.yalk.overloadDeltaSnapshot=Array(88).fill(0);
    }
    if(stage==='ytp'){
      r.ytp.pointIndex=0;r.ytp.channel=0;r.ytp.phase='settle';r.ytp.phaseElapsed=0;r.ytp.waiting=true;r.ytp.frame=makeYtpFrame(0);
      r.ytp.min=r.ytp.frame.slice();r.ytp.max=r.ytp.frame.slice();r.ytp.modalPoint=null;
      r.etaMode='waiting_operator';
      setTimeout(()=>{const ar=activeRun();if(ar&&ar.stage==='ytp'&&ar.ytp.waiting)showR4831Modal(0)},0);
    }
    if(stage==='yvp'){r.yvp.channel=0;r.yvp.gainIndex=0;r.yvp.frequencyIndex=0;r.yvp.pointElapsed=0}
    if(['analog','discrete','overload','ytp'].includes(stage)) resetZoom(r,stage);
  }
  function finishRun(){
    if(state.screen==='tu-runtime') return;
    stopSimulation();
    const r=state.run;
    r.finishedAt=Date.now();
    if(!r.finishedBlocks.yalk)r.finishedBlocks.yalk=r.productFail?'НЕ НОРМА':'НОРМА';
    if(!r.finishedBlocks.ytp)r.finishedBlocks.ytp=r.productFail?'НЕ НОРМА':'НОРМА';
    if(!r.finishedBlocks.yvp)r.finishedBlocks.yvp=r.productFail?'НЕ НОРМА':'НОРМА';
    state.screen='finish';render();
  }

  function stageRouteState(r,block){
    const order=['prep','power','yalk','ytp','yvp','finish'];
    const activeBlock = r.stage==='power'?'power':['analog','discrete','overload'].includes(r.stage)?'yalk':r.stage==='ytp'?'ytp':r.stage==='yvp'?'yvp':'finish';
    return {active:activeBlock===block,done:order.indexOf(block)<order.indexOf(activeBlock),activeBlock};
  }

  function renderRoute(r){
    const stage=r.stage;
    let title='',items=[];
    if(stage==='power'){
      title='ПИТАНИЕ';
      const vals=['24 В','27 В','35 В','19 В / 5 мин','27 В · возврат','37 В / 1 мин','27 В · возврат'];
      const idx=Math.min(r.power.index,vals.length-1);items=vals.map((x,i)=>({label:x,state:i<idx?'done':i===idx?'active':'future'}));
    } else if(['analog','discrete','overload'].includes(stage)){
      title='ЯЛК-96';
      const order=[['Инициализация потока','done'],['Калибровка 97 / 99','done'],['Исходное состояние','done'],['Аналоговые каналы',stage==='analog'?'active':stage!=='analog'?'done':'future'],['Контактные сигналы',stage==='discrete'?'active':stage==='overload'?'done':'future'],['Перегрузка ±12 В',stage==='overload'?'active':'future'],['Эталон 6,2 В','future'],['Безопасное завершение','future']];items=order.map(([label,state])=>({label,state}));
    } else if(stage==='ytp'){
      title='ЯТП';items=[{label:'Инициализация',state:'done'},{label:'Калибровка',state:'done'},{label:'30 каналов',state:'active'},{label:'Безопасное завершение',state:'future'}];
    } else if(stage==='yvp'){
      title='ЯВП-8';items=[{label:'Инициализация',state:'done'},{label:'8 каналов',state:'active'},{label:'Безопасное завершение',state:'future'}];
    } else {title='ЗАВЕРШЕНИЕ';items=[]}
    return `<div class="context-route-title">${title}</div><div class="context-route-list">${items.map(x=>`<div class="substep ${x.state==='active'?'active':x.state==='done'?'done':''}"><span>${x.state==='done'?'✓':x.state==='active'?'●':'○'}</span>${x.label}</div>`).join('')}</div>`;
  }

  function renderRuntime(){
    const r=state.run;if(!r)return setScreen('session');
    app.innerHTML=`
      <div class="app-shell runtime">
        <div class="run-header">
          <button class="btn btn-small btn-ghost" id="to-session">← Сессия</button>
          <div class="product">УБСИ SN ${r.serial}</div>
          <div class="branch">Производство · ${state.scope}</div>
          <div class="spacer"></div>
          <div class="operator">${state.operator}</div>
          <button class="btn btn-small btn-danger" id="stop-run">Остановить</button>
        </div>
        <div class="stage-strip" id="stage-strip"></div>
        <div class="work-grid">
          <aside class="route" id="route">${renderRoute(r)}</aside>
          <main class="workspace" id="workspace"></main>
        </div>
        <div class="telemetry" id="telemetry"></div>
      </div>`;
    document.getElementById('to-session').onclick=()=>{
      confirmModal('Вернуться в сессию?','Активный прогон будет остановлен и переведён в безопасное состояние.','Вернуться',()=>{
        stopSimulation();state.run.stopped=true;state.screen='session';render();
      });
    };
    document.getElementById('stop-run').onclick=()=>{
      confirmModal('Остановить испытание?','Стенд будет приведён в безопасное состояние. Полученные результаты останутся в истории.','Остановить',()=>{
        stopSimulation();state.run.stopped=true;state.screen='finish';render();
      },true);
    };
    paintRuntimeLive(true);
    startSimulation();
  }

  function paintRuntimeLive(force=false){
    const r=state.run;if(!r||state.screen!=='runtime')return;
    const strip=document.getElementById('stage-strip');
    const workspace=document.getElementById('workspace');
    const telemetry=document.getElementById('telemetry');
    const routeEl=document.getElementById('route');
    if(!strip||!workspace||!telemetry)return;

    const viewStage=r.viewStage||r.stage;
    strip.innerHTML=stageMetricsHtml(r,viewStage);
    routeEl.innerHTML=renderRoute(r);
    const reviewBanner=r.viewStage?`<div class="runtime-review-banner">ПРОСМОТР: ${currentStageLabel(r.viewStage)} · сейчас выполняется: ${currentStageLabel(r.stage)} <button class="btn btn-small" id="return-live">Вернуться к текущему</button></div>`:'';
    workspace.innerHTML=reviewBanner+stageViewHtml(r,viewStage);
    telemetry.innerHTML=telemetryHtml(r);

    wireStageInteractions(r,viewStage);
    if(r.viewStage){
      const back=document.getElementById('return-live');
      if(back)back.onclick=()=>{r.viewStage=null;r.yalk.viewingPoint=null;paintRuntimeLive()};
    }
    document.querySelectorAll('[data-route-view]').forEach(el=>el.onclick=()=>{
      const st=el.dataset.routeView;
      if(st && st!==r.stage){r.viewStage=st;if(st==='analog'&&r.yalk.analogSnapshots[6.2])r.yalk.viewingPoint=6.2;paintRuntimeLive();}
    });
    document.querySelectorAll('[data-route-expand]').forEach(el=>el.onclick=()=>{
      r.routeExpanded=r.routeExpanded===el.dataset.routeExpand?null:el.dataset.routeExpand;paintRuntimeLive();
    });
  }

  function stageMetricsHtml(r,stage){
    let m=[];
    if(stage==='power'){
      const p=r.power, step=p.steps[Math.min(p.index,p.steps.length-1)] || p.steps[p.steps.length-1];
      const actual=step.v + Math.sin(r.displayElapsed*.4)*.08;
      const holdText=step.hold>1?`${formatDuration(p.stepElapsed)} / ${formatDuration(step.hold)}`:'—';
      m=[
        ['Уставка',`${fmt(step.v,1)} В`],['Фактически',`${fmt(actual,2)} В`],
        ['Шаг',`${Math.min(p.index+1,7)} / 7`],['Выдержка',holdText],
        ['Общий ток',`${fmt(r.current.value,3)} А`],['ЯЛК поток',p.yalkFresh?'СВЕЖИЙ':'НЕТ ДАННЫХ',p.yalkFresh?'ok':'wait']
      ];
    } else if(stage==='analog'){
      const y=r.yalk; const nominal=y.viewingPoint!=null?y.viewingPoint:y.analogPoints[y.analogPoint] ?? 6.2;
      const snap=y.viewingPoint!=null?y.analogSnapshots[y.viewingPoint]:null;
      const arr=snap?snap.channels:y.analogFrame;
      const ch=clamp(y.analogChannel,0,79);
      const actual=snap?snap.actual:(y.analogActual??nominal);
      const val=arr[ch]??nominal;
      const dev=val-actual;
      m=[
        ['Номинальное воздействие',`${fmt(nominal,3)} В`],
        ['Фактическое воздействие',`${fmt(actual,3)} В`],
        ['Воздействуемый канал',`${ch+1} / 80`],
        ['ЯЛК',`${fmt(val,3)} В`],
        ['Отклонение',`${dev>=0?'+':''}${fmt(dev,3)} В`],
        ['Результат',Math.abs(dev)<=.08?'НОРМА':'НЕ НОРМА',Math.abs(dev)<=.08?'ok':'bad']
      ];
    } else if(stage==='discrete'){
      const y=r.yalk,point=y.discretePoints[y.discretePoint]??2.5,ch=clamp(y.discreteChannel,0,79),cell=y.discreteFrame[ch];
      m=[
        ['Номинальное воздействие',`${fmt(point,3)} В`],
        ['Фактическое воздействие',`${fmt(y.discreteActual??point,3)} В`],
        ['Воздействуемый канал',`${ch+1} / 80`],
        ['Ожидается',point>=2.5?'1':'0'],
        ['Текущее состояние',String(cell?.logic??0)],
        ['Результат',cell?.state==='failed'?'НЕ НОРМА':'НОРМА',cell?.state==='failed'?'bad':'ok']
      ];
    } else if(stage==='overload'){
      const y=r.yalk;
      const phaseLabel={
        baseline:'Снятие baseline',
        safe:'Сброс · ЦАП OFF',
        impact:`${y.overloadPolarity>0?'+':''}${y.overloadPolarity} В · выдержка`,
        snapshot:'Снимок после воздействия'
      }[y.overloadPhase]||y.overloadPhase;
      m=[
        ['Полярность',y.overloadPhase==='baseline'?'—':`${y.overloadPolarity>0?'+':''}${y.overloadPolarity} В`],
        ['Канал воздействия',y.overloadPhase==='baseline'?'—':`${y.overloadChannel+1} / 88`],
        ['Воздействие',y.overloadPhase==='baseline'?'baseline':`${Math.min(y.overloadExposure,176)} / 176`],
        ['Фаза',phaseLabel],
        ['max |Δcode|',String(y.overloadMaxDelta||0)],
        ['Результат',(y.overloadMaxDelta||0)<=2?'НОРМА':'НЕ НОРМА',(y.overloadMaxDelta||0)<=2?'ok':'bad']
      ];
    } else if(stage==='ytp'){
      const y=r.ytp,p=y.points[Math.min(y.pointIndex,2)],ch=clamp(y.channel,0,29),v=y.frame[ch]??p;
      m=[
        ['Точка Р4831',`${p} Ом`],['Текущий канал',`${ch+1} / 30`],
        ['Измерено',`${fmt(v,2)} Ом`],['Ожидается',`${p} Ом`],
        ['Отклонение',`${v-p>=0?'+':''}${fmt(v-p,2)} Ом`],
        ['Результат',Math.abs(v-p)<=2?'НОРМА':'НЕ НОРМА',Math.abs(v-p)<=2?'ok':'bad']
      ];
    } else if(stage==='yvp'){
      const y=r.yvp,ch=clamp(y.channel,0,7),v=y.frame[ch]||y.frame[7];
      const gain=y.gains[Math.min(y.gainIndex,y.gains.length-1)];
      const freq=y.frequencies[Math.min(y.frequencyIndex,y.frequencies.length-1)];
      const pointIndex=ch*y.gains.length*y.frequencies.length+y.gainIndex*y.frequencies.length+y.frequencyIndex+1;
      const total=8*y.gains.length*y.frequencies.length;
      const delta=v.measured-v.actual;
      m=[
        ['Канал',`${ch+1} / 8`],
        ['Подано',`${fmt(v.actual,3)} В`],
        ['Измерено',`${fmt(v.measured,3)} В`],
        ['Отклонение',`${delta>=0?'+':''}${fmt(delta,3)} В`],
        ['Режим',`K ${gain} · ${freq} Гц`],
        ['Ход',`${pointIndex} / ${total}`]
      ];
    }
    return m.map(([label,value,cls=''])=>`<div class="metric"><div class="label">${label}</div><div class="value ${cls}">${value}</div></div>`).join('');
  }

  function stageViewHtml(r,stage){
    if(stage==='power')return powerView(r);
    if(stage==='analog')return analogView(r);
    if(stage==='discrete')return discreteView(r);
    if(stage==='overload')return overloadView(r);
    if(stage==='ytp')return ytpView(r);
    if(stage==='yvp')return yvpView(r);
    return '';
  }

  function powerView(r){
    const p=r.power;
    const step=p.steps[Math.min(p.index,p.steps.length-1)] || p.steps[p.steps.length-1];
    const yalkMin=Math.min(...p.yalkFrame)-.035, yalkMax=Math.max(...p.yalkFrame)+.035;
    return `<section class="stage-view">
      <div class="stage-title-row"><h2>Питание УБСИ</h2><div class="hint">последовательность напряжений и выдержек</div><div class="spacer"></div></div>
      <div class="power-steps">
        ${p.steps.map((s,i)=>`<div class="power-step ${i<p.index?'done':''} ${i===p.index?'active':''}">
          <div class="v">${s.v} В</div><div class="hold">${s.label||'переход'}</div>
        </div>`).join('')}
      </div>
      <div class="power-dual">
        <div class="power-plot">${powerSvg(r)}</div>
        <div class="chart-card passive-yalk ${p.yalkFresh?'':'stale'}">
          <div class="chart-head">
            <strong>Пассивное наблюдение ЯЛК · 80 каналов</strong>
            <span class="meta">${p.yalkFresh?'поток свежий · не критерий питания':'НЕТ СВЕЖИХ ДАННЫХ · при 19 В поток может пропасть'}</span>
            <div class="spacer"></div>
          </div>
          ${barPlotOnly(p.yalkFrame,{min:yalkMin,max:yalkMax,reference:3.08,tolerance:null,stimulated:-1,pinned:null,kind:'power-yalk'})}
        </div>
      </div>
    </section>`;
  }
  function powerSvg(r){
    const steps=r.power.steps,w=1000,h=340,left=40,right=20,top=20,bottom=34;
    const xs=steps.map((_,i)=>left+i*(w-left-right)/(steps.length-1));
    const mapY=v=>top+(40-v)/(24)*(h-top-bottom);
    let setPath='',actPath='';
    steps.forEach((s,i)=>{
      const x=xs[i],y=mapY(s.v),ya=mapY(s.v+Math.sin((r.displayElapsed+i)*.35)*.08);
      if(i===0){setPath=`M ${x} ${y}`;actPath=`M ${x} ${ya}`;}
      else{
        const xp=xs[i-1],yp=mapY(steps[i-1].v),yap=mapY(steps[i-1].v+Math.sin((r.displayElapsed+i-1)*.35)*.08);
        setPath+=` L ${x} ${yp} L ${x} ${y}`;
        actPath+=` L ${x} ${yap} L ${x} ${ya}`;
      }
    });
    const grid=[19,24,27,35,37].map(v=>`<line x1="${left}" y1="${mapY(v)}" x2="${w-right}" y2="${mapY(v)}" stroke="rgba(80,113,134,.2)"/><text x="${left-8}" y="${mapY(v)+3}" text-anchor="end" fill="#61788a" font-size="10">${v}</text>`).join('');
    return `<svg class="power-svg" viewBox="0 0 ${w} ${h}" preserveAspectRatio="none">
      ${grid}
      <path d="${setPath}" fill="none" stroke="#58a5ff" stroke-width="3"/>
      <path d="${actPath}" fill="none" stroke="#61d5e8" stroke-width="2"/>
      <text x="${left}" y="${h-9}" fill="#8ea6b7" font-size="11">Заданное напряжение</text>
      <text x="${left+170}" y="${h-9}" fill="#61d5e8" font-size="11">Фактическое напряжение</text>
    </svg>`;
  }

  function analogView(r){
    const y=r.yalk;
    const currentNom=y.analogPoints[Math.min(y.analogPoint,2)];
    const viewing=y.viewingPoint;
    const nominal=viewing!=null?viewing:currentNom;
    const snap=viewing!=null?y.analogSnapshots[viewing]:null;
    const values=snap?snap.channels:y.analogFrame;
    const mins=snap?.mins||y.analogMin, maxs=snap?.maxs||y.analogMax;
    const actual=snap?snap.actual:(y.analogActual??nominal);
    const channel=viewing!=null?-1:y.analogChannel;
    const title=viewing!=null?`ЯЛК-96 — аналоговые каналы · просмотр ${String(viewing).replace('.',',')} В`:'ЯЛК-96 — аналоговые каналы';
    return `<section class="stage-view">
      <div class="stage-title-row">
        <h2>${title}</h2>
        <div class="point-tabs">
          ${y.analogPoints.map(p=>{
            const done=!!y.analogSnapshots[p];
            const active=viewing==null && p===currentNom;
            const isView=viewing===p;
            return `<button class="point-tab ${done?'done':''} ${active?'active':''} ${isView?'viewing':''}" data-point="${p}" ${!done&&!active?'disabled':''}>${done?'✓ ':active?'● ':'○ '}${String(p).replace('.',',')} В</button>`;
          }).join('')}
        </div>
        <div class="spacer"></div>
        <button class="btn btn-small" data-full-scale>Полная шкала</button>
        <button class="btn btn-small" data-reset-zoom>Сбросить масштаб</button>
      </div>
      <div class="view-banner ${viewing!=null?'visible':''}">
        ПРОСМОТР ТОЧКИ ${viewing!=null?String(viewing).replace('.',','):'—'} В · сейчас выполняется ${String(currentNom).replace('.',',')} В / канал ${y.analogChannel+1}
      </div>
      ${barChartHtml(values,{min:nominal===0?-.12:nominal-.15,max:nominal===0?.16:nominal+.16,reference:actual,tolerance:.08,stimulated:channel,pinned:y.pinned,kind:'analog',mins,maxs})}
    </section>`;
  }

  function discreteView(r){
    const y=r.yalk,point=y.discretePoints[Math.min(y.discretePoint,2)],expected=point>=2.5?1:0;
    return `<section class="stage-view">
      <div class="stage-title-row"><h2>ЯЛК-96 — контактные сигналы</h2>
        <div class="point-tabs">
          ${y.discretePoints.map((p,i)=>`<button class="point-tab ${i<y.discretePoint?'done':''} ${i===y.discretePoint?'active':''}" disabled>${i<y.discretePoint?'✓ ':i===y.discretePoint?'● ':'○ '}${String(p).replace('.',',')} В</button>`).join('')}
        </div><div class="spacer"></div>
      </div>
      <div class="digital-split">
        <div class="digital-card">
          <div class="digital-grid">
            ${y.discreteFrame.map((c,i)=>`<div class="digital-cell ${c.state==='done'?'done':''} ${c.state==='failed'?'failed':''} ${i===y.discreteChannel?'stimulated':''} ${i===y.pinned?'pinned':''}" data-digital="${i}">
              <span class="idx">${i+1}</span>${c.logic}
            </div>`).join('')}
          </div>
        </div>
        <div class="analog-card">
          ${barPlotOnly(y.discreteFrame.map(c=>c.analog),{min:point-.18,max:point+.18,reference:y.discreteActual??point,tolerance:.09,stimulated:y.discreteChannel,pinned:y.pinned,kind:'discrete',mins:y.discreteFrame.map(c=>c.min),maxs:y.discreteFrame.map(c=>c.max)})}
        </div>
      </div>
    </section>`;
  }

  function overloadView(r){
    const y=r.yalk;
    const deltas=(y.overloadPhase==='safe'||y.overloadPhase==='snapshot') ? y.overloadDeltaSnapshot : y.overloadDelta;
    return `<section class="stage-view">
      <div class="stage-title-row"><h2>ЯЛК-96 — перегрузка ±12 В</h2>
        <div class="hint">88 каналов · отклонение каждого наблюдаемого канала от baseline · предел ±2 кода</div><div class="spacer"></div>
      </div>
      <div class="overload-method overload-method-v11">
        <span><i class="legend-safe-v11"></i> допустимо: −2…+2</span>
        <span><i class="legend-current"></i> Δcode относительно baseline</span>
        <span><i class="legend-blue"></i> канал воздействия · из критерия исключён</span>
        <span>между воздействиями: источник OFF → RESET → ЦАП OFF</span>
      </div>
      ${overloadDeltaChartHtml(deltas,{stimulated:y.overloadPhase==='impact'?y.overloadChannel:-1,pinned:y.pinned})}
    </section>`;
  }

  function ytpView(r){
    const y=r.ytp,p=y.points[Math.min(y.pointIndex,2)];
    const min=p===0?-3:p-5,max=p===0?5:p+5;
    return `<section class="stage-view">
      <div class="stage-title-row"><h2>ЯТП — сопротивление каналов</h2>
        <div class="point-tabs">${y.points.map((v,i)=>`<button class="point-tab ${i<y.pointIndex?'done':''} ${i===y.pointIndex?'active':''}" disabled>${i<y.pointIndex?'✓ ':i===y.pointIndex?'● ':'○ '}${v} Ом</button>`).join('')}</div>
        <div class="spacer"></div>
      </div>
      ${barChartHtml(y.frame,{min,max,reference:p,tolerance:2,stimulated:y.channel,pinned:y.pinned,kind:'ytp',unit:' Ом',mins:y.min,maxs:y.max})}
    </section>`;
  }

  function yvpView(r){
    const y=r.yvp;
    const all=y.frame.flatMap(v=>[v.actual,v.measured]);
    const min=Math.min(...all)-.08,max=Math.max(...all)+.08,range=max-min||1;
    const yPct=v=>clamp((v-min)/range*100,0,100);
    const lines=Array.from({length:5},(_,i)=>{const value=min+(max-min)*(4-i)/4;return `<div class="y-grid-line" style="top:${i*25}%"><span class="y-label">${fmt(value,3)} В</span></div>`}).join('');
    return `<section class="stage-view">
      <div class="stage-title-row"><h2>ЯВП-8 — 8 каналов</h2><div class="hint">сопоставление поданного и измеренного значения по всем каналам</div><div class="spacer"></div></div>
      <div class="chart-card yvp-plane-card">
        <div class="chart-head"><strong>Подано / измерено</strong><span class="meta">8 каналов на одной плоскости</span><div class="spacer"></div><span class="yvp-key"><i class="sent"></i>подано <i class="measured"></i>измерено</span></div>
        <div class="yvp-plane">
          <div class="y-grid">${lines}</div>
          <div class="yvp-channel-row">${y.frame.map((v,i)=>`<div class="yvp-channel-slot ${i===y.channel?'stimulated':''} ${i===y.pinned?'pinned':''}" data-yvp="${i}">
            <div class="yvp-pair"><div class="yvp-bar sent" style="height:${Math.max(1,yPct(v.actual))}%"><span>${fmt(v.actual,3)}</span></div><div class="yvp-bar measured" style="height:${Math.max(1,yPct(v.measured))}%"><span>${fmt(v.measured,3)}</span></div></div>
            <div class="yvp-channel-label">${i+1}</div>
          </div>`).join('')}</div>
        </div>
      </div>
    </section>`;
  }

  function barChartHtml(values,opt){
    return `<div class="chart-card">
      <div class="chart-head"><strong>${opt.kind==='ytp'?'30 каналов ЯТП':'80 каналов ЯЛК'}</strong><span class="meta">live array · click — закрепить канал</span><div class="spacer"></div></div>
      ${barPlotOnly(values,opt)}
    </div>`;
  }
  function barPlotOnly(values,opt){
    let [min,max]=resolvedRange(opt.kind,opt.min,opt.max);
    const range=max-min||1,plotH=100;
    const yPct=v=>clamp((v-min)/range*100,0,100);
    const refPct=yPct(opt.reference);
    const tolTop=yPct(opt.reference+(opt.tolerance||0)),tolBottom=yPct(opt.reference-(opt.tolerance||0));
    const lines=Array.from({length:5},(_,i)=>{
      const val=min+(max-min)*(4-i)/4;
      return `<div class="y-grid-line" style="top:${i*25}%"><span class="y-label">${fmt(val,opt.kind==='ytp'?1:3)}${opt.unit||''}</span></div>`;
    }).join('');
    return `<div class="plot" data-plot-kind="${opt.kind}">
      <div class="y-grid">${lines}
        <div class="tolerance-band" style="bottom:${tolBottom}%;height:${Math.max(1,tolTop-tolBottom)}%"></div>
        <div class="reference-line" style="bottom:${refPct}%"></div>
      </div>
      <div class="bar-row">
        ${values.map((v,i)=>{
          const dev=Math.abs(v-opt.reference);
          const failed=opt.tolerance!=null && dev>opt.tolerance;
          const stimulated=i===opt.stimulated,pinned=i===opt.pinned;
          const label=opt.kind==='ytp'?fmt(v,1):fmt(v,3);
          const lo=opt.mins?.[i]??v, hi=opt.maxs?.[i]??v;
          const loPct=yPct(lo),hiPct=yPct(hi);
          return `<div class="bar-slot" data-bar-index="${i}" data-bar-value="${v}">
            <div class="range-whisker" style="bottom:${Math.min(loPct,hiPct)}%;height:${Math.max(1,Math.abs(hiPct-loPct))}%"><i></i><b></b></div>
            <div class="bar ${failed?'failed':''} ${stimulated?'stimulated':''} ${pinned?'pinned':''}" style="height:${Math.max(1,yPct(v))}%">
              <span class="bar-label">${label}</span>
            </div>
            <span class="x-label">${(i+1)%5===0||i===0?i+1:''}</span>
          </div>`;
        }).join('')}
      </div>
    </div>`;
  }

  function overloadDeltaChartHtml(deltas,opt){
    const [min,max]=resolvedRange('overload',-3.5,3.5),range=max-min||1;
    const topPct=v=>clamp((max-v)/range*100,0,100),zero=topPct(0),safeTop=topPct(2),safeBottom=topPct(-2);
    const ticks=[3,2,1,0,-1,-2,-3];
    const grid=ticks.map(val=>`<div class="delta-grid-line-v12 ${Math.abs(val)===2?'limit':''} ${val===0?'zero':''}" style="top:${topPct(val)}%"><span>${val>0?'+':''}${val}</span></div>`).join('');
    const ok=deltas.filter((d,i)=>i!==opt.stimulated&&Math.abs(d)<=2).length;
    const fail=deltas.filter((d,i)=>i!==opt.stimulated&&Math.abs(d)>2).length;
    return `<div class="chart-card overload-card-v12">
      <div class="chart-head"><strong>88 каналов · Δcode</strong><span class="meta">baseline = 0 · предел ±2 · наблюдаются остальные 87 каналов</span><div class="spacer"></div><span class="delta-summary ${fail?'bad':''}">${fail?`НЕ НОРМА: ${fail}`:`в допуске: ${ok}`}</span></div>
      <div class="delta-plot-v12 overload-row">
        ${grid}
        <div class="delta-safe-v12" style="top:${safeTop}%;height:${Math.max(0,safeBottom-safeTop)}%"></div>
        <div class="delta-bars-v12">${deltas.map((d,i)=>{
          const excluded=i===opt.stimulated;
          const y=topPct(d), top=Math.min(y,zero), height=Math.max(1.5,Math.abs(y-zero));
          const fail=Math.abs(d)>2&&!excluded,pinned=i===opt.pinned;
          return `<div class="delta-slot-v12 ${excluded?'excluded':''} ${pinned?'pinned':''}" data-overload="${i}" data-delta="${d}">
            ${excluded?'<div class="delta-excluded-lane-v12"><span>×</span></div>':`<div class="delta-stem-v12 ${fail?'failed':''}" style="top:${top}%;height:${height}%"></div><div class="delta-marker-v12 ${fail?'failed':''}" style="top:${y}%"></div>`}
            <span class="delta-x-v12">${(i===0||(i+1)%8===0)?i+1:''}</span>
          </div>`;
        }).join('')}</div>
      </div>
    </div>`;
  }

  function telemetryHtml(r){
    const progress=stageProgress(r);
    const points=r.current.history;
    const stale=!r.currentFresh;
    return `
      <div class="tele-box">
        <div class="tele-title">Время</div>
        <div class="tele-value">${formatDuration(r.displayElapsed)}</div>
        <div class="tele-sub">${r.etaMode==='waiting_operator'?'Ожидание действия оператора':'фактически прошедшее время'}</div>
      </div>
      <div class="tele-box">
        <div class="tele-title">Ход текущей процедуры</div>
        <div class="proc-line"><span>${progress.label}</span><strong>${progress.text}</strong></div>
        <div class="progress-track"><div class="progress-fill" style="width:${clamp(progress.fraction*100,0,100)}%"></div></div>
        <div class="tele-sub">${progress.sub||''}</div>
      </div>
      <div class="tele-box">
        <div class="tele-title">Потребление УБСИ — I, АКИП</div>
        <div class="current-box">
          <div class="current-chart">${currentSvg(points,stale)}</div>
          <div class="current-readout">
            <strong>${stale?'—':fmt(r.current.value,3)+' А'}</strong>
            <div class="tele-sub"><span class="fresh-dot ${stale?'stale':''}"></span>${stale?'НЕТ СВЕЖИХ ДАННЫХ':'данные свежие'}</div>
          </div>
        </div>
      </div>`;
  }
  function currentSvg(points,stale){
    const w=300,h=82,pad=6;
    const ys=points.map(p=>p.y),min=.16,max=.48;
    const path=points.map((p,i)=>{
      const x=pad+i*(w-2*pad)/Math.max(1,points.length-1);
      const y=h-pad-(p.y-min)/(max-min)*(h-2*pad);
      return `${i?'L':'M'} ${x.toFixed(1)} ${y.toFixed(1)}`
    }).join(' ');
    return `<svg class="current-svg" viewBox="0 0 ${w} ${h}" preserveAspectRatio="none">
      <line x1="0" y1="${h-18}" x2="${w}" y2="${h-18}" stroke="rgba(80,113,134,.18)"/>
      <line x1="0" y1="18" x2="${w}" y2="18" stroke="rgba(80,113,134,.18)"/>
      <path d="${path}" fill="none" stroke="${stale?'#ef5a5a':'#35cf79'}" stroke-width="2"/>
    </svg>`;
  }
  function etaForRun(r){
    if(r.etaMode==='waiting_operator')return null;

    const powerRemaining=()=>{
      if(r.stage!=='power')return 0;
      let left=Math.max(0,(r.power.steps[r.power.index]?.hold||0)-r.power.stepElapsed);
      for(let i=r.power.index+1;i<r.power.steps.length;i++)left+=r.power.steps[i].hold;
      return left;
    };
    const analogRemaining=()=>{
      const y=r.yalk;
      const completed=y.analogPoint*80+y.analogChannel;
      return Math.max(0,240-completed)*1.15;
    };
    const discreteRemaining=()=>{
      const y=r.yalk;
      const completed=y.discretePoint*80+y.discreteChannel;
      return Math.max(0,240-completed)*1.15;
    };
    const overloadRemaining=()=>{
      const y=r.yalk;
      const total=176,completed=Math.max(0,y.overloadExposure-1);
      let current=0;
      if(y.overloadPhase==='baseline')current=Math.max(0,1-y.overloadPhaseElapsed)+.3;
      else if(y.overloadPhase==='safe')current=Math.max(0,.3-y.overloadPhaseElapsed)+10+.15;
      else if(y.overloadPhase==='impact')current=Math.max(0,10-y.overloadPhaseElapsed)+.15+.3;
      else if(y.overloadPhase==='snapshot')current=Math.max(0,.15-y.overloadPhaseElapsed)+.3;
      return current+Math.max(0,total-completed-1)*(10+.15+.3);
    };
    const ytpRemaining=()=>{
      const y=r.ytp;
      const pointsLeft=Math.max(0,3-y.pointIndex);
      if(y.waiting)return null;
      const currentPoint=Math.max(0,(y.phase==='settle'?1.5-y.phaseElapsed:0))
        +Math.max(0,30-y.channel)*.25;
      return currentPoint+Math.max(0,pointsLeft-1)*(1.5+30*.25);
    };
    const yvpRemaining=()=>{
      const y=r.yvp,total=8*y.gains.length*y.frequencies.length;
      const done=y.channel*y.gains.length*y.frequencies.length+y.gainIndex*y.frequencies.length+y.frequencyIndex;
      return Math.max(0,total-done)*.2;
    };

    const order=['power','analog','discrete','overload','ytp','yvp'];
    const fixedFuture={
      power:0,
      analog:240*1.15,
      discrete:240*1.15,
      overload:1+.3+176*(10+.15+.3),
      ytp:3*(1.5+30*.25),
      yvp:8*7*7*.2
    };
    const currentFn={power:powerRemaining,analog:analogRemaining,discrete:discreteRemaining,overload:overloadRemaining,ytp:ytpRemaining,yvp:yvpRemaining};
    let seen=false,total=0;
    for(const s of order){
      if(s===r.stage){
        seen=true;
        const cur=currentFn[s]();
        if(cur==null)return null;
        total+=cur;
      } else if(seen) total+=fixedFuture[s];
    }
    return total;
  }
  function stageProgress(r){
    if(r.stage==='power'){
      const p=r.power,step=p.steps[Math.min(p.index,p.steps.length-1)];
      const fraction=(p.index+(step?.hold?clamp(p.stepElapsed/step.hold,0,1):1))/7;
      return {
        label:'Последовательность питания',
        text:`${Math.min(p.index+1,7)} / 7`,
        fraction,
        sub:step?.prototypeCompressed?`реальная выдержка ${formatDuration(step.scenarioSeconds)} · в прототипе ${formatDuration(step.hold)}`:(step?.hold>1?`выдержка ${formatDuration(p.stepElapsed)} / ${formatDuration(step.hold)}`:'')
      };
    }
    if(r.stage==='analog'){
      const y=r.yalk,p=y.analogPoint,ch=y.analogChannel;return {label:`ЯЛК analog · ${String(y.analogPoints[Math.min(p,2)]).replace('.',',')} В`,text:`канал ${ch+1} / 80`,fraction:(p*80+ch)/240,sub:`точка ${Math.min(p+1,3)} / 3 · point-major`};
    }
    if(r.stage==='discrete'){
      const y=r.yalk;return {label:`ЯЛК discrete · ${String(y.discretePoints[Math.min(y.discretePoint,2)]).replace('.',',')} В`,text:`канал ${y.discreteChannel+1} / 80`,fraction:(y.discretePoint*80+y.discreteChannel)/240,sub:`точка ${Math.min(y.discretePoint+1,3)} / 3 · 0/1 + analog live`};
    }
    if(r.stage==='overload'){
      const y=r.yalk;
      const frac=clamp((y.overloadExposure-1+(y.overloadPhase==='impact'?y.overloadPhaseElapsed/10:0))/176,0,1);
      const phase={baseline:'baseline',safe:'RESET / ЦАП OFF',impact:'выдержка',snapshot:'снимок'}[y.overloadPhase];
      return {label:`Перегрузка ${y.overloadPolarity>0?'+':''}${y.overloadPolarity} В`,text:`${Math.min(y.overloadExposure,176)} / 176`,fraction:frac,sub:`канал ${y.overloadChannel+1} / 88 · ${phase}${y.overloadPhase==='impact'?` ${fmt(y.overloadPhaseElapsed,1)} / 10,0 с`:''}`};
    }
    if(r.stage==='ytp'){
      const y=r.ytp;return {label:`ЯТП · ${y.points[Math.min(y.pointIndex,2)]} Ом`,text:`канал ${y.channel+1} / 30`,fraction:(y.pointIndex*30+y.channel)/90,sub:y.waiting?'ожидание оператора':y.phase==='settle'?`стабилизация ${fmt(y.phaseElapsed,1)} / 0,8 с`:`считывание · точка ${Math.min(y.pointIndex+1,3)} / 3`};
    }
    if(r.stage==='yvp'){
      const y=r.yvp,total=8*y.gains.length*y.frequencies.length;
      const done=y.channel*y.gains.length*y.frequencies.length+y.gainIndex*y.frequencies.length+y.frequencyIndex;
      return {label:'ЯВП-8 · подано / измерено',text:`${done+1} / ${total}`,fraction:done/total,sub:`канал ${y.channel+1}/8 · K=${y.gains[y.gainIndex]} · f=${y.frequencies[y.frequencyIndex]} Гц`};
    }
    return {label:'—',text:'—',fraction:0,sub:''};
  }

  function wireStageInteractions(r,stage){
    document.querySelectorAll('[data-bar-index]').forEach(slot=>{
      const i=+slot.dataset.barIndex;
      slot.onclick=()=>{
        if(stage==='analog'||stage==='discrete')r.yalk.pinned = r.yalk.pinned===i?null:i;
        if(stage==='ytp')r.ytp.pinned = r.ytp.pinned===i?null:i;
        if(state.screen==='tu-runtime') paintTuRuntimeLive(); else paintRuntimeLive();
      };
      slot.onmousemove=e=>{
        const v=+slot.dataset.barValue;
        showTooltip(e.clientX,e.clientY,`Канал ${i+1}`,`${stage==='ytp'?fmt(v,2)+' Ом':fmt(v,3)+' В'}`);
      };
      slot.onmouseleave=hideTooltip;
    });
    document.querySelectorAll('[data-digital]').forEach(cell=>{
      const i=+cell.dataset.digital;
      cell.onclick=()=>{r.yalk.pinned=r.yalk.pinned===i?null:i;(state.screen==='tu-runtime'?paintTuRuntimeLive:paintRuntimeLive)()};
      cell.onmousemove=e=>showTooltip(e.clientX,e.clientY,`Канал ${i+1}`,`live: ${r.yalk.discreteFrame[i].logic}`);
      cell.onmouseleave=hideTooltip;
    });
    document.querySelectorAll('[data-overload]').forEach(slot=>{
      const i=+slot.dataset.overload;
      slot.onclick=()=>{r.yalk.pinned=r.yalk.pinned===i?null:i;(state.screen==='tu-runtime'?paintTuRuntimeLive:paintRuntimeLive)()};
      slot.onmousemove=e=>showTooltip(e.clientX,e.clientY,`Канал ${i+1}`,`Δcode: ${slot.dataset.delta}`);
      slot.onmouseleave=hideTooltip;
    });
    document.querySelectorAll('[data-yvp]').forEach(slot=>{
      const i=+slot.dataset.yvp;
      slot.onclick=()=>{r.yvp.pinned=r.yvp.pinned===i?null:i;(state.screen==='tu-runtime'?paintTuRuntimeLive:paintRuntimeLive)()};
      slot.onmousemove=e=>{const v=r.yvp.frame[i];showTooltip(e.clientX,e.clientY,`Канал ${i+1}`,`подано ${fmt(v.actual,3)} В · измерено ${fmt(v.measured,3)} В`)};
      slot.onmouseleave=hideTooltip;
    });
    document.querySelectorAll('[data-point]').forEach(btn=>{
      btn.onclick=()=>{
        const p=+btn.dataset.point;
        const current=r.yalk.analogPoints[Math.min(r.yalk.analogPoint,2)];
        if(p===current && r.yalk.viewingPoint==null)return;
        r.yalk.viewingPoint = r.yalk.viewingPoint===p?null:p;
        paintRuntimeLive();
      };
    });
    document.querySelectorAll('[data-reset-zoom]').forEach(b=>b.onclick=()=>{
      resetZoom(r,stage);
      if(state.screen==='tu-runtime')paintTuRuntimeLive();else paintRuntimeLive();
    });
    document.querySelectorAll('[data-full-scale]').forEach(b=>b.onclick=()=>{
      ensureZoom(r).analog.full=!ensureZoom(r).analog.full;
      if(state.screen==='tu-runtime')paintTuRuntimeLive();else paintRuntimeLive();
    });

    document.querySelectorAll('.plot[data-plot-kind]').forEach(plot=>{
      const kind=plot.dataset.plotKind;
      if(!['analog','discrete','ytp'].includes(kind))return;
      plot.onwheel=e=>{
        e.preventDefault();
        const z=ensureZoom(r)[kind];
        z.full=false;
        z.scale=clamp(z.scale*(e.deltaY<0?1.18:0.84),1,12);
        if(state.screen==='tu-runtime')paintTuRuntimeLive();else paintRuntimeLive();
      };
      plot.ondblclick=()=>{
        resetZoom(r,kind);
        if(state.screen==='tu-runtime')paintTuRuntimeLive();else paintRuntimeLive();
      };
      plot.onmousedown=e=>{
        if(e.button!==0)return;
        const z=ensureZoom(r)[kind],startY=e.clientY,startShift=z.shift;
        const move=ev=>{
          z.full=false;
          z.shift=startShift+(ev.clientY-startY)/Math.max(120,plot.clientHeight)/z.scale;
          if(state.screen==='tu-runtime')paintTuRuntimeLive();else paintRuntimeLive();
        };
        const up=()=>{window.removeEventListener('mousemove',move);window.removeEventListener('mouseup',up)};
        window.addEventListener('mousemove',move);window.addEventListener('mouseup',up);
      };
    });
    document.querySelectorAll('.overload-row').forEach(plot=>{
      plot.onwheel=e=>{
        e.preventDefault();const z=ensureZoom(r).overload;z.scale=clamp(z.scale*(e.deltaY<0?1.18:.84),1,12);
        if(state.screen==='tu-runtime')paintTuRuntimeLive();else paintRuntimeLive();
      };
      plot.ondblclick=()=>{resetZoom(r,'overload');if(state.screen==='tu-runtime')paintTuRuntimeLive();else paintRuntimeLive()};
    });
  }

  let tooltipEl=null;
  function showTooltip(x,y,title,body){
    hideTooltip();
    tooltipEl=document.createElement('div');tooltipEl.className='tooltip';
    tooltipEl.innerHTML=`<strong>${escapeHtml(title)}</strong><div class="muted">${escapeHtml(body)}</div>`;
    document.body.appendChild(tooltipEl);
    tooltipEl.style.left=Math.min(window.innerWidth-180,x+12)+'px';
    tooltipEl.style.top=Math.min(window.innerHeight-70,y+12)+'px';
  }
  function hideTooltip(){if(tooltipEl){tooltipEl.remove();tooltipEl=null}}

  function showR4831Modal(value){
    const run=activeRun();
    if(!run||run.stage!=='ytp'||!run.ytp.waiting)return;
    const pointIndex=run.ytp.pointIndex;
    if(run.ytp.modalPoint===pointIndex && modalRoot.querySelector('#confirm-r4831'))return;
    run.ytp.modalPoint=pointIndex;
    modalRoot.innerHTML=`<div class="modal-backdrop"><div class="modal">
      <h3>Установите Р4831: ${value} Ом</h3>
      <p>Сценарий ожидает подтверждения оператора. Телеметрия общего тока продолжает обновляться.</p>
      <div class="modal-actions"><button class="btn btn-primary" id="confirm-r4831">Подтвердить</button></div>
    </div></div>`;
    document.getElementById('confirm-r4831').onclick=()=>{
      const active=activeRun();
      if(!active||active.stage!=='ytp'||active.ytp.pointIndex!==pointIndex)return;
      modalRoot.innerHTML='';
      active.ytp.waiting=false;active.ytp.modalPoint=null;
      active.etaMode='planned';
      active.ytp.frame=makeYtpFrame(value);active.ytp.min=active.ytp.frame.slice();active.ytp.max=active.ytp.frame.slice();
      active.ytp.phase='settle';active.ytp.phaseElapsed=0;
      if(state.screen==='tu-runtime') paintTuRuntimeLive(); else paintRuntimeLive();
    };
  }


  function showStandErrorModal(message){
    stopSimulation();
    const r=state.run;
    if(r)r.standError=true;
    modalRoot.innerHTML=`<div class="modal-backdrop"><div class="modal">
      <h3>ОШИБКА СТЕНДА</h3>
      <p>${escapeHtml(message)}</p>
      <div class="modal-actions">
        <button class="btn" id="stand-safe-finish">Завершить безопасно</button>
        <button class="btn btn-primary" id="stand-retry">Повторить / продолжить</button>
      </div>
    </div></div>`;
    document.getElementById('stand-safe-finish').onclick=()=>{
      modalRoot.innerHTML='';
      if(state.run){state.run.stopped=true;state.screen='finish';render()}
    };
    document.getElementById('stand-retry').onclick=()=>{
      modalRoot.innerHTML='';
      if(state.run){
        state.run.standError=false;
        state.run.currentFresh=true;
        state.screen='runtime';
        render();
        startSimulation();
      }
    };
  }

  function confirmModal(title,text,actionLabel,onConfirm,danger=false){
    modalRoot.innerHTML=`<div class="modal-backdrop"><div class="modal">
      <h3>${escapeHtml(title)}</h3><p>${escapeHtml(text)}</p>
      <div class="modal-actions">
        <button class="btn" id="modal-cancel">Продолжить испытание</button>
        <button class="btn ${danger?'btn-danger':'btn-primary'}" id="modal-confirm">${escapeHtml(actionLabel)}</button>
      </div>
    </div></div>`;
    document.getElementById('modal-cancel').onclick=()=>modalRoot.innerHTML='';
    document.getElementById('modal-confirm').onclick=()=>{modalRoot.innerHTML='';onConfirm()};
  }

  function renderFinish(){
    const r=state.run;
    const stopped=r?.stopped;
    const stand=r?.standError;
    const bad=r?.productFail;
    const verdict=stand?'ОШИБКА СТЕНДА':stopped?'ОСТАНОВЛЕНО':bad?'НЕ НОРМА':'НОРМА';
    const cls=bad||stand?'bad':'ok';
    const blocks=[
      ['Питание',r?.finishedBlocks.power|| (stopped?'ОСТАНОВЛЕНО':'НОРМА')],
      ['ЯЛК-96',r?.finishedBlocks.yalk|| (bad?'НЕ НОРМА':'НОРМА')],
      ['ЯТП',r?.finishedBlocks.ytp|| (stopped?'НЕ ВЫПОЛНЕНО':bad?'НЕ НОРМА':'НОРМА')],
      ['ЯВП-8',r?.finishedBlocks.yvp|| (stopped?'НЕ ВЫПОЛНЕНО':bad?'НЕ НОРМА':'НОРМА')],
    ];
    app.innerHTML=`
      <div class="app-shell page">
        ${topbar('ПРОИЗВОДСТВО',`SN ${r.serial} · завершение`)}
        <div class="finish">
          <div class="finish-grid">
            <section class="result-card">
              <div class="result-main">
                <div><div class="brand-kicker">УБСИ SN ${r.serial}</div><h2>${state.productionStage} · ${state.scope}</h2></div>
                <div class="verdict ${cls}">${verdict}</div>
              </div>
              <div class="summary-table">
                ${blocks.map(([name,status])=>`<div class="summary-row"><div>${name}</div><div class="status ${status==='НОРМА'?'ok':status==='НЕ НОРМА'?'bad':''}">${status}</div></div>`).join('')}
              </div>
              <div style="margin-top:14px" class="field">
                <label>Комментарий испытателя</label>
                <textarea class="comment" id="prod-comment" placeholder="Необязательный комментарий к этому прогону">${escapeHtml(r.operatorComment||'')}</textarea>
              </div>
              <div class="actions-row">
                <button class="btn" id="open-prod-report">Открыть отчёт</button>
                <button class="btn btn-primary" id="next-item" ${state.activeQueueIndex>=state.queue.length-1?'disabled':''}>Следующее изделие</button>
                <button class="btn" id="back-session-finish">Вернуться в сессию</button>
              </div>
            </section>
            <section class="result-card">
              <div class="panel-title" style="padding:0 0 12px;border:0">Краткий технический итог</div>
              <div class="detail-list">
                <div class="detail-item"><strong>Оператор</strong><span>${state.operator}</span></div>
                <div class="detail-item"><strong>Длительность</strong><span>${formatDuration(r.displayElapsed)}</span></div>
                <div class="detail-item"><strong>Run ID</strong><span>20260914-${r.serial}-${String(state.activeQueueIndex+1).padStart(2,'0')}</span></div>
                <div class="detail-item"><strong>ЯЛК</strong><span>80 каналов · точки 0 / 3,1 / 6,2 В · discrete · overload</span></div>
                <div class="detail-item"><strong>ЯТП</strong><span>30 каналов · 0 / 120 / 240 Ом</span></div>
                <div class="detail-item"><strong>ЯВП</strong><span>8 каналов · подано / измерено · общий результат от исполнительной части</span></div>
              </div>
            </section>
          </div>
        </div>
      </div>`;
    const comment=document.getElementById('prod-comment');
    comment.oninput=e=>r.operatorComment=e.target.value;
    const commitHistory=()=>{
      if(r.historyCommitted)return;
      const tail=r.operatorComment?.trim()?` · Комментарий: ${r.operatorComment.trim()}`:'';
      addAdminHistory(r.serial,`Производство · ${state.productionStage} · ${state.scope}${tail}`,verdict);
      r.historyCommitted=true;
    };
    document.getElementById('back-session-finish').onclick=()=>{commitHistory();state.run=null;setScreen('session')};
    document.getElementById('open-prod-report').onclick=()=>showProductionReport(r,blocks,verdict);
    document.getElementById('next-item').onclick=()=>{
      if(state.activeQueueIndex<state.queue.length-1){
        commitHistory();state.activeQueueIndex++;state.prepChecked=false;state.prepOk=false;state.prepBusy=false;state.run=null;setScreen('prep');
      }
    };
  }


  function showProductionReport(r,blocks,verdict){
    modalRoot.innerHTML=`<div class="modal-backdrop"><div class="modal" style="width:min(760px,100%)">
      <h3>Отчёт производственного прогона</h3>
      <p>УБСИ SN ${escapeHtml(r.serial)} · ${escapeHtml(state.productionStage)} · ${escapeHtml(state.scope)}</p>
      <div class="summary-table" style="margin-top:14px">
        ${blocks.map(([name,status])=>`<div class="summary-row"><div>${name}</div><div class="status ${status==='НОРМА'?'ok':status==='НЕ НОРМА'?'bad':''}">${status}</div></div>`).join('')}
      </div>
      <div class="detail-list" style="margin-top:12px">
        <div class="detail-item"><strong>Итог</strong><span>${verdict}</span></div>
        <div class="detail-item"><strong>Этап</strong><span>${escapeHtml(state.productionStage)}</span></div>
        <div class="detail-item"><strong>Объём</strong><span>${escapeHtml(state.scope)}</span></div>
        <div class="detail-item"><strong>Оператор</strong><span>${escapeHtml(state.operator)}</span></div>
        <div class="detail-item"><strong>Run ID</strong><span>20260914-${r.serial}-${String(state.activeQueueIndex+1).padStart(2,'0')}</span></div>
        <div class="detail-item"><strong>Длительность</strong><span>${formatDuration(r.displayElapsed)}</span></div>
        <div class="detail-item"><strong>Комментарий испытателя</strong><span>${r.operatorComment?.trim()?escapeHtml(r.operatorComment.trim()):'—'}</span></div>
      </div>
      <div class="modal-actions"><button class="btn btn-primary" id="prod-report-close">Закрыть</button></div>
    </div></div>`;
    document.getElementById('prod-report-close').onclick=()=>modalRoot.innerHTML='';
  }

  function renderProtoPanel(){
    if(!['runtime','tu-ready','tu-runtime'].includes(state.screen))return;
    const wrap=document.createElement('div');wrap.className='proto-panel';
    let items='';
    if(state.screen==='runtime'){
      items=`
        <button data-proto="fail">Переключить fail канала</button>
        <button data-proto="stale">Переключить stale I</button>
        <button data-proto="error">Ошибка стенда</button>
        <button data-proto="analog">Перейти: analog</button>
        <button data-proto="discrete">Перейти: discrete</button>
        <button data-proto="overload">Перейти: overload</button>
        <button data-proto="ytp">Перейти: ЯТП</button>
        <button data-proto="yvp">Перейти: ЯВП</button>`;
    } else if(state.screen==='tu-ready'){
      items=`<button data-proto="tu-ready-error">Ошибка стенда readiness</button>`;
    } else {
      items=`<button data-proto="tu-fail">ТУ: НЕ НОРМА</button><button data-proto="tu-error">ТУ: ОШИБКА СТЕНДА</button>`;
    }
    wrap.innerHTML=`<div class="proto-menu ${state.protoOpen?'':'hidden'}">${items}</div><button class="proto-toggle">PROTO</button>`;
    document.body.appendChild(wrap);
    wrap.querySelector('.proto-toggle').onclick=()=>{state.protoOpen=!state.protoOpen;wrap.remove();renderProtoPanel()};
    wrap.querySelectorAll('[data-proto]').forEach(b=>b.onclick=()=>{
      const a=b.dataset.proto;
      if(state.screen==='runtime'){
        const r=state.run;if(!r)return;
        if(a==='fail'){r.productFail=!r.productFail;r.failChannel=r.productFail?36:null}
        else if(a==='stale'){r.currentFresh=!r.currentFresh}
        else if(a==='error'){
          state.protoOpen=false;
          showStandErrorModal('Нет свежих данных измерительного тракта. Испытание приостановлено.');
          return;
        } else if(['analog','discrete','overload','ytp','yvp'].includes(a)){setStage(r,a);r.viewStage=null}
        state.protoOpen=false;paintRuntimeLive();
      } else if(state.screen==='tu-ready' && a==='tu-ready-error'){
        state.tu.reportMode='stand_error';state.protoOpen=false;setScreen('tu-report');
      } else if(state.screen==='tu-runtime'){
        if(a==='tu-fail'){
          state.tu.run.tuProductFail=true;
          const i=Math.min(state.tu.run.tuIndex,state.tu.run.tuResults.length-1);
          state.tu.run.tuResults[i].status='ne_norma';
        } else if(a==='tu-error'){
          stopSimulation();state.tu.reportMode='stand_error';state.protoOpen=false;setScreen('tu-report');return;
        }
        state.protoOpen=false;paintTuRuntimeLive();
      }
    });
  }


  // ------------------------- TU branch -------------------------
  const tuRequirements = [
    {id:'1.1.4.13',title:'Готовность после подачи питания',stage:'power'},
    {id:'1.1.4.3',title:'Диапазон и устойчивость питания',stage:'power'},
    {id:'1.1.4.5',title:'Ток потребления',stage:'power'},
    {id:'1.1.4.1',title:'Аналоговые сигналы ЯЛК',stage:'analog'},
    {id:'1.1.4.14',title:'Погрешность измерения',stage:'analog'},
    {id:'1.1.4.1',title:'Контактные сигналы',stage:'discrete'},
    {id:'1.1.4.10',title:'Исходное состояние / обрыв',stage:'discrete'},
    {id:'1.1.4.11',title:'Перегрузка ±12 В',stage:'overload'},
    {id:'1.1.4.9',title:'Эталон 6,2 В',stage:'analog'},
    {id:'1.1.4.1',title:'Температурные каналы ЯТП',stage:'ytp'},
    {id:'1.1.4.7',title:'ЯВП — АЧХ',stage:'yvp'},
    {id:'1.1.4.8',title:'ЯВП — коэффициент усиления',stage:'yvp'},
  ];

  function renderTuEntry(){
    const t=state.tu;
    app.innerHTML=`
      <div class="app-shell page">
        ${topbar('ПРОВЕРКА ПО ТУ','один утверждённый маршрут')}
        <div class="tu-entry tu-entry-final">
          <div>
            <h2 style="margin:0 0 6px">Проверка по ТУ</h2>
            <div style="color:var(--muted);font-size:13px">Выберите оператора и одно зарегистрированное УБСИ. Открытие режима не опрашивает оборудование.</div>
          </div>
          <div class="tu-entry-grid tu-entry-grid-final">
            <section class="tu-choice">
              <h3>Оператор</h3>
              <select class="select" id="tu-operator-select">${operators.map(o=>`<option ${o===t.operator?'selected':''}>${escapeHtml(o)}</option>`).join('')}</select>
              <p>Используется тот же минимальный справочник, что в Производстве.</p>
            </section>
            <section class="tu-choice">
              <h3>Зарегистрированное УБСИ</h3>
              <select class="select" id="tu-registry">${registry.map(x=>`<option value="${x.serial}" ${t.serial===x.serial?'selected':''}>УБСИ ${x.serial}</option>`).join('')}</select>
              <p>Неизвестный серийный номер сначала регистрируется в административном контуре.</p>
            </section>
          </div>
          <div class="session-footer">
            <button class="btn btn-ghost" id="tu-home">← Главная</button>
            <div class="spacer"></div>
            <button class="btn btn-primary" id="tu-check-ready">Проверить готовность</button>
          </div>
        </div>
      </div>`;
    document.getElementById('tu-home').onclick=()=>setScreen('home');
    document.getElementById('tu-operator-select').onchange=e=>t.operator=e.target.value;
    document.getElementById('tu-registry').onchange=e=>t.serial=e.target.value;
    document.getElementById('tu-check-ready').onclick=()=>{t.ready=false;setScreen('tu-ready');setTimeout(()=>{if(state.screen==='tu-ready'){t.ready=true;renderTuReady()}},800)};
  }

  function renderTuReady(){
    const t=state.tu;
    app.innerHTML=`
      <div class="app-shell page">
        ${topbar('ПРОВЕРКА ПО ТУ',`УБСИ SN ${t.serial}`)}
        <div class="tu-ready">
          <div class="tu-ready-card">
            <div>
              <div class="serial">УБСИ SN ${escapeHtml(t.serial)}</div>
              <h2>${t.ready?'СТЕНД ГОТОВ':'ПРОВЕРКА СТЕНДА…'}</h2>
              ${t.ready?'<button class="btn btn-primary" id="tu-start">НАЧАТЬ ПРОВЕРКУ</button>':''}
            </div>
          </div>
        </div>
      </div>`;
    if(t.ready)document.getElementById('tu-start').onclick=startTuRun;
  }

  function makeTuRun(){
    const base=makeRun();
    base.serial=state.tu.serial;
    base.stage='power';
    base.tuIndex=0;
    base.tuResults=tuRequirements.map(x=>({...x,status:'pending'}));
    base.tuProductFail=false;
    base.tuStartedAt=Date.now();
    base.isTu=true;
    return base;
  }

  function startTuRun(){
    state.tu.run=makeTuRun();
    state.screen='tu-runtime';
    render();
    startTuSimulation();
  }

  function startTuSimulation(){
    stopSimulation(); lastFrame=performance.now();
    simTimer=setInterval(()=>{
      if(state.screen!=='tu-runtime'||!state.tu.run)return;
      const r=state.tu.run,now=performance.now(),dt=Math.min(.25,(now-lastFrame)/1000);lastFrame=now;
      const currentReq=r.tuResults[r.tuIndex];
      if(!currentReq)return;
      // Reuse measurement simulators but TU has no Production footer/header.
      const before=r.stage;
      tickRun(r,dt);
      // tickRun can finish Production-style; intercept stage transitions by requirement progress.
      if(state.screen!=='tu-runtime')return;
      syncTuRequirement(r,before);
      paintTuRuntimeLive();
    },120);
  }

  function syncTuRequirement(r,beforeStage){
    const idx=r.tuIndex;
    if(idx>=r.tuResults.length)return;
    const req=r.tuResults[idx];
    req.status='running';

    // ЯТП проходит через реальный операторский контракт 0 / 120 / 240 Ом.
    // Нормативная строка закрывается только после завершения всей 30-канальной процедуры.
    if(req.stage==='ytp'){
      if(r.stage!=='yvp')return;
      req.status=r.tuProductFail?'ne_norma':'norma';
      r.tuIndex++;
      if(r.tuIndex>=r.tuResults.length){finishTuRun(r);return;}
      setTuMeasurementStage(r,r.tuResults[r.tuIndex].stage);
      return;
    }

    // Остальные строки в прототипе используют ускоренное окно просмотра,
    // не меняя структуру HMI и не выдавая прототипное время за нормативное.
    req._elapsed=(req._elapsed||0)+0.12;
    if(req._elapsed<2.4)return;
    req.status=(r.tuProductFail && (idx===4||idx===7))?'ne_norma':'norma';
    r.tuIndex++;
    if(r.tuIndex>=r.tuResults.length){finishTuRun(r);return;}
    setTuMeasurementStage(r,r.tuResults[r.tuIndex].stage);
  }

  function finishTuRun(r){
    stopSimulation();
    state.tu.reportMode=r.tuResults.some(x=>x.status==='ne_norma')?'ne_norma':'norma';
    setScreen('tu-report');
  }

  function setTuMeasurementStage(r,stage){
    if(stage==='power'){r.stage='power';r.power.index=0;r.power.stepElapsed=0}
    else if(stage==='analog'){setStage(r,'analog')}
    else if(stage==='discrete'){setStage(r,'discrete')}
    else if(stage==='overload'){setStage(r,'overload')}
    else if(stage==='ytp'){setStage(r,'ytp')}
    else if(stage==='yvp'){setStage(r,'yvp')}
  }

  function renderTuRuntime(){
    const r=state.tu.run;if(!r)return setScreen('tu-entry');
    app.innerHTML=`
      <div class="app-shell tu-runtime">
        <div class="topbar">
          <div class="title">ПРОВЕРКА ПО ТУ</div>
          <div class="crumb">УБСИ SN ${escapeHtml(state.tu.serial)}</div>
          <div class="spacer"></div>
          <button class="btn btn-small btn-danger" id="tu-stop">Остановить</button>
        </div>
        <div class="tu-body">
          <aside class="tu-list" id="tu-list"></aside>
          <main class="tu-stage" id="tu-stage"></main>
        </div>
      </div>`;
    document.getElementById('tu-stop').onclick=()=>{
      confirmModal('Остановить проверку по ТУ?','Проверка будет завершена как ОШИБКА СТЕНДА/прерванная проверка без нормативной таблицы результата.','Остановить',()=>{
        stopSimulation();state.tu.reportMode='stand_error';setScreen('tu-report');
      },true)
    };
    paintTuRuntimeLive();
    startTuSimulation();
  }

  function paintTuRuntimeLive(){
    if(state.screen!=='tu-runtime')return;
    const r=state.tu.run, list=document.getElementById('tu-list'),stage=document.getElementById('tu-stage');
    if(!r||!list||!stage)return;
    list.innerHTML=r.tuResults.map((x,i)=>{
      const cls=i===r.tuIndex?'active':x.status==='ne_norma'?'bad':x.status==='norma'?'done':'';
      const st=x.status==='norma'?'<span class="tu-state ok">НОРМА</span>':x.status==='ne_norma'?'<span class="tu-state bad">НЕ НОРМА</span>':i===r.tuIndex?'<span class="tu-state">●</span>':'';
      return `<div class="tu-item ${cls}"><div class="tu-id">${escapeHtml(x.id)}</div><div class="tu-name">${escapeHtml(x.title)}</div>${st}</div>`;
    }).join('');
    const req=r.tuResults[Math.min(r.tuIndex,r.tuResults.length-1)];
    stage.innerHTML=`
      <div class="tu-stage-header"><div class="id">${escapeHtml(req.id)}</div><h2>${escapeHtml(req.title)}</h2></div>
      <div style="flex:1;min-height:0">${tuMeasurementBody(r,req.stage)}</div>`;
    wireStageInteractions(r,req.stage);
  }

  function tuMeasurementBody(r,stage){
    // Measurement widget only: no Production context strip/footer.
    if(stage==='power'){
      return `<div class="stage-view" style="padding:0">${powerView(r).replace(/^<section class="stage-view">|<\/section>$/g,'')}</div>`;
    }
    if(stage==='analog'){
      const y=r.yalk,nom=y.analogPoints[Math.min(y.analogPoint,2)],actual=y.analogActual??nom;
      return `<div class="stage-view" style="padding:0">
        ${barChartHtml(y.analogFrame,{min:nom===0?-.12:nom-.15,max:nom===0?.16:nom+.16,reference:actual,tolerance:.08,stimulated:y.analogChannel,pinned:y.pinned,kind:'analog'})}
      </div>`;
    }
    if(stage==='discrete'){
      return discreteView(r).replace(/^<section class="stage-view">|<\/section>$/g,'');
    }
    if(stage==='overload'){
      return overloadView(r).replace(/^<section class="stage-view">|<\/section>$/g,'');
    }
    if(stage==='ytp'){
      return ytpView(r).replace(/^<section class="stage-view">|<\/section>$/g,'');
    }
    if(stage==='yvp'){
      return yvpView(r).replace(/^<section class="stage-view">|<\/section>$/g,'');
    }
    return '';
  }

  function renderTuOperator(){
    const t=state.tu;
    app.innerHTML=`
      <div class="app-shell page">
        ${topbar('ПРОВЕРКА ПО ТУ',`УБСИ SN ${t.serial} · завершено`)}
        <div class="tu-operator">
          <div class="tu-operator-card">
            <h2>Проверка завершена</h2>
            <div class="field">
              <label>Оператор</label>
              <input class="input" id="tu-operator-input" placeholder="ФИО" value="${escapeHtml(t.operator)}" />
            </div>
            <div class="actions-row" style="justify-content:flex-end">
              <button class="btn btn-primary" id="tu-make-report">СФОРМИРОВАТЬ ОТЧЁТ</button>
            </div>
          </div>
        </div>
      </div>`;
    document.getElementById('tu-make-report').onclick=()=>{
      const v=document.getElementById('tu-operator-input').value.trim();
      if(!v){showInfoModal('ФИО не указано','Введите ФИО оператора перед формированием отчёта.');return}
      t.operator=v;setScreen('tu-report');
    };
  }

  function renderTuReport(){
    const t=state.tu,r=t.run;
    const mode=t.reportMode;
    const overall=mode==='norma'?'НОРМА':mode==='ne_norma'?'НЕ НОРМА':'ОШИБКА СТЕНДА';
    const rows=mode==='stand_error'?[]:(r?.tuResults||[]).filter(x=>x.status==='norma'||x.status==='ne_norma');
    app.innerHTML=`
      <div class="tu-report-page">
        <div class="tu-report">
          <h1>ОТЧЁТ</h1>
          <div class="tu-report-meta"><table>
            <tr><td><strong>Дата:</strong></td><td>${new Date().toLocaleDateString('ru-RU')}</td></tr>
            <tr><td><strong>Оператор:</strong></td><td>${escapeHtml(t.operator)}</td></tr>
          </table></div>
          <div class="tu-verdict ${mode==='ne_norma'?'bad':mode==='stand_error'?'error':''}">
            <div class="unit">УБСИ</div>
            <div class="result">${overall}</div>
          </div>
          ${rows.length?`<table class="tu-report-table">${rows.map(x=>`<tr class="${x.status==='ne_norma'?'bad':''}">
            <td>ТУ ${escapeHtml(x.id)} — ${escapeHtml(x.title)}</td>
            <td>${x.status==='norma'?'Норма':'Не норма'}</td>
          </tr>`).join('')}</table>`:''}
          <div class="tu-report-actions">
            <button class="btn" id="tu-print">Печать</button>
            <button class="btn" id="tu-new-check">Новая проверка</button>
            <button class="btn" id="tu-back-home">Вернуться</button>
          </div>
        </div>
      </div>`;
    document.getElementById('tu-back-home').onclick=()=>{state.tu.run=null;state.tu.ready=false;setScreen('home')};
    document.getElementById('tu-new-check').onclick=()=>{state.tu.run=null;state.tu.ready=false;state.tu.operator='';setScreen('tu-entry')};
    document.getElementById('tu-print').onclick=()=>window.print();
  }

  function showInfoModal(title,text){
    modalRoot.innerHTML=`<div class="modal-backdrop"><div class="modal"><h3>${escapeHtml(title)}</h3><p>${escapeHtml(text)}</p>
    <div class="modal-actions"><button class="btn btn-primary" id="info-ok">ОК</button></div></div></div>`;
    document.getElementById('info-ok').onclick=()=>modalRoot.innerHTML='';
  }

  // ------------------------- Administration -------------------------
  const adminComponents = {
    '345':{yalk:'YALK-1842',ytp:'YTP-442',yvp:'YVP-118',ypp:'YPP-077'},
    '346':{yalk:'YALK-1971',ytp:'YTP-451',yvp:'YVP-121',ypp:'YPP-081'},
    '347':{yalk:'YALK-2033',ytp:'YTP-454',yvp:'YVP-126',ypp:'YPP-083'},
    '348':{yalk:'YALK-2060',ytp:'YTP-458',yvp:'YVP-129',ypp:'YPP-084'},
  };
  const adminHistory = {
    '345':[
      ['14.09.2026 12:41','Производство · Климат Н.У. · ЯЛК-96','НОРМА'],
      ['14.09.2026 11:20','Замена ЯЛК · YALK-1730 → YALK-1842',''],
      ['13.09.2026 16:03','Проверка по ТУ','НОРМА'],
    ],
    '346':[
      ['14.09.2026 10:12','Производство · Климат +50 · Полная УБСИ','НОРМА'],
      ['12.09.2026 15:08','Регистрация изделия и состава',''],
    ],
  };

  function renderAdmin(){
    const a=state.admin;
    app.innerHTML=`
      <div class="app-shell page">
        <div class="topbar">
          <button class="btn btn-small btn-ghost" id="admin-home">← Главная</button>
          <div class="title">АДМИНИСТРИРОВАНИЕ</div>
          <div class="crumb">изделия, состав и история</div>
          <div class="spacer"></div>
          <div class="clock" data-clock>${nowText()}</div>
        </div>
        <div class="admin">
          <aside class="admin-list">
            <div class="admin-list-head">УБСИ</div>
            <div class="search-wrap"><input class="input" id="admin-search" placeholder="Поиск по SN..." value="${escapeHtml(a.filter)}" /></div>
            <div class="admin-items" id="admin-items"></div>
          </aside>
          <main class="admin-main" id="admin-main"></main>
        </div>
      </div>`;
    document.getElementById('admin-home').onclick=()=>setScreen('home');
    document.getElementById('admin-search').oninput=e=>{a.filter=e.target.value;paintAdminList();};
    paintAdminList();paintAdminMain();
  }
  function paintAdminList(){
    const a=state.admin,root=document.getElementById('admin-items');if(!root)return;
    const needle=a.filter.trim();
    const items=registry.filter(x=>!needle||x.serial.includes(needle));
    root.innerHTML=items.map(x=>`<div class="admin-item ${a.selected===x.serial?'active':''}" data-admin="${x.serial}">
      <strong>SN ${x.serial}</strong><span class="meta">${x.date}</span>
    </div>`).join('');
    root.querySelectorAll('[data-admin]').forEach(el=>el.onclick=()=>{a.selected=el.dataset.admin;paintAdminList();paintAdminMain()});
  }
  function paintAdminMain(){
    const serial=state.admin.selected, root=document.getElementById('admin-main');if(!root)return;
    const c=adminComponents[serial]||{yalk:'—',ytp:'—',yvp:'—',ypp:'—'};
    const hist=adminHistory[serial]||[];
    const filtered=hist.filter(x=>{
      const f=state.admin.historyFilter;
      if(f==='Все')return true;
      const text=x[1];
      if(f==='Испытания')return text.includes('Производство');
      if(f==='Состав')return /Замена|Регистрация|Перенос/.test(text);
      if(f==='ТУ')return text.includes('ТУ');
      return true;
    });
    root.innerHTML=`
      <section class="admin-card">
        <div style="display:flex;align-items:center;gap:12px"><h2>УБСИ SN ${serial}</h2><div style="flex:1"></div>
          <button class="btn btn-small" id="admin-new">Новое УБСИ</button>
        </div>
        <div class="component-grid">
          ${adminComponent('ЯЛК-96',c.yalk)}
          ${adminComponent('ЯТП',c.ytp)}
          ${adminComponent('ЯВП-8',c.yvp)}
          ${adminComponent('ЯП-П',c.ypp)}
        </div>
      </section>
      <section class="admin-card">
        <div style="display:flex;align-items:center;gap:8px;margin-bottom:12px">
          <h3 style="margin:0">История</h3><div style="flex:1"></div>
          ${['Все','Испытания','Состав','ТУ'].map(f=>`<button class="scope-chip ${state.admin.historyFilter===f?'active':''}" data-hfilter="${f}" style="min-width:auto;padding:7px 9px">${f}</button>`).join('')}
        </div>
        <div class="history">
          ${filtered.length?filtered.map((x,i)=>`<div class="history-row" data-history-index="${i}" style="cursor:pointer"><div class="time">${x[0]}</div><div class="kind">${x[1]}</div><div class="status">${x[2]}</div></div>`).join(''):'<div style="color:var(--muted)">Нет событий по выбранному фильтру.</div>'}
        </div>
      </section>`;
    root.querySelectorAll('[data-replace]').forEach(b=>b.onclick=()=>showReplaceModal(b.dataset.replace,b.dataset.sn));
    root.querySelectorAll('[data-component-history]').forEach(b=>b.onclick=()=>showComponentHistory(b.dataset.componentHistory));
    root.querySelectorAll('[data-hfilter]').forEach(b=>b.onclick=()=>{state.admin.historyFilter=b.dataset.hfilter;paintAdminMain()});
    root.querySelectorAll('[data-history-index]').forEach(row=>row.onclick=()=>{
      const entry=filtered[+row.dataset.historyIndex];showHistoryDetails(serial,entry);
    });
    document.getElementById('admin-new').onclick=showNewUbsiModal;
  }
  function adminComponent(type,sn){
    const empty=sn==='—';
    return `<div class="component ${empty?'empty':''}"><div class="type">${type}</div><div class="sn">${sn}</div>
      <div style="display:flex;gap:6px">
        <button class="btn btn-small" data-replace="${type}" data-sn="${sn}">${empty?'Добавить':'Заменить'}</button>
        ${empty?'':`<button class="btn btn-small" data-component-history="${sn}">История</button>`}
      </div>
    </div>`;
  }
  function componentKey(type){
    return ({'ЯЛК-96':'yalk','ЯТП':'ytp','ЯВП-8':'yvp','ЯП-П':'ypp'})[type];
  }
  function findComponentOwner(sn,excludeSerial=null){
    for(const [serial,comp] of Object.entries(adminComponents)){
      for(const [key,value] of Object.entries(comp)){
        if(value===sn && serial!==excludeSerial)return {serial,key};
      }
    }
    return null;
  }
  function addAdminHistory(serial,text,status=''){
    if(!adminHistory[serial])adminHistory[serial]=[];
    adminHistory[serial].unshift([new Date().toLocaleString('ru-RU',{day:'2-digit',month:'2-digit',year:'numeric',hour:'2-digit',minute:'2-digit'}),text,status]);
  }
  function showReplaceModal(type,current){
    const targetSerial=state.admin.selected,key=componentKey(type);
    modalRoot.innerHTML=`<div class="modal-backdrop"><div class="modal">
      <h3>${current==='—'?'Установить':'Заменить'} ${escapeHtml(type)}</h3>
      ${current==='—'?'':`<p>Текущий компонент: ${escapeHtml(current)}</p>`}
      <div class="field" style="margin-top:14px"><label>Новый серийный номер</label><input class="input" id="replace-sn" /></div>
      <div class="field" style="margin-top:10px"><label>Комментарий</label><textarea class="comment" id="replace-comment"></textarea></div>
      <div class="modal-actions"><button class="btn" id="replace-cancel">Отмена</button><button class="btn btn-primary" id="replace-ok">Продолжить</button></div>
    </div></div>`;
    document.getElementById('replace-cancel').onclick=()=>modalRoot.innerHTML='';
    document.getElementById('replace-ok').onclick=()=>{
      const sn=document.getElementById('replace-sn').value.trim(),cm=document.getElementById('replace-comment').value.trim();
      if(!sn||!cm)return;
      const owner=findComponentOwner(sn,targetSerial);
      if(owner){
        modalRoot.innerHTML=`<div class="modal-backdrop"><div class="modal">
          <h3>Перенести компонент?</h3>
          <p>${escapeHtml(sn)} сейчас установлен в УБСИ ${escapeHtml(owner.serial)}. Будет выполнен перенос в УБСИ ${escapeHtml(targetSerial)}.</p>
          <div class="field" style="margin-top:12px"><label>Комментарий</label><textarea class="comment" id="transfer-comment">${escapeHtml(cm)}</textarea></div>
          <div class="modal-actions"><button class="btn" id="transfer-cancel">Отмена</button><button class="btn btn-primary" id="transfer-ok">Перенести</button></div>
        </div></div>`;
        document.getElementById('transfer-cancel').onclick=()=>modalRoot.innerHTML='';
        document.getElementById('transfer-ok').onclick=()=>{
          const comment=document.getElementById('transfer-comment').value.trim();if(!comment)return;
          adminComponents[owner.serial][owner.key]='—';
          adminComponents[targetSerial][key]=sn;
          addAdminHistory(owner.serial,`Перенос ${sn} → УБСИ ${targetSerial}. ${comment}`);
          addAdminHistory(targetSerial,`Перенос ${sn} из УБСИ ${owner.serial}. ${comment}`);
          modalRoot.innerHTML='';paintAdminMain();
        };
      } else {
        adminComponents[targetSerial][key]=sn;
        addAdminHistory(targetSerial,current==='—'?`Установка ${type}: ${sn}. ${cm}`:`Замена ${type}: ${current} → ${sn}. ${cm}`);
        modalRoot.innerHTML='';paintAdminMain();
      }
    };
  }
  function showNewUbsiModal(){
    modalRoot.innerHTML=`<div class="modal-backdrop"><div class="modal">
      <h3>Регистрация нового УБСИ</h3>
      ${['SN УБСИ','SN ЯЛК-96','SN ЯТП','SN ЯВП-8','SN ЯП-П'].map((x,i)=>`<div class="field" style="margin-top:${i?8:12}px"><label>${x}</label><input class="input" data-new-field="${i}" /></div>`).join('')}
      <div class="modal-actions"><button class="btn" id="new-cancel">Отмена</button><button class="btn btn-primary" id="new-ok">Зарегистрировать</button></div>
    </div></div>`;
    document.getElementById('new-cancel').onclick=()=>modalRoot.innerHTML='';
    document.getElementById('new-ok').onclick=()=>{
      const vals=[...document.querySelectorAll('[data-new-field]')].map(x=>x.value.trim());
      const [serial,yalk,ytp,yvp,ypp]=vals;
      if(!serial)return;
      if(registry.some(x=>x.serial===serial)){showInfoModal('УБСИ уже существует',`SN ${serial} уже зарегистрирован.`);return}
      registry.unshift({serial,type:'УБСИ',date:new Date().toLocaleDateString('ru-RU')});
      adminComponents[serial]={yalk:yalk||'—',ytp:ytp||'—',yvp:yvp||'—',ypp:ypp||'—'};
      adminHistory[serial]=[];
      const count=[yalk,ytp,yvp,ypp].filter(Boolean).length;
      addAdminHistory(serial,count?`Регистрация изделия · заполнено ячеек ${count}/4`:'Регистрация изделия');
      state.admin.selected=serial;
      modalRoot.innerHTML='';paintAdminList();paintAdminMain();
    };
  }

  function showComponentHistory(sn){
    const events=[];
    for(const [serial,comp] of Object.entries(adminComponents)){
      for(const value of Object.values(comp)){
        if(value===sn)events.push(`Текущая установка: УБСИ ${serial}`);
      }
    }
    for(const [serial,hist] of Object.entries(adminHistory)){
      hist.forEach(x=>{if(x[1].includes(sn))events.push(`${x[0]} · УБСИ ${serial} · ${x[1]}`)});
    }
    modalRoot.innerHTML=`<div class="modal-backdrop"><div class="modal">
      <h3>История компонента ${escapeHtml(sn)}</h3>
      <div class="detail-list" style="margin-top:12px">${events.length?events.map(x=>`<div class="detail-item"><span>${escapeHtml(x)}</span></div>`).join(''):'<div class="detail-item"><span>История пока отсутствует.</span></div>'}</div>
      <div class="modal-actions"><button class="btn btn-primary" id="component-history-close">Закрыть</button></div>
    </div></div>`;
    document.getElementById('component-history-close').onclick=()=>modalRoot.innerHTML='';
  }

  function showHistoryDetails(serial,entry){
    if(!entry)return;
    const isRun=/Производство|Проверка по ТУ/.test(entry[1]);
    modalRoot.innerHTML=`<div class="modal-backdrop"><div class="modal">
      <h3>${isRun?'Сводка испытания':'Событие состава'}</h3>
      <div class="detail-list" style="margin-top:12px">
        <div class="detail-item"><strong>УБСИ</strong><span>SN ${escapeHtml(serial)}</span></div>
        <div class="detail-item"><strong>Дата</strong><span>${escapeHtml(entry[0])}</span></div>
        <div class="detail-item"><strong>Событие</strong><span>${escapeHtml(entry[1])}</span></div>
        ${entry[2]?`<div class="detail-item"><strong>Результат</strong><span>${escapeHtml(entry[2])}</span></div>`:''}
      </div>
      <div class="modal-actions"><button class="btn btn-primary" id="history-detail-close">Закрыть</button></div>
    </div></div>`;
    document.getElementById('history-detail-close').onclick=()=>modalRoot.innerHTML='';
  }


  // avoid orphan proto controls after full rerender
  const originalRender=render;
  const observer=new MutationObserver(()=>{
    document.querySelectorAll('.proto-panel').forEach((el,i)=>{if(i>0)el.remove()});
  });
  observer.observe(document.body,{childList:true});

  render();
})();
