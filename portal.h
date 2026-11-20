#pragma once
const char INDEX_HTML[] PROGMEM = R"PORTAL(<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Stress Lab</title><style>
*{box-sizing:border-box}body{margin:auto;padding:16px;max-width:820px;background:#111c24;color:#edf6f8;font:16px system-ui,sans-serif}
h1{font-size:1.6rem}h2{font-size:1.1rem}section{margin:12px 0;padding:16px;background:#20313d;border-radius:10px}
button,input,select{font:inherit;padding:8px;border-radius:6px;border:1px solid #79909c}button{cursor:pointer}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:10px 0}.start,.stop{font-weight:bold;color:white;padding:12px}
.start{background:#086448}.stop{background:#9a3340}input[type=number]{width:6em}
canvas{display:block;width:100%;height:180px;background:#12222b;margin:10px 0}output{display:block}small{color:#bbced7}
#lanCard{padding:14px;border:1px solid #587083;border-radius:8px;margin-top:12px}
#lanCard.connected{background:#103d32;border:2px solid #65dbac}
#localLink{display:block;color:#a4ffdc;font-size:1.5rem;font-weight:bold;overflow-wrap:anywhere}
#lanTop{color:#a4ffdc;font-weight:bold}#localLink[hidden]{display:none}
body{max-width:1020px;background:#0d1723;color:#f0f5fa;font-family:"Instrument Sans",system-ui,sans-serif}
h1{font-size:clamp(1.7rem,4vw,2.5rem);letter-spacing:-.045em;margin:14px 0}
section{background:#172534;border:1px solid #304454;padding:clamp(14px,3vw,26px)}
h2{font-size:1.4rem;letter-spacing:-.025em;margin:0 0 14px}
p{line-height:1.55}summary{cursor:pointer}select,input,button{max-width:100%}
#live{border-top:3px solid #67dcc9}#live header{display:flex;align-items:baseline;justify-content:space-between;gap:8px;flex-wrap:wrap}
#transport,#range{color:#b4cad8;font-size:.85rem}.signals{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;margin:8px 0 18px}
.signals div{background:#101d29;padding:10px;border-radius:6px;border-top:2px solid #64dec9;font-size:.86rem}
.signals div:nth-child(2){border-color:#73baff}.signals div:nth-child(3){border-color:#deabff}
.signals strong{display:block;font-size:clamp(.95rem,3vw,1.65rem);font-variant-numeric:tabular-nums;margin-top:6px}
#current{font-size:2rem;font-variant-numeric:tabular-nums}#unit{color:#b4cad8}
#graph{height:clamp(215px,29vw,300px);margin:12px 0;background:#101d29}#range{display:block}
#report{line-height:1.5}#state{color:#90e7cd}
</style></head><body><h1>ESP32 Stress Lab</h1>
<a id="lanTop" hidden></a>
<section><form id="test" method="post" action="/start">
<label>Duración (minutos) <input id="minutes" name="minutes" type="number" min="0" max="43200" required value="30"></label>
<div class="row" id="presets" hidden></div>
<small>0 minutos permite dejar la prueba sin límite.</small>
<div class="row"><button class="start">INICIAR PRUEBA</button>
<button class="stop" formaction="/stop" formnovalidate>DETENER</button></div>
<div class="row"><label>Carga máxima (%) <input type="number" name="peak" min="0" max="100" value="100" required></label>
<label>Patrón <select name="pattern" id="pattern"><option value="guided">Benchmark de alimentación</option><option value="pulse">Pulsante</option><option value="constant" selected>Constante</option><option value="ramp">Rampa</option></select></label></div>
<div class="row"><label>Período (s) <input type="number" name="period" min="1" max="3600" value="5" required></label>
<label>Núcleos <select name="cores" id="cores"><option value="auto" selected>Todos los disponibles</option><option>1</option><option>2</option></select></label>
<label><input type="checkbox" checked name="wifi"> Generar tráfico Wi-Fi</label>
<label><input type="checkbox" name="serial"> Tráfico serie interno (UART1)</label></div></form>
<output id="message" role="alert"></output><strong id="state">Consultando estado…</strong><div id="times"></div></section>
<section id="live"><header><h2>Telemetría en vivo</h2><span id="transport"></span></header><div class="signals"><div>CPU total<strong id="cpuStat">—</strong><small>iter/s</small></div><div>Wi-Fi · UDP<strong id="wifiStat">—</strong><small>datagramas/s</small></div><div>Serie · UART<strong id="uartStat">—</strong><small>ecos/s</small></div></div><div class="row"><label>Señal <select id="metric">
<option value="rate0">Núcleo 1 (iteraciones/s)</option><option value="rate1">Núcleo 2 (iteraciones/s)</option>
<option value="load">Carga solicitada (%)</option><option value="temperature">Temperatura interna (°C)</option>
<option value="serialRate">Ecos serie verificados (tramas/s)</option><option value="udpRate">Envíos UDP locales (datagramas/s)</option><option value="lag">Retraso de muestreo (ms)</option></select></label><label>Escala <select id="scale"><option value="detail">Detalle automático</option><option value="zero">Desde cero</option></select></label></div>
<div id="reading"><strong id="current">—</strong> <span id="unit"></span></div><small id="range"></small><canvas aria-label="Evolución de la señal seleccionada" id="graph" width="720" height="180"></canvas><small>Últimos 120 s · medición interna cada segundo · los huecos indican falta de muestras</small>
<p id="values"></p><small id="details"></small><p><button id="csv">Descargar CSV</button></p></section>
<section id="informe"><h2>Informe automático</h2><p>Al iniciar la prueba aparece un informe provisional. Al terminar el tiempo, la placa guarda el resultado y el informe final aparece aquí. Si pulsas DETENER, verás un informe parcial.</p><pre id="report" style="white-space:pre-wrap;font:inherit">Consultando el último ensayo…</pre>
<p><a style="color:#91dfef" href="/report?download=1">Descargar informe de estabilidad (.txt)</a></p>
<form method="post" action="/reference"><button>Guardar ensayo terminado como referencia</button></form>
<p><small>Usa este botón después de probar tu fuente conocida. El siguiente ensayo completo con los mismos ajustes mostrará la diferencia en el informe. Guardar otra referencia reemplaza la anterior.</small></p>
<details><summary>Cómo interpretar los resultados</summary>
<p><b>Ensayo completo.</b> Comprueba que alcanzó la duración prevista. En la fase sostenida compara las iteraciones/s y su variación entre repeticiones. En la fase pulsante es normal que el rendimiento caiga al entrar en reposo.</p>
<p><b>Retrasos.</b> Cada muestra está prevista un segundo después de la anterior. Un exceso de 100 ms queda marcado. La atención de peticiones web y el Wi-Fi también pueden introducir retrasos. Busca un cambio reproducible frente a la referencia.</p>
<p><b>Temperatura y memoria.</b> La temperatura corresponde al chip del S3. Observa si el calentamiento coincide con degradación del rendimiento. La memoria mínima refleja el margen de RAM durante el ensayo.</p>
<p><b>Reinicio.</b> BROWNOUT indica que se activó el detector interno de baja tensión. El registro identifica el evento, pero se desconoce la magnitud de la caída. POWER_ON también puede aparecer tras una desconexión manual. Si el fallo se repite solo con el step-up, anota en qué fase ocurre y compara con la fuente de referencia.</p></details></section>
<section><h2>Cómo probar el step-up</h2>
<p>Para evaluar estabilidad prolongada usa el perfil predeterminado: carga constante al 100 %, todos los núcleos y radio activa. Para estudiar cambios de demanda elige el <b>benchmark de alimentación</b> de 30 minutos al 100 %. Ejecuta primero con una fuente conocida y guarda el informe. Repite con el step-up cambiando solo la alimentación. Para evaluar un conector, conserva la fuente y cambia solo ese conector.</p>
<p>En 30 minutos la placa pasa <b>3 min en reposo</b>, <b>12 min alternando carga y reposo</b> y <b>15 min bajo carga sostenida</b>. Así puedes distinguir una falla al cambiar la demanda de otra que aparece después de calentarse. Con otra duración se conservan las proporciones.</p>
<p>El step-up solo alimenta la placa. Deja libres los pines de medición. El ESP32 observa su propio rendimiento, los retrasos, la memoria y los reinicios. En el S3 también lee la temperatura interna del chip. Si el USB también suministra energía, puede ocultar un fallo del convertidor.</p>
<details><summary>Qué cambia cada control</summary><ul>
<li><b>Duración.</b> El temporizador funciona en la placa aunque cierres esta página. Para ejecución continua usa 0 con un patrón manual. Iniciar otra prueba reemplaza el informe anterior.</li>
<li><b>Carga máxima y núcleos.</b> El porcentaje fija la fracción de tiempo que trabaja cada núcleo seleccionado. Con dos núcleos hay dos tareas en paralelo. Conserva estos ajustes al comparar fuentes.</li>
<li><b>Pulsante.</b> Con período de 5 s la placa trabaja 2,5 s y descansa 2,5 s. Las transiciones repetidas ayudan a reproducir fallos intermitentes.</li>
<li><b>Constante.</b> Mantiene el nivel elegido. Úsalo para prolongar la fase en que aparece calentamiento o pérdida de rendimiento.</li>
<li><b>Rampa.</b> Sube de cero al máximo durante cada período y vuelve a empezar. Ayuda a buscar una zona de carga problemática.</li>
<li><b>Tráfico serie interno.</b> UART1 se envía tramas de 64 bytes a 460800 baudios y verifica el contenido recibido. El informe cuenta los errores y el peor tiempo hasta atender el eco. Actívalo para comparar CPU y Wi-Fi con y sin interrupciones de la UART.</li>
<li><b>Tráfico Wi-Fi.</b> Añade envíos UDP por la red del punto de acceso durante toda la prueba, incluso en reposo de CPU. Desmarca la casilla para desactivar los envíos de estrés. Para comparar, usa la misma configuración de radio, red y clientes.</li></ul></details></section>


<section id="red"><h2>Conexión al panel</h2>
<p><b>Directa (AP).</b> Conéctate a ESP32-Power-Test (red abierta, sin clave) y abre http://192.168.4.1/. Esta red sigue disponible durante el ensayo.</p>
<p><b>Desde tu router.</b> La clave que escribas se enviará por HTTP sobre el AP abierto. Configura la conexión en un entorno de confianza. Estando conectado al AP, introduce una red Wi-Fi personal de 2,4 GHz. Cuando aparezca la dirección local, conecta tu computador o teléfono a esa misma red y abre el enlace. Cualquier equipo con acceso a la placa puede controlar la prueba. Usa una red de confianza.</p>
<form id="networkForm" method="post" action="/network"><div class="row"><label>Nombre de red (SSID) <input name="ssid" maxlength="32" required></label>
<label>Clave Wi-Fi <input type="password" name="key" maxlength="63" autocomplete="off"></label>
<button id="joinButton">Conectar a la red local</button></div></form>
<small>Deja la clave vacía para una red abierta. La configuración dura hasta reiniciar la placa.</small>
<div id="lanCard"><strong id="netState" role="status">Consultando la conexión…</strong>
<a id="localLink" hidden></a><small id="lanHint" hidden>Abre esta dirección desde un equipo conectado al mismo router.</small></div>
<output id="joinError" role="alert"></output></section>
<script>
const el=id=>document.getElementById(id);
const history=[], rows=[];
const keys=['fecha','estado','transcurrido_s','load','rate0','rate1','temperature','lag','heap','udpRate','serialRate'];
for(const n of [1,5,10,30,60,0]){
  const b=document.createElement('button'); b.type='button'; b.textContent=n?n+' min':'Sin límite';
  b.onclick=()=>el('minutes').value=n; el('presets').appendChild(b);
}
el('presets').hidden=false;
function validateMode(){
  el('minutes').setCustomValidity(el('pattern').value==='guided' && el('minutes').value==='0'
    ? 'El benchmark necesita una duración. Elige al menos 1 minuto o selecciona un patrón manual.' : '');
}
el('presets').addEventListener('click',validateMode);
el('minutes').oninput=el('pattern').onchange=validateMode;
let nextDetails=0, lastState='';
async function refreshDetails(){
  el('report').textContent=await readTelemetry('/report','text');
}
async function readTelemetry(path,format){
  const controller=new AbortController();
  const timeout=setTimeout(()=>controller.abort(),3500);
  try{
    const r=await fetch(path,{cache:'no-store',signal:controller.signal});
    if(!r.ok)throw Error();
    return await r[format]();
  }finally{clearTimeout(timeout);}
}
let netTimer, netGeneration=0;
function showNetwork(state,ip='',ssid=''){
  const url=ip?'http://'+ip+'/':'';
  const label=ip?(ssid?'Conectado a «'+ssid+'»':'Conectado a la red local'):state;
  if(el('netState').textContent!==label)el('netState').textContent=label;
  el('lanCard').classList.toggle('connected',!!ip);
  for(const id of ['localLink','lanTop']){
    const link=el(id);
    link.hidden=!ip;
    if(ip){link.href=url;link.textContent=id==='lanTop'?label+' · '+ip:url;}
    else link.removeAttribute('href');
  }
  el('lanHint').hidden=!ip;
}
async function networkRequest(options){
  const controller=new AbortController(), timeout=setTimeout(()=>controller.abort(),4500);
  try{return await fetch('/api/network',{...options,cache:'no-store',signal:controller.signal});}
  finally{clearTimeout(timeout);}
}
async function pollNetwork(generation){
  let delay=1000;
  try{
    const r=await networkRequest({});if(!r.ok)throw Error();
    const n=await r.json();if(generation!==netGeneration)return;
    showNetwork(n.state,n.ip,n.ssid);
    if(n.state!=='Conectando')delay=5000;
  }catch(e){
    if(generation===netGeneration)showNetwork('Esperando respuesta del ESP32…');
  }finally{
    if(generation===netGeneration)netTimer=setTimeout(()=>pollNetwork(generation),delay);
  }
}
el('networkForm').onsubmit=async e=>{
  e.preventDefault();
  const form=e.currentTarget, generation=++netGeneration;
  clearTimeout(netTimer);el('joinButton').disabled=true;el('joinError').textContent='';
  showNetwork('Conectando al router…');
  try{
    const r=await networkRequest({method:'POST',body:new URLSearchParams(new FormData(form))});
    if(r.ok)form.elements.key.value='';
    else el('joinError').textContent=await r.text();
  }catch(e){showNetwork('Comprobando si el ESP32 recibió la solicitud…');}
  finally{el('joinButton').disabled=false;pollNetwork(generation);}
};
pollNetwork(netGeneration);
const signals={
  rate0:['iter/s','#64dec9'],rate1:['iter/s','#a8eab8'],load:['%','#64dec9'],
  temperature:['°C','#ffbd77'],serialRate:['ecos/s','#deabff'],udpRate:['datagramas/s','#73baff'],lag:['ms','#ffbd77']
};
const fmt=new Intl.NumberFormat('es-CL',{maximumFractionDigits:2});
function draw(){
  const c=el('graph'),g=c.getContext('2d'),k=el('metric').value,[unit,color]=signals[k];
  const w=Math.max(240,c.clientWidth),h=c.clientHeight||230,dpr=Math.min(window.devicePixelRatio||1,2);
  if(c.width!==Math.round(w*dpr)||c.height!==Math.round(h*dpr)){c.width=Math.round(w*dpr);c.height=Math.round(h*dpr);}
  g.setTransform(dpr,0,0,dpr,0,0);g.clearRect(0,0,w,h);
  const now=performance.now(),samples=history.filter(p=>now-p.t<=120000);
  const vals=samples.map(p=>p[k]).filter(Number.isFinite),last=samples.length?samples[samples.length-1][k]:null;
  el('current').textContent=Number.isFinite(last)?fmt.format(last):'—';el('unit').textContent=unit;
  g.fillStyle='#b4cad8';g.font='13px system-ui';
  if(!vals.length){el('range').textContent='Sin lecturas de esta señal';g.fillText('Esperando datos',16,h/2);return;}
  const low=Math.min(...vals),high=Math.max(...vals);
  el('range').textContent='Mínimo '+fmt.format(low)+' · Máximo '+fmt.format(high)+' '+unit+' · '+vals.length+' muestras';
  const pad=Math.max((high-low)*.12,unit==='°C'?.1:.5);
  const detail=el('scale').value==='detail';
  const lo=Math.min(detail?low-pad:0,low),bottom=k==='temperature'?lo:Math.max(0,lo);
  const top=Math.max(bottom+pad,high+pad,!detail&&k==='lag'?100:-Infinity);
  const l=80,r=w-14,t=16,b=h-30;
  const span=Math.min(120000,Math.max(10000,now-samples[0].t));
  const x=p=>r-(now-p.t)*(r-l)/span,y=v=>b-(v-bottom)*(b-t)/(top-bottom);
  g.textAlign='right';
  for(let i=0;i<=4;i++){
    const value=bottom+(top-bottom)*i/4,row=y(value);
    g.strokeStyle='#304456';g.beginPath();g.moveTo(l,row);g.lineTo(r,row);g.stroke();
    g.fillText(fmt.format(value),l-7,row+4);
  }
  g.textAlign='left';g.fillText('−'+Math.round(span/1000)+' s',l,h-8);
  g.textAlign='right';g.fillText('ahora',r,h-8);
  if(k==='lag'&&bottom<=100&&top>=100){
    g.strokeStyle='#ff9955';g.setLineDash([5,4]);g.beginPath();g.moveTo(l,y(100));g.lineTo(r,y(100));g.stroke();g.setLineDash([]);
  }
  g.strokeStyle=g.fillStyle=color;g.lineWidth=2;let prev=null;
  for(const p of samples){
    if(!Number.isFinite(p[k])){prev=null;continue;}
    if(prev && p.t-prev.t<2500){g.beginPath();g.moveTo(x(prev),y(prev[k]));g.lineTo(x(p),y(p[k]));g.stroke();}
    g.beginPath();g.arc(x(p),y(p[k]),2,0,Math.PI*2);g.fill();prev=p;
  }
  g.lineWidth=1;
}
el('metric').onchange=el('scale').onchange=draw;
window.addEventListener('resize',draw);
function duration(s){return Math.floor(s/3600)+' h '+Math.floor(s/60)%60+' min '+s%60+' s';}
let lastSampleId=-1, lastPush=-Infinity, lastNewSample=-Infinity, socket, pingAt=0, detailsBusy=false;
function showStatus(s){
    const temp=s.temperature===null?'No disponible':s.temperature+' °C';
    el('state').textContent=s.state+(s.phase?' · '+s.phase:'');
    el('times').textContent='Transcurrido: '+duration(s.elapsed)+(s.running?' · Restante: '+(s.minutes?duration(s.minutes*60-s.elapsed):'sin límite'):'');
    el('values').textContent='Carga: '+s.load+' % · Núcleos: '+s.rates.slice(0,s.workers).join(' / ')+' iter/s · Temperatura: '+temp;
    el('details').textContent='Retraso: '+s.lag+' ms · Memoria libre: '+s.heap+' B · Último reinicio: '+s.reset;
    el('cores').options[2].disabled=s.workers<2;
    const p={...s,rate0:s.rates[0],rate1:s.workers>1?s.rates[1]:null,fecha:new Date().toISOString(),estado:s.state,transcurrido_s:s.elapsed};
    if(s.sample_ms!==lastSampleId){
    el('cpuStat').textContent=fmt.format(s.rates[0]+s.rates[1]);
    el('wifiStat').textContent=fmt.format(s.udpRate);
    el('uartStat').textContent=fmt.format(s.serialRate);
    lastSampleId=s.sample_ms;
    p.t=performance.now();
    lastNewSample=p.t;
    history.push(p);while(history.length && history[0].t<p.t-120000)history.shift();
    rows.push(keys.map(k=>p[k]).join(',')); if(rows.length>3600)rows.shift();
    draw();
    }
    if(!detailsBusy && (s.state!==lastState || Date.now()>=nextDetails)){
      detailsBusy=true;nextDetails=Date.now()+5000;lastState=s.state;
      refreshDetails().catch(()=>el('report').textContent='Esperando el informe de la placa…')
        .finally(()=>detailsBusy=false);
    }
}
function connectLive(){
  socket=new WebSocket('ws://'+location.hostname+':81/', 'arduino');
  socket.onmessage=e=>{
    const now=performance.now();
    if(e.data==='pong'){
      if(pingAt)el('transport').textContent='WebSocket · ida y vuelta navegador–ESP32: '+Math.round(now-pingAt)+' ms';
      pingAt=0;return;
    }
    showStatus(JSON.parse(e.data));lastPush=now;
    if(!pingAt && now-lastPing>3000){lastPing=pingAt=now;socket.send('ping');}
  };
  socket.onopen=()=>el('transport').textContent='WebSocket conectado';
  socket.onclose=()=>{pingAt=0;lastPush=-Infinity;setTimeout(connectLive,3000);};
}
let lastPing=-Infinity;
async function poll(){
  if(performance.now()-lastPush>1500 || performance.now()-lastNewSample>3500){
    if(lastSampleId!==-1 && performance.now()-lastNewSample>3500){
      draw();
      for(const id of ['current','cpuStat','wifiStat','uartStat'])el(id).textContent='—';
      el('transport').textContent='Sin muestras nuevas · recuperando la telemetría…';
    }
    try{
      const s=await readTelemetry('/api/status','json');
      if(performance.now()-lastPush>1500 || performance.now()-lastNewSample>3500){
        showStatus(s);
        el('transport').textContent=performance.now()-lastNewSample>3500?'ESP32 conectado · esperando una muestra nueva':'Actualización por HTTP';
      }
    }catch(e){if(performance.now()-lastPush>1500)el('state').textContent='Sin conexión al ESP32';}
  }
  setTimeout(poll,1000);
}
el('test').onsubmit=async e=>{
  if(!e.submitter)return;
  e.preventDefault();const form=e.currentTarget;
  try{
    const r=await fetch(e.submitter.getAttribute('formaction')||form.action,{method:'POST',body:new URLSearchParams(new FormData(form))});
    if(!r.ok)throw Error(await r.text());
    el('message').textContent='';
  }catch(err){el('message').textContent='Error al enviar: '+err.message;}
};
el('csv').onclick=()=>{
  const url=URL.createObjectURL(new Blob([[keys.join(','),...rows].join('\r\n')],{type:'text/csv'}));
  const a=document.createElement('a');a.href=url;a.download='esp32-stress.csv';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
};
if(window.WebSocket)connectLive();
poll();
</script></body></html>)PORTAL";

