#pragma once

static const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<title>LiPo Monitor</title>
<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>
<style>
body { font-family: 'Segoe UI', sans-serif; background: #f4f4f9; margin: 0 auto; max-width: 1000px; padding: 20px; color: #333; }
h2 { text-align: center; }
.tabs { text-align: center; margin-bottom: 20px; }
.tabs button {
  background-color: #2196F3;
  border: none;
  color: white;
  padding: 10px 20px;
  margin: 0 5px;
  border-radius: 6px;
  cursor: pointer;
  transition: background-color 0.3s;
}
.tabs button:hover { background-color: #1565c0; }
.tabs button.active { background-color: #0d47a1; }
.pane { display: none; }
.pane.active { display: block; }
.cards { display: flex; gap: 12px; flex-wrap: wrap; justify-content: center; }
.card { background: white; border-radius: 10px; padding: 12px; width: 140px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); text-align: center; cursor: pointer; transition: transform 0.2s; }
.card:hover { transform: scale(1.05); }
.battery { width: 50px; height: 100px; border: 3px solid #333; border-radius: 6px; position: relative; margin: 8px auto; background: linear-gradient(to top, var(--batt-color) var(--level), #ddd var(--level)); }
.battery::before { content: ""; position: absolute; top: -10px; left: 15px; width: 20px; height: 6px; background: #333; border-radius: 2px; }
.total-battery { border-width: 4px; }
canvas { max-width: 100%; height: 180px; margin-top: 20px; }
a { display: inline-block; margin-top: 12px; color: #2196F3; text-decoration: none; }
a:hover { text-decoration: underline; }
.setup-box { background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 10px rgba(0,0,0,0.1); max-width: 500px; margin: auto; }
.setup-box h3 { margin-top: 0; }
.setup-box input { width: 60px; padding: 5px; margin: 5px; border: 1px solid #ccc; border-radius: 4px; }
.setup-box button { background-color: #2196F3; color: white; border: none; padding: 8px 16px; border-radius: 6px; margin: 8px 0; cursor: pointer; transition: background-color 0.3s; }
.setup-box button:hover { background-color: #0d47a1; }
</style>
</head>
<body>
<h2>Monitor de Baterias LiPo</h2>
<div class='tabs'>
 <button id='tabMon' class='active'>Monitor</button>
 <button id='tabSet'>Setup</button>
</div>
<div id='paneMon' class='pane active'>
  <div class='cards'>
    <div class='card' onclick="selectCell(0)">C1<div class='battery' id='batt0'></div><span id='v0'>-</span> V | <span id='soc0'>-</span>%</div>
    <div class='card' onclick="selectCell(1)">C2<div class='battery' id='batt1'></div><span id='v1'>-</span> V | <span id='soc1'>-</span>%</div>
    <div class='card' onclick="selectCell(2)">C3<div class='battery' id='batt2'></div><span id='v2'>-</span> V | <span id='soc2'>-</span>%</div>
    <div class='card' onclick="selectCell(3)">C4<div class='battery' id='batt3'></div><span id='v3'>-</span> V | <span id='soc3'>-</span>%</div>
    <div class='card' onclick="selectCell(4)">Total<div class='battery total-battery' id='battTot'></div><span id='tot'>-</span> V</div>
  </div>
  <canvas id='graph'></canvas>
  <a href='/download'>📥 Baixar CSV</a>
</div>
<div id='paneSet' class='pane'>
  <div class="setup-box">
    <h3>⚙️ Calibração kDiv</h3>
    <p>Digite tensões medidas (V):</p>
    <div>🔋 C1 <input id='in0'> V</div>
    <div>🔋 C2 <input id='in1'> V</div>
    <div>🔋 C3 <input id='in2'> V</div>
    <div>🔋 C4 <input id='in3'> V</div>
    <button onclick='calib()'>✅ Calibrar</button>
    <h3>🗑️ Logs</h3>
    <button onclick='clearLog()'>🧹 Limpar logs</button>
  </div>
</div>
<script>
const tabMon=document.getElementById('tabMon'),tabSet=document.getElementById('tabSet');
const paneMon=document.getElementById('paneMon'),paneSet=document.getElementById('paneSet');
function show(p){paneMon.classList.toggle('active',p==='mon');paneSet.classList.toggle('active',p==='set');tabMon.classList.toggle('active',p==='mon');tabSet.classList.toggle('active',p==='set');}
tabMon.onclick=()=>show('mon'); tabSet.onclick=()=>show('set');
let selectedCell = 0;
function selectCell(c){selectedCell = c; updateGraph();}
const ctx = graph.getContext('2d');
const chart = new Chart(ctx, {type:'line',data:{labels:[],datasets:[{label:'Célula',data:[],borderWidth:2,borderColor:'#2196F3',backgroundColor:'rgba(33, 150, 243, 0.2)',pointRadius:2}]},options:{animation:false,scales:{x:{display:false},y:{min:3,max:4.3}}}});
const hist=[[],[],[],[],[]]; const labels=[]; const cells=4;
let ws=new WebSocket('ws://'+location.hostname+'/ws');
ws.onmessage=e=>{
    const d=JSON.parse(e.data); 
    const now = new Date().toLocaleTimeString(); 
    labels.push(now); 
    if(labels.length>60) labels.shift(); 
    d.v.forEach((mv,i)=>{
        const v=mv/1000; 
        document.getElementById('v'+i).textContent = v.toFixed(3); 
        const soc = d.soc[i];
        document.getElementById('soc'+i).textContent = soc;
        updateBattery('batt'+i, v, soc);
        hist[i].push(v); 
        if(hist[i].length>60) hist[i].shift();
    }); 
    const total=d.tot/1000; 
    document.getElementById('tot').textContent=total.toFixed(3); 
    updateBattery('battTot',total, null, true);
    hist[4].push(total); 
    if(hist[4].length>60) hist[4].shift(); 
    updateGraph();
};

function updateBattery(id, v, soc, isTotal=false){
    let p;
    if (isTotal) {
        const cells = 4;
        const min=3.2*cells, max=4.2*cells;
        p = Math.max(0, Math.min(1, (v-min)/(max-min)));
    } else {
        p = soc / 100.0;
    }
    const b=document.getElementById(id);
    b.style.setProperty('--level',(p*100)+'%');
    let c;
    const vpc=isTotal?v/4:v;
    if(vpc>=3.7)c='#4caf50';else if(vpc>=3.5)c='#ffc107';else c='#f44336';
    b.style.setProperty('--batt-color',c);
}
function updateGraph(){chart.data.labels=[...labels];const t=selectedCell===4,n=t?'Total':'C'+(selectedCell+1);chart.data.datasets[0].label=n+' (V)';chart.data.datasets[0].data=[...hist[selectedCell]];chart.options.scales.y=t?{min:3*cells,max:4.2*cells}:{min:3,max:4.3};chart.update();}
function calib(){const v=[0,1,2,3].map(i=>parseFloat(document.getElementById('in'+i).value)*1000||0);const p=JSON.stringify({v});fetch('/api/calibrate',{method:'POST',headers:{'Content-Type':'application/json'},body:p}).then(r=>r.text()).then(t=>alert("Resposta: "+t)).catch(e=>alert("Erro: "+e));}
function clearLog(){fetch('/api/clear_logs',{method:'POST'});}
</script>
</body>
</html>
)rawliteral";
