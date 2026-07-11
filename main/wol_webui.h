/*
 * wol_webui.h — Embedded dashboard HTML/CSS/JS
 *
 * Single-page app served at /. No external dependencies.
 * Dark theme, mobile-friendly, auto-refresh every 30s.
 */
#pragma once

/* Minified HTML/CSS/JS for the dashboard */
#define WOL_WEBUI_HTML \
"<!DOCTYPE html><html lang=en><meta charset=UTF-8>" \
"<meta name=viewport content='width=device-width,initial-scale=1'>" \
"<title>ESPresso — WoL Dashboard</title>" \
"<style>" \
"*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}" \
":root{--bg:#0d1117;--card:#161b22;--border:#30363d;--text:#c9d1d9;" \
"--muted:#8b949e;--accent:#58a6ff;--green:#3fb950;--red:#f85149;" \
"--yellow:#d2991d;--wake:#238636;--wake-hover:#2ea043;--radius:8px}" \
"@media(prefers-color-scheme:light){:root{--bg:#f6f8fa;--card:#fff;" \
"--border:#d0d7de;--text:#24292f;--muted:#656d76;--accent:#0969da;" \
"--green:#1a7f37;--red:#cf222e;--yellow:#9a6700;--wake:#1f883d;" \
"--wake-hover:#1a7f37}}" \
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif;" \
"background:var(--bg);color:var(--text);min-height:100vh;padding:16px}" \
".container{max-width:640px;margin:0 auto}" \
"header{display:flex;justify-content:space-between;align-items:center;" \
"padding:16px 0;border-bottom:1px solid var(--border);margin-bottom:20px}" \
"header h1{font-size:1.4em;font-weight:600}" \
".status-bar{display:flex;gap:12px;font-size:.8em;color:var(--muted)}" \
".status-dot{display:inline-block;width:8px;height:8px;border-radius:50%;" \
"background:var(--green)}" \
".status-dot.off{background:var(--red)}" \
".add-btn{background:var(--accent);color:#fff;border:none;padding:10px 20px;" \
"border-radius:var(--radius);font-size:.9em;cursor:pointer;font-weight:500}" \
".add-btn:hover{opacity:.9}" \
".host-card{background:var(--card);border:1px solid var(--border);" \
"border-radius:var(--radius);padding:16px;margin-bottom:12px;" \
"display:flex;justify-content:space-between;align-items:center;" \
"transition:border-color .2s}" \
".host-card:hover{border-color:var(--accent)}" \
".host-info{flex:1;min-width:0}" \
".host-name{font-weight:600;font-size:1.05em;display:flex;align-items:center;gap:8px}" \
".host-dot{width:10px;height:10px;border-radius:50%;flex-shrink:0}" \
".host-dot.online{background:var(--green);box-shadow:0 0 6px var(--green)}" \
".host-dot.offline{background:var(--red)}" \
".host-dot.unknown{background:var(--yellow)}" \
".host-details{font-size:.8em;color:var(--muted);margin-top:4px;" \
"display:flex;gap:12px;flex-wrap:wrap}" \
".host-details span{white-space:nowrap}" \
".wake-btn{background:var(--wake);color:#fff;border:none;padding:8px 20px;" \
"border-radius:var(--radius);font-size:.85em;cursor:pointer;font-weight:500;" \
"white-space:nowrap;transition:background .2s}" \
".wake-btn:hover{background:var(--wake-hover)}" \
".wake-btn:active{transform:scale(.97)}" \
".wake-btn.sent{background:var(--accent)}" \
".remove-btn{background:none;border:none;color:var(--muted);cursor:pointer;" \
"font-size:1.2em;padding:4px 8px;opacity:.5;transition:opacity .2s}" \
".remove-btn:hover{opacity:1;color:var(--red)}" \
".empty-state{text-align:center;padding:60px 20px;color:var(--muted)}" \
".empty-state p{font-size:1.1em;margin-bottom:8px}" \
".modal-overlay{display:none;position:fixed;top:0;left:0;right:0;bottom:0;" \
"background:rgba(0,0,0,.6);z-index:100;justify-content:center;align-items:center}" \
".modal-overlay.active{display:flex}" \
".modal{background:var(--card);border:1px solid var(--border);" \
"border-radius:12px;padding:24px;width:90%;max-width:400px}" \
".modal h2{font-size:1.15em;margin-bottom:16px}" \
".modal label{display:block;font-size:.85em;color:var(--muted);margin-bottom:4px}" \
".modal input{width:100%;padding:10px;background:var(--bg);border:1px solid var(--border);" \
"border-radius:var(--radius);color:var(--text);font-size:.95em;margin-bottom:12px;" \
"outline:none}" \
".modal input:focus{border-color:var(--accent)}" \
".modal-actions{display:flex;gap:8px;justify-content:flex-end;margin-top:8px}" \
".modal-actions button{padding:8px 16px;border-radius:var(--radius);font-size:.9em;" \
"cursor:pointer;border:1px solid var(--border)}" \
".btn-cancel{background:var(--bg);color:var(--text)}" \
".btn-save{background:var(--accent);color:#fff;border-color:var(--accent)}" \
".toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%);" \
"background:var(--card);border:1px solid var(--border);padding:12px 24px;" \
"border-radius:var(--radius);font-size:.9em;z-index:200;opacity:0;" \
"transition:opacity .3s;pointer-events:none}" \
".toast.show{opacity:1}" \
".toast.error{border-color:var(--red)}" \
".refresh-bar{display:flex;justify-content:space-between;align-items:center;" \
"padding:8px 0;font-size:.75em;color:var(--muted)}" \
".refresh-bar button{background:none;border:1px solid var(--border);color:var(--muted);" \
"padding:4px 12px;border-radius:var(--radius);cursor:pointer;font-size:.85em}" \
"footer{text-align:center;padding:24px 0;font-size:.75em;color:var(--muted);" \
"border-top:1px solid var(--border);margin-top:20px}" \
"@media(max-width:480px){.host-card{flex-direction:column;align-items:stretch;gap:12px}" \
".host-actions{display:flex;gap:8px}.wake-btn{flex:1}}" \
"</style>" \
"<div class=container>" \
"<header><h1>⚡ ESPresso</h1>" \
"<div class=status-bar><span class=status-dot id=ts-dot></span>" \
"<span id=ts-ip>...</span></div></header>" \
"<button class=add-btn onclick=showModal()>+ Add Host</button>" \
"<div class=refresh-bar>" \
"<span>Auto-refresh: 30s · <span id=last-update>—</span></span>" \
"<button onclick=loadHosts()>🔄 Refresh</button></div>" \
"<div id=hosts></div>" \
"<footer>ESPresso v1.0 · <span id=uptime>—</span> up · " \
"<span id=heap>—</span> free</footer></div>" \
"<div class=modal-overlay id=modal>" \
"<div class=modal><h2>Add Host</h2>" \
"<label>Name</label><input id=f-name placeholder='NAS Server'>" \
"<label>MAC Address</label><input id=f-mac placeholder='AA:BB:CC:DD:EE:FF'>" \
"<label>IP Address</label><input id=f-ip placeholder='192.168.1.50'>" \
"<div class=modal-actions>" \
"<button class=btn-cancel onclick=hideModal()>Cancel</button>" \
"<button class=btn-save onclick=addHost()>Save</button></div></div></div>" \
"<div class=toast id=toast></div>" \
"<script>" \
"let hosts=[];" \
"async function loadHosts(){" \
"try{let r=await fetch('/api/hosts');let d=await r.json();hosts=d.hosts;render()}" \
"catch(e){toast('Connection error','error')}" \
"document.getElementById('last-update').textContent=new Date().toLocaleTimeString()}" \
"function render(){" \
"let el=document.getElementById('hosts');" \
"if(!hosts.length){el.innerHTML='<div class=empty-state><p>No hosts configured</p><p class=muted>Click \"+ Add Host\" to add one</p></div>';return}" \
"el.innerHTML=hosts.map(h=>`" \
"<div class=host-card>" \
"<div class=host-info>" \
"<div class=host-name><span class='host-dot ${h.online?\"online\":(h.last_seen?\"offline\":\"unknown\")}'></span>${esc(h.name)}</div>" \
"<div class=host-details><span>MAC: ${esc(h.mac)}</span><span>IP: ${esc(h.ip)}</span>" \
"${h.last_seen?`<span>Seen: ${new Date(h.last_seen*1000).toLocaleTimeString()}</span>`:''}</div></div>" \
"<div class=host-actions>" \
"<button class=wake-btn onclick=wakeHost(${h.id},this)>⏻ Wake</button>" \
"<button class=remove-btn onclick=removeHost(${h.id}) title=Remove>×</button></div></div>`).join('')}" \
"async function addHost(){" \
"let n=document.getElementById('f-name').value.trim();" \
"let m=document.getElementById('f-mac').value.trim();" \
"let ip=document.getElementById('f-ip').value.trim();" \
"if(!n||!m||!ip){toast('All fields required','error');return}" \
"if(!/^([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}$/.test(m)){toast('Invalid MAC format','error');return}" \
"try{let r=await fetch('/api/hosts',{method:'POST',headers:{'Content-Type':'application/json'}," \
"body:JSON.stringify({name:n,mac:m.toUpperCase(),ip})});" \
"let d=await r.json();" \
"if(d.ok){toast('Host added');hideModal();loadHosts()}else{toast(d.error||'Failed','error')}}" \
"catch(e){toast('Request failed','error')}}" \
"async function removeHost(id){" \
"if(!confirm('Remove this host?'))return;" \
"try{await fetch('/api/hosts?id='+id,{method:'DELETE'});toast('Host removed');loadHosts()}" \
"catch(e){toast('Failed','error')}}" \
"async function wakeHost(id,btn){" \
"btn.textContent='...';btn.classList.add('sent');" \
"try{let r=await fetch('/api/wake?id='+id,{method:'POST'});let d=await r.json();" \
"if(d.ok){toast('Magic packet sent to '+d.host)}else{toast(d.error||'Failed','error')}}" \
"catch(e){toast('Request failed','error')}" \
"setTimeout(()=>{btn.textContent='⏻ Wake';btn.classList.remove('sent')},2000)}" \
"function showModal(){document.getElementById('modal').classList.add('active');" \
"document.getElementById('f-name').focus()}" \
"function hideModal(){document.getElementById('modal').classList.remove('active');" \
"document.getElementById('f-name').value='';" \
"document.getElementById('f-mac').value='';document.getElementById('f-ip').value=''}" \
"function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\"/g,'&quot;')}" \
"function toast(msg,type){let t=document.getElementById('toast');" \
"t.textContent=msg;t.className='toast show'+(type==='error'?' error':'');" \
"setTimeout(()=>t.className='toast',2500)}" \
"async function loadStatus(){" \
"try{let r=await fetch('/api/status');let d=await r.json();" \
"document.getElementById('ts-dot').className='status-dot'+(d.tailscale_connected?'':' off');" \
"document.getElementById('ts-ip').textContent=d.tailscale_ip||'offline';" \
"document.getElementById('uptime').textContent=fmtUptime(d.uptime);" \
"document.getElementById('heap').textContent=fmtBytes(d.free_heap)}catch(e){}}" \
"function fmtUptime(s){if(s<60)return s+'s';if(s<3600)return Math.floor(s/60)+'m';" \
"if(s<86400)return Math.floor(s/3600)+'h '+Math.floor(s%3600/60)+'m';" \
"return Math.floor(s/86400)+'d '+Math.floor(s%86400/3600)+'h'}" \
"function fmtBytes(b){return b>1048576?(b/1048576).toFixed(1)+' MB':(b/1024).toFixed(0)+' KB'}" \
"document.getElementById('modal').addEventListener('click',function(e){" \
"if(e.target===this)hideModal()});" \
"loadHosts();loadStatus();" \
"setInterval(loadHosts,30000);" \
"setInterval(loadStatus,60000);" \
"</script>"
