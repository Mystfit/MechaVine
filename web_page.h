#ifndef MECHAVINE_WEB_PAGE_H
#define MECHAVINE_WEB_PAGE_H

const char WEB_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>MechaVine Calibration</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,sans-serif;background:#1a1a2e;color:#e0e0e0;padding:12px;max-width:600px;margin:0 auto}
h1{text-align:center;color:#a0d2db;margin:8px 0 16px;font-size:1.4em}
canvas{width:100%;background:#0f0f23;border:1px solid #333;border-radius:8px;margin-bottom:16px}
.ctrl{background:#16213e;border-radius:8px;padding:12px;margin-bottom:10px}
.ctrl label{display:block;font-size:0.85em;color:#a0d2db;margin-bottom:6px}
.ctrl .row{display:flex;align-items:center;gap:10px}
.ctrl input[type=range]{flex:1;accent-color:#a0d2db}
.ctrl input[type=number]{width:90px;background:#0f0f23;border:1px solid #444;color:#e0e0e0;border-radius:4px;padding:4px 6px;font-size:0.95em;text-align:right}
.ctrl input[type=number]::-webkit-inner-spin-button{opacity:1}
#status{text-align:center;padding:8px;border-radius:6px;font-size:0.85em;margin-top:4px}
.connected{background:#1b4332;color:#95d5b2}
.disconnected{background:#3d0000;color:#ff6b6b}
.state-bar{text-align:center;font-size:1.1em;margin-bottom:12px;padding:6px;background:#16213e;border-radius:6px}
</style>
</head>
<body>
<h1>MechaVine Calibration</h1>
<div id="status" class="disconnected">Disconnected</div>
<div class="state-bar">State: <span id="flowerState">--</span></div>
<canvas id="graph" height="200"></canvas>

<div class="ctrl">
<label>Spike Threshold (fast RMS)</label>
<div class="row">
<input type="range" id="threshold_r" min="50000" max="2000000" step="10000">
<input type="number" id="threshold_n" min="50000" max="2000000" step="10000">
</div>
</div>

<div class="ctrl">
<label>Sustained Threshold (slow average)</label>
<div class="row">
<input type="range" id="sustainedThreshold_r" min="10000" max="1000000" step="10000">
<input type="number" id="sustainedThreshold_n" min="10000" max="1000000" step="10000">
</div>
</div>

<div class="ctrl">
<label>Quiet Delay (ms)</label>
<div class="row">
<input type="range" id="quietDelay_r" min="500" max="10000" step="100">
<input type="number" id="quietDelay_n" min="500" max="10000" step="100">
</div>
</div>

<div class="ctrl">
<label>Servo Speed — Opening (deg/s)</label>
<div class="row">
<input type="range" id="speedOpen_r" min="10" max="200" step="5">
<input type="number" id="speedOpen_n" min="10" max="200" step="5">
</div>
</div>

<div class="ctrl">
<label>Servo Speed — Closing (deg/s)</label>
<div class="row">
<input type="range" id="speedClose_r" min="10" max="200" step="5">
<input type="number" id="speedClose_n" min="10" max="200" step="5">
</div>
</div>

<script>
var ws, rmsData=[], slowRmsData=[], maxPts=200, canvas=document.getElementById('graph'), ctx=canvas.getContext('2d');
var sendTimer=null, pendingSends={};

function resize(){canvas.width=canvas.clientWidth;canvas.height=200}
window.addEventListener('resize',resize);resize();

function connect(){
  ws=new WebSocket('ws://'+location.hostname+':81/');
  ws.onopen=function(){
    document.getElementById('status').textContent='Connected';
    document.getElementById('status').className='connected';
  };
  ws.onclose=function(){
    document.getElementById('status').textContent='Disconnected';
    document.getElementById('status').className='disconnected';
    setTimeout(connect,2000);
  };
  ws.onmessage=function(e){
    var d=JSON.parse(e.data);
    if(d.rms!==undefined){
      rmsData.push(d.rms);
      if(rmsData.length>maxPts)rmsData.shift();
      if(d.slowRms!==undefined){slowRmsData.push(d.slowRms);if(slowRmsData.length>maxPts)slowRmsData.shift();}
      if(d.state)document.getElementById('flowerState').textContent=d.state;
      drawGraph();
    }
    if(d.cfg){
      setCtrl('threshold',d.cfg.threshold);
      setCtrl('sustainedThreshold',d.cfg.sustainedThreshold);
      setCtrl('quietDelay',d.cfg.quietDelay);
      setCtrl('speedOpen',d.cfg.speedOpen);
      setCtrl('speedClose',d.cfg.speedClose);
    }
  };
}
connect();

function setCtrl(name,val){
  document.getElementById(name+'_r').value=val;
  document.getElementById(name+'_n').value=val;
}

function drawGraph(){
  var w=canvas.width,h=canvas.height;
  var thresh=parseInt(document.getElementById('threshold_r').value);
  var susThresh=parseInt(document.getElementById('sustainedThreshold_r').value);
  var maxRms=Math.max(thresh*2,100000);
  for(var i=0;i<rmsData.length;i++){if(rmsData[i]>maxRms)maxRms=rmsData[i]*1.2}
  ctx.clearRect(0,0,w,h);
  // grid
  ctx.strokeStyle='#222';ctx.lineWidth=1;
  for(var i=1;i<5;i++){var y=h*i/5;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(w,y);ctx.stroke()}
  ctx.setLineDash([6,4]);
  // spike threshold line (red)
  var ty=h-(thresh/maxRms)*h;
  ctx.strokeStyle='#ff4444';ctx.lineWidth=2;
  ctx.beginPath();ctx.moveTo(0,ty);ctx.lineTo(w,ty);ctx.stroke();
  ctx.fillStyle='#ff4444';ctx.font='11px sans-serif';ctx.fillText('Spike: '+thresh,4,ty-4);
  // sustained threshold line (orange)
  var sy=h-(susThresh/maxRms)*h;
  ctx.strokeStyle='#ffaa00';ctx.lineWidth=2;
  ctx.beginPath();ctx.moveTo(0,sy);ctx.lineTo(w,sy);ctx.stroke();
  ctx.fillStyle='#ffaa00';ctx.fillText('Sustained: '+susThresh,4,sy-4);
  ctx.setLineDash([]);
  var step=w/(maxPts-1);
  // slow RMS line (orange, drawn under fast RMS)
  if(slowRmsData.length>=2){
    ctx.strokeStyle='#cc7700';ctx.lineWidth=2;ctx.beginPath();
    var offset=maxPts-slowRmsData.length;
    for(var i=0;i<slowRmsData.length;i++){
      var x=(offset+i)*step;var y=h-(slowRmsData[i]/maxRms)*h;
      if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
    }
    ctx.stroke();
  }
  // fast RMS line (teal)
  if(rmsData.length<2)return;
  ctx.strokeStyle='#4ecdc4';ctx.lineWidth=2;ctx.beginPath();
  var offset=maxPts-rmsData.length;
  for(var i=0;i<rmsData.length;i++){
    var x=(offset+i)*step;var y=h-(rmsData[i]/maxRms)*h;
    if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
  }
  ctx.stroke();
}

function bindCtrl(name,setKey){
  var r=document.getElementById(name+'_r'),n=document.getElementById(name+'_n');
  r.addEventListener('input',function(){n.value=r.value;queueSend(setKey,parseInt(r.value))});
  n.addEventListener('change',function(){r.value=n.value;queueSend(setKey,parseInt(n.value))});
}
bindCtrl('threshold','threshold');
bindCtrl('sustainedThreshold','sustainedThreshold');
bindCtrl('quietDelay','quietDelay');
bindCtrl('speedOpen','speedOpen');
bindCtrl('speedClose','speedClose');

function queueSend(key,val){
  pendingSends[key]=val;
  if(sendTimer)clearTimeout(sendTimer);
  sendTimer=setTimeout(flushSends,200);
}
function flushSends(){
  sendTimer=null;
  for(var k in pendingSends){
    if(ws&&ws.readyState===1)ws.send(JSON.stringify({set:k,val:pendingSends[k]}));
  }
  pendingSends={};
}
</script>
</body>
</html>)rawliteral";

#endif
