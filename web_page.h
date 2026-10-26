// esp32-S3-ws4-caravan v1.0
#pragma once
/* The web page: one fixed HTML file that fetches /json every 30 seconds and draws
 * itself in the browser. The board only builds a few kB of JSON per update, and
 * the page adapts to any screen: one column on a phone, more on a tablet or PC.
 * Sections appear only for what the board has (relays, Shelly, NMEA 2000 ...),
 * so the same page serves the caravan and the boat display. */

static const char WEB_PAGE[] PROGMEM = R"PAGE(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#111418">
<link rel="icon" href="data:,">
<title>Display</title>
<style>
:root{--bg:#111418;--card:#1b1f25;--card2:#242a32;--line:#2e353f;--text:#e6e8eb;--dim:#9aa3ad;
--blue:#38a9f5;--solar:#f5b32b;--red:#e74c3c;--green:#2ecc71;--amber:#f39c12;--load:#e8604c}
*{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
header{display:flex;align-items:center;gap:12px;flex-wrap:wrap;padding:14px 16px 6px;max-width:1240px;margin:0 auto}
header h1{font-size:20px;margin:0;flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.clock{text-align:right;line-height:1.1;margin-left:auto}.clock b{font-size:26px;font-variant-numeric:tabular-nums}
.clock div{color:var(--dim);font-size:12px}
.upd{font:inherit;font-size:12px;padding:3px 10px;border-radius:99px;border:1px solid var(--line);background:var(--card2);color:var(--text);cursor:pointer;white-space:nowrap}
.pill{font-size:12px;padding:3px 9px;border-radius:99px;background:var(--card2);color:var(--dim);white-space:nowrap}
.pill.ok::before{content:"";display:inline-block;width:7px;height:7px;border-radius:50%;background:var(--green);margin-right:6px;vertical-align:1px}
.pill.bad{background:#5a3b10;color:#ffd08a}
main{max-width:1240px;margin:0 auto;padding:6px 16px 24px}
.grid{display:grid;gap:12px;grid-template-columns:repeat(auto-fill,minmax(min(100%,310px),1fr));grid-auto-flow:row dense;align-items:start}
.card{background:var(--card);border-radius:14px;padding:14px 16px;min-width:0}
.wide{grid-column:1/-1}
h2{font-size:13px;font-weight:600;letter-spacing:.04em;text-transform:uppercase;color:var(--dim);margin:0 0 10px}
.alarm{padding:11px 14px;border-radius:12px;margin:0 0 10px;font-weight:600}
.lvl2{background:#9f2a1e}.lvl1{background:#8a5a0b}
.big{font-size:44px;font-weight:700;line-height:1;font-variant-numeric:tabular-nums}
.mid{font-size:24px;font-weight:600;font-variant-numeric:tabular-nums}
.sub{color:var(--dim);font-size:13px}
.bar{height:10px;border-radius:5px;background:var(--card2);overflow:hidden;margin:10px 0 8px}
.bar i{display:block;height:100%;border-radius:5px}
.flex{display:flex;justify-content:space-between;align-items:baseline;gap:10px;flex-wrap:wrap}
.tiles{display:grid;gap:8px;grid-template-columns:repeat(auto-fill,minmax(104px,1fr))}
.tile{background:var(--card2);border-radius:10px;padding:8px 10px;min-width:0}
.tile .sub{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.row{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:9px 0;border-top:1px solid var(--line)}
.row:first-of-type{border-top:0}
.row>div:first-child{min-width:0}.row .r{text-align:right;flex-shrink:0}
.name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.chg{color:var(--solar);font-weight:600}.on{color:var(--green);font-weight:600}.off{color:var(--dim)}
.warn{color:var(--amber)}.alm{color:#ff6b5b}
svg{display:block;width:100%}
svg.c24{height:170px}svg.sp{height:46px;margin:4px 0}
.axis{display:flex;justify-content:space-between;font-size:11px;color:var(--dim);margin-top:3px}
.legend{display:flex;gap:14px;flex-wrap:wrap;font-size:12px;color:var(--dim);margin-top:6px}
.legend i{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:5px;vertical-align:-1px}
.fc{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:10px}
.fc>div{background:var(--card2);border-radius:10px;padding:8px;text-align:center;font-size:13px}
.seg{display:inline-flex;border-radius:9px;overflow:hidden;border:1px solid #2f6fb5}
.seg button{font:inherit;font-size:14px;padding:7px 14px;border:0;background:#0e2233;color:var(--text);cursor:pointer}
.seg button.act{background:#4a525c;color:#fff}.seg button.act[data-on="1"]{background:#2e9e4f}.seg button:disabled{opacity:.6;cursor:default}
.btn{font:inherit;font-size:13px;padding:6px 10px;margin-top:6px;border-radius:8px;border:1px solid var(--line);background:var(--card2);color:var(--text);cursor:pointer}
.tanks{display:grid;grid-template-columns:minmax(0,max-content) minmax(60px,1fr) auto;gap:10px 12px;align-items:center}
.tanks .bar{margin:0}
footer{max-width:1240px;margin:0 auto;padding:0 16px 28px}
/* the About card: like the others, but quieter */
.about{border:1px solid var(--line);border-radius:14px;padding:12px 16px;color:var(--dim);font-size:12px;line-height:1.6}
.about h2{font-size:11px;margin-bottom:8px}
.about dl{display:grid;grid-template-columns:max-content 1fr;gap:4px 14px;margin:0}
.about dt{color:var(--dim);opacity:.75}
.about dd{margin:0;min-width:0;overflow-wrap:anywhere}
.about a{color:var(--dim);text-decoration:underline;text-decoration-color:var(--line);text-underline-offset:2px}
.about a:hover{color:var(--blue)}
@media (max-width:420px){.about dl{grid-template-columns:1fr}.about dt{margin-top:6px}}
a{color:var(--blue)}
.empty{color:var(--dim);font-size:13px}
/* while the first answer is on its way: grey placeholder cards, or the last values dimmed */
.skel h2,.skel i{display:block;border-radius:7px;background:linear-gradient(90deg,var(--card2) 0%,#2f3640 50%,var(--card2) 100%);background-size:200% 100%;animation:sk 1.4s linear infinite}
.skel h2{width:40%;height:12px}.skel i{height:16px;margin:12px 0 0}
@keyframes sk{from{background-position:200% 0}to{background-position:-200% 0}}
@media (prefers-reduced-motion:reduce){.skel h2,.skel i{animation:none}}
body.stale main{opacity:.55}
main{transition:opacity .3s}
@media (max-width:420px){.big{font-size:38px}header h1{font-size:18px;flex-basis:100%}.clock b{font-size:22px}main{padding:6px 10px 20px}}
</style></head><body>
<header><h1 id="name">&nbsp;</h1><span class="pill" id="state">Connecting&hellip;</span><button class="upd" id="upd">&#8635;</button>
<div class="clock"><b id="time">--:--</b><div id="date"></div></div></header>
<main><div id="alarms"></div><div class="grid" id="grid">
<section class="card skel"><h2></h2><i style="width:55%;height:34px"></i><i></i></section>
<section class="card skel"><h2></h2><i style="width:45%;height:34px"></i><i style="width:80%"></i></section>
<section class="card skel"><h2></h2><i></i><i style="width:70%"></i><i style="width:85%"></i></section>
<section class="card skel"><h2></h2><i style="width:60%"></i><i></i><i style="width:75%"></i></section>
</div></main>
<footer id="foot"></footer>
<script>
"use strict";
const $=id=>document.getElementById(id);
const esc=s=>String(s==null?"":s).replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"})[c]);
const num=(v,d=0)=>v==null||isNaN(v)?"--":(+v).toFixed(d);
const card=(t,body,cls="")=>`<section class="card ${cls}"><h2>${t}</h2>${body}</section>`;
const lvl=l=>l==2?"alm":l==1?"warn":"";
/* Texts follow the display's language: /lang gives the translations of the page's
   texts: every T(...) call below with a quoted text (tools/i18n.py collects them into
   lang/<code>.po, web.cpp finds them for /lang). Each %s is filled in from the
   arguments after the text, in order. */
let TXT={},lang="en",langV="";
const T=(t,...a)=>{let i=0;return (TXT[t]||t).replace(/%s/g,()=>a[i++])};
async function setLang(l,v){
  if(l==lang&&v==langV)return;
  try{const r=await fetch("/lang",{cache:"no-store"});if(r.ok){TXT=await r.json();lang=l;langV=v;document.documentElement.lang=l;keep("lang",{l,v,TXT})}}catch(e){}
}
/* The last answer is kept in this browser, so the page can show it straight away
   (dimmed) while the board answers. Nothing breaks if the browser keeps nothing. */
const keep=(k,v)=>{try{localStorage.setItem("ws4."+k,JSON.stringify(v))}catch(e){}};
const kept=k=>{try{return JSON.parse(localStorage.getItem("ws4."+k))}catch(e){return null}};
let timer=0,busy=false,fails=0,lastOk=0,pvMax=400,data=null;

function schedule(ms){clearTimeout(timer);timer=setTimeout(load,ms)}
/* Every 30 s, and every 2 minutes once the page has been left alone for 2 minutes:
   each update is work for the board. Update, a switch or coming back to the tab
   makes it quick again. */
let active=Date.now();
const every=()=>Date.now()-active<120000?30000:120000;
function wake(){active=Date.now();schedule(0)}
async function load(){
  if(busy)return;
  if(document.hidden){schedule(10000);return}   // nobody is looking: don't load the board
  busy=true;
  const ac=new AbortController(),to=setTimeout(()=>ac.abort(),8000);
  try{
    const r=await fetch("/json",{signal:ac.signal,cache:"no-store"});
    if(r.status==401){location.href="/login";return}
    if(!r.ok)throw new Error(r.status);
    data=await r.json();fails=0;lastOk=Date.now();
    if(data.lang)await setLang(data.lang,data.version);
    render(data);document.body.classList.remove("stale");
    keep("data",{t:lastOk,d:data});
  }catch(e){fails++}
  finally{clearTimeout(to);busy=false;status();schedule(fails?Math.min(30000,4000*fails):every())}
}
function status(){
  const s=$("state");
  $("upd").textContent="↻ "+T("Update");
  if(!lastOk){s.className="pill"+(fails?" bad":"");s.textContent=fails?T("No answer, retrying"):T("Connecting…");return}
  const age=Math.round((Date.now()-lastOk)/1000);
  if(fails){s.className="pill bad";s.textContent=T("Offline · data %s old",age<90?age+" s":Math.round(age/60)+" min")}
  else{s.className="pill ok";s.textContent=T("Connected")}
}
document.addEventListener("visibilitychange",()=>{if(!document.hidden)wake()});
$("upd").addEventListener("click",wake);

/* ---- small charts as SVG ---- */
function bars24(h){
  const n=h.solar.length,W=480,H=150,B=H-20,bw=Math.max(3,Math.floor(W/n/2)-2);
  let max=1;for(let i=0;i<n;i++)max=Math.max(max,h.solar[i]||0,h.load[i]||0);
  let s=`<svg class="c24" viewBox="0 0 ${W} ${B}" preserveAspectRatio="none" role="img" aria-label="${T("Last 24 hours")}"><line x1="0" y1="${B}" x2="${W}" y2="${B}" stroke="#2e353f" vector-effect="non-scaling-stroke"/>`;
  const step=W/n;
  for(let i=0;i<n;i++){
    const x=i*step+step/2-bw;
    const a=(h.solar[i]||0)/max*(B-10),b=(h.load[i]||0)/max*(B-10);
    if(a>0)s+=`<rect x="${x.toFixed(1)}" y="${(B-a).toFixed(1)}" width="${bw}" height="${a.toFixed(1)}" rx="1" fill="var(--solar)"/>`;
    if(b>0)s+=`<rect x="${(x+bw+1).toFixed(1)}" y="${(B-b).toFixed(1)}" width="${bw}" height="${b.toFixed(1)}" rx="1" fill="var(--load)"/>`;
  }
  let p="";for(let i=0;i<n;i++)if(h.soc[i]!=null)p+=`${(i*step+step/2).toFixed(1)},${(B-h.soc[i]/100*(B-10)).toFixed(1)} `;
  if(p)s+=`<polyline points="${p}" fill="none" stroke="var(--blue)" stroke-width="2.5" stroke-linejoin="round" vector-effect="non-scaling-stroke"/>`;
  s+=`</svg><div class="axis"><span>-${n-1}h</span><span>-${Math.round((n-1)/2)}h</span><span>${T("now")}</span></div>`;
  return s+`<div class="legend"><span><i style="background:var(--solar)"></i>${T("Solar")}</span><span><i style="background:var(--load)"></i>${T("Consumption")}</span><span><i style="background:var(--blue)"></i>${T("Battery (%)")}</span><span>${T("Max %s Wh per hour",Math.round(max))}</span></div>`;
}
function spark(v,color,label,unit){
  const pts=v.map((x,i)=>[i,x]).filter(p=>p[1]!=null);
  if(pts.length<2)return "";
  let mn=Infinity,mx=-Infinity;pts.forEach(p=>{mn=Math.min(mn,p[1]);mx=Math.max(mx,p[1])});
  if(mx-mn<1)mx=mn+1;
  const W=240,H=56,n=v.length-1;
  const pl=pts.map(p=>`${(p[0]/n*(W-4)+2).toFixed(1)},${(H-4-(p[1]-mn)/(mx-mn)*(H-8)).toFixed(1)}`).join(" ");
  return `<div class="tile"><div class="sub">${label}</div><svg class="sp" viewBox="0 0 ${W} ${H}" preserveAspectRatio="none"><polyline points="${pl}" fill="none" stroke="${color}" stroke-width="2" vector-effect="non-scaling-stroke"/></svg><div class="sub">${Math.round(mn)}–${Math.round(mx)} ${unit}</div></div>`;
}
function seg(kind,idx,on,enabled){
  const d=enabled?"":" disabled";
  return `<span class="seg"><button data-k="${kind}" data-i="${idx}" data-on="0" class="${on?"":"act"}"${d}>${T("Off")}</button><button data-k="${kind}" data-i="${idx}" data-on="1" class="${on?"act":""}"${d}>${T("On")}</button></span>`;
}

/* ---- the page ---- */
function render(d){
  document.title=d.name||T("Display");$("name").textContent=d.name||"";
  $("time").textContent=d.time||"--:--";$("date").textContent=d.date||"";
  $("alarms").innerHTML=(d.alarms||[]).map(a=>`<div class="alarm lvl${a.level}">⚠ ${esc(a.text)}</div>`).join("");
  const c=[];

  /* boat: NMEA 2000 */
  const bt=d.boat;
  if(bt){
    const L=bt.levels||{};
    const t=(n,v,u,cl="")=>`<div class="tile"><div class="sub">${n}</div><span class="mid ${cl}">${v}</span> <span class="sub">${u}</span></div>`;
    let b=`<div class="tiles">${t(T("Depth"),num(bt.depth,1),"m",lvl(L.depth))}${t(T("Log"),num(bt.speed_water,1),"kn")}${t(T("SOG"),num(bt.sog,1),"kn")}${t(T("COG"),num(bt.cog,0),"°")}${t(T("Sea"),num(bt.sea_temp,1),"°C")}</div>`;
    c.push(card(T("Boat"),b));
    const e=bt.engine||{};
    b=`<div class="tiles">${t(T("RPM"),num(e.rpm,0),"",lvl(L.rpm))}${t(T("Coolant"),num(e.coolant,0),"°C",lvl(L.coolant))}${t(T("Oil"),num(e.oil_pressure,1),"bar",lvl(L.oil_pressure))}${t(T("Alternator"),num(e.alternator,1),"V",lvl(L.alternator))}</div>`;
    c.push(card(!e.name||e.name=="Engine"||e.name==T("Engine")?T("Engine"):T("Engine: %s",esc(e.name)),b));
    if(bt.tanks&&bt.tanks.length){
      b=bt.tanks.map(k=>{const col=k.state==2?"var(--red)":k.state==1?"var(--amber)":(k.type==5?"#8a6a4c":k.type==0||k.type==6?"#d39a2c":"var(--blue)");
        return `<span class="name">${esc(k.name)}</span><div class="bar"><i style="width:${k.level==null?0:Math.max(0,Math.min(100,k.level))}%;background:${col}"></i></div><b class="${lvl(k.state)}">${num(k.level,0)}%</b>`}).join("");
      c.push(card(T("Tanks"),`<div class="tanks">${b}</div>`));
    }
  }

  /* battery and solar */
  const bat=d.battery||{};
  if(bat.soc!=null){
    const low=bat.soc<20,col=low?"linear-gradient(90deg,#6e1e16,#e74c3c)":"linear-gradient(90deg,#0b4a7a,#38a9f5)";
    let b=`<div class="flex"><span class="big">${num(bat.soc,0)}%</span>${bat.charging?`<span class="chg">⚡ ${T("Charging")}</span>`:""}</div><div class="bar"><i style="width:${Math.max(0,Math.min(100,bat.soc))}%;background:${col}"></i></div>`;
    b+=`<div class="sub">${num(bat.voltage,2)} V${bat.current!=null?" · "+(bat.current>0?"+":"")+num(bat.current,1)+" A":""}${bat.power!=null?" · "+(bat.power>0?"+":"")+num(bat.power,0)+" W":""}${bat.ttg_min!=null&&bat.ttg_min>=0&&bat.power<0?" · "+T("%s left",Math.floor(bat.ttg_min/60)+"h "+String(bat.ttg_min%60).padStart(2,"0")+"m"):""}</div>`;
    c.push(card(T("Battery"),b));
  }else if(!bt)c.push(card(T("Battery"),`<div class="empty">${T("No battery data")}</div>`));
  const so=d.solar;
  if(so&&so.power!=null){
    pvMax=Math.max(pvMax,so.power);
    c.push(card(T("Solar"),`<div class="flex"><span class="big">${num(so.power,0)} W</span><span class="sub">${T("today %s kWh",num(so.yield_kwh,2))}</span></div><div class="bar"><i style="width:${so.power/pvMax*100}%;background:linear-gradient(90deg,#8a4d06,#f5b32b)"></i></div>`));
  }

  /* temperatures */
  const tt=(d.temperatures||[]).map(x=>`<div class="tile"><div class="sub">${esc(x.name)}</div><span class="mid">${num(x.temperature,1)}</span><span class="sub">°C</span></div>`);
  if(d.weather&&d.weather.temperature!=null)tt.push(`<div class="tile"><div class="sub">${T("Weather")}</div><span class="mid">${num(d.weather.temperature,1)}</span><span class="sub">°C</span></div>`);
  if(tt.length)c.push(card(T("Temperatures"),`<div class="tiles">${tt.join("")}</div>`));

  /* history */
  const h=d.history;
  if(h&&h.hours&&(h.hours.solar.some(x=>x>0)||h.hours.load.some(x=>x>0)))
    c.push(card(T("Last 24 hours"),bars24(h.hours),"wide"));
  if(h&&h.recent){
    const sl=[spark(h.recent.solar,"var(--solar)",T("Solar"),"W"),spark(h.recent.soc,"var(--blue)",T("Battery"),"%"),spark(h.recent.load,"var(--load)",T("Consumption"),"W")].filter(x=>x);
    // full width only with two or three charts; a single one fits in a normal card
    if(sl.length)c.push(card(T("Last 10 minutes"),`<div class="tiles" style="grid-template-columns:repeat(auto-fill,minmax(180px,1fr))">${sl.join("")}</div>`,sl.length>1?"wide":""));
  }

  /* weather */
  const w=d.weather||{};
  if(w.temperature!=null){
    let b=`<div class="flex"><span><span class="big">${num(w.temperature,1)}°</span> <span class="sub">${esc(w.condition)}</span></span><span class="sub">${T("H %s° · L %s°",num(w.today_max),num(w.today_min))}</span></div><div class="sub" style="margin-top:6px">${T("Feels %s°",num(w.feels_like,1))} · ${T("humidity %s",num(w.humidity)+"%")} · ${T("wind %s m/s",num(w.wind,1))}</div>`;
    if(w.forecast)b+=`<div class="fc">${w.forecast.map(f=>`<div><b>${esc(f.day)}</b><div class="sub">${esc(f.condition)}</div>${num(f.max)}° / ${num(f.min)}°</div>`).join("")}</div>`;
    c.push(card(T("Weather"),b));
  }else c.push(card(T("Weather"),`<div class="empty">${d.has_location===false?T("No location set"):T("Loading weather…")}</div>`));

  /* Victron */
  if(d.victron){
    const b=d.victron.length?d.victron.map(v=>`<div class="row"><div><div class="name">${esc(v.name)}</div><div class="sub">${esc(v.type)}</div></div><div class="r">${v.power!=null?num(v.power,0)+(v.type=="Inverter"||v.type==T("Inverter")?" VA":" W")+"<br>":""}${v.charging?`<span class="chg">⚡ ${T("Charging")}</span>`:`<span class="sub">${esc(v.state||v.status)}</span>`}</div></div>`).join(""):`<div class="empty">${T("No Victron devices added")}</div>`;
    c.push(card("Victron",b));
  }

  /* relays, Shelly, schedules (caravan) */
  if(d.relays&&(d.relays.length||d.relays_note))
    c.push(card(T("Relays"),d.relays.length?d.relays.map((r,i)=>`<div class="row"><div class="name">${esc(r.name)}</div><div class="r">${d.remote?seg("relay",i,r.on,true):`<span class="${r.on?"on":"off"}">${r.on?T("ON"):T("OFF")}</span>`}</div></div>`).join(""):`<div class="empty">${esc(d.relays_note)}</div>`));
  if(d.shelly&&d.shelly.length)
    c.push(card("Shelly",d.shelly.map(s=>`<div class="row"><div><div class="name">${esc(s.name)}</div><div class="sub">${esc(s.status)}${s.power!=null?" · "+num(s.power,1)+" W":""}</div></div><div class="r">${!s.valid?'<span class="off">--</span>':d.remote?seg("shelly",s.idx,s.on,true):`<span class="${s.on?"on":"off"}">${s.on?T("ON"):T("OFF")}</span>`}</div></div>`).join("")));
  if(d.schedules&&d.schedules.length)
    c.push(card(T("Schedules"),d.schedules.map(s=>`<div class="row"><div><div class="name">${esc(s.name)}</div><div class="sub">${esc(s.status)} · ${esc(s.days)}</div></div><div class="r">${d.remote?seg("sched",s.idx,s.on,true)+(s.overridden?`<br><button class="btn" data-k="sched" data-i="${s.idx}" data-resume="1">${T("Back on schedule")}</button>`:""):`<span class="${s.on?"on":"off"}">${s.on?T("ON"):T("OFF")}</span>`}</div></div>`).join("")));

  /* the board's own battery */
  const bb=d.board_battery;
  if(bb)c.push(card(T("Board battery"),`<div class="flex"><span class="mid">${num(bb.percent)}%</span><span class="sub">${num(bb.voltage,2)} V · ${bb.charging?T("charging or full"):bb.on_battery?T("running on battery"):T("on external power")}</span></div><div class="bar"><i style="width:${bb.percent}%;background:var(--green)"></i></div>`));

  $("grid").innerHTML=c.join("");
  const rate=every()<60000?T("every 30 s"):T("every 2 minutes");
  const links=`<a href="/log">${T("log")}</a> · <a href="/files">${T("files")}</a> · <a href="/json">JSON</a>${d.logout?` · <a href="/logout">${T("Log out")}</a>`:""}`;
  $("foot").innerHTML=`<section class="about"><h2>${T("About")}</h2><dl>
<dt>${T("Updates")}</dt><dd>${T("Updated %s",new Date().toLocaleTimeString(lang))} · ${rate}<br><span style="opacity:.75">${T("Every 30 s while the page is in use, every 2 minutes after 2 minutes without use. Update fetches at once.")}</span></dd>
<dt>${T("Board")}</dt><dd>${esc(d.project)} v${esc(d.version)} · ${T("up %s",fmtUp(d.uptime_min))} · ${links}</dd>
<dt>${T("Source")}</dt><dd><a href="https://github.com/fredrikrudin/${esc(d.project)}">github.com/fredrikrudin/${esc(d.project)}</a></dd>
<dt>${T("License")}</dt><dd>© Fredrik Rudin · <a href="https://creativecommons.org/licenses/by-nc/4.0/">CC BY-NC 4.0</a> · ${T("Written with the help of Claude (Anthropic)")}</dd>
</dl></section>`;
}
function fmtUp(m){if(m==null)return"--";if(m<60)return m+" min";if(m<2880)return Math.floor(m/60)+" h "+(m%60)+" min";return T("%s days",Math.floor(m/1440))}

/* switching (relays, Shelly, schedules) */
$("grid").addEventListener("click",async ev=>{
  const b=ev.target.closest("button[data-k]");if(!b||b.disabled)return;
  const p=new URLSearchParams({ajax:"1"});p.set(b.dataset.k,b.dataset.i);
  if(b.dataset.resume)p.set("resume","1");else p.set("on",b.dataset.on);
  b.closest(".r").querySelectorAll("button").forEach(x=>x.disabled=true);
  try{await fetch("/switch",{method:"POST",body:p})}catch(e){}
  active=Date.now();schedule(300);
});
/* start: the last values (if this browser has them) while the board answers */
{
  const kl=kept("lang");
  if(kl&&kl.l){TXT=kl.TXT||{};lang=kl.l;langV=kl.v||"";document.documentElement.lang=lang}
  const kd=kept("data");
  if(kd&&kd.d)try{
    render(kd.d);document.body.classList.add("stale");
    $("state").textContent=T("Values from %s, loading…",new Date(kd.t).toLocaleTimeString(lang,{hour:"2-digit",minute:"2-digit"}));
  }catch(e){$("grid").innerHTML=""}
  $("upd").textContent="↻ "+T("Update");
}
load();
</script></body></html>
)PAGE";
