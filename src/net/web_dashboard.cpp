#include "net/web_dashboard.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "app_config.h"
#include "sensors/comfort.h"

namespace fw {
namespace {

struct Appender {
  Appender(char* outBuffer, size_t bufferSize) : out(outBuffer), size(bufferSize) {}

  char* out;
  size_t size;
  size_t used = 0;
  size_t needed = 0;

  void appendf(const char* format, ...) {
    char scratch = '\0';
    char* destination = (out != nullptr && used < size) ? out + used : &scratch;
    const size_t remaining = (out != nullptr && used < size) ? size - used : 0;
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(destination, remaining, format, args);
    va_end(args);
    if (written <= 0) {
      return;
    }
    needed += static_cast<size_t>(written);
    if (remaining > 0) {
      const size_t available = remaining - 1;
      used += static_cast<size_t>(written) < available
                  ? static_cast<size_t>(written)
                  : available;
    }
  }
};

void appendJsonString(Appender& text, const char* value) {
  text.appendf("\"");
  if (value != nullptr) {
    for (const char* p = value; *p != '\0'; ++p) {
      const unsigned char c = static_cast<unsigned char>(*p);
      switch (c) {
        case '"':
          text.appendf("\\\"");
          break;
        case '\\':
          text.appendf("\\\\");
          break;
        case '\n':
          text.appendf("\\n");
          break;
        case '\r':
          text.appendf("\\r");
          break;
        case '\t':
          text.appendf("\\t");
          break;
        default:
          if (c < 0x20) {
            text.appendf("\\u%04x", static_cast<unsigned>(c));
          } else {
            text.appendf("%c", static_cast<char>(c));
          }
          break;
      }
    }
  }
  text.appendf("\"");
}

void appendJsonFloat(Appender& text, bool present, float value) {
  if (present) {
    text.appendf("%.2f", static_cast<double>(value));
  } else {
    text.appendf("null");
  }
}

void appendHtmlEscaped(Appender& text, const char* value) {
  if (value == nullptr) {
    return;
  }
  for (const char* p = value; *p != '\0'; ++p) {
    switch (*p) {
      case '&':
        text.appendf("&amp;");
        break;
      case '<':
        text.appendf("&lt;");
        break;
      case '>':
        text.appendf("&gt;");
        break;
      case '"':
        text.appendf("&quot;");
        break;
      case '\'':
        text.appendf("&#39;");
        break;
      default:
        text.appendf("%c", *p);
        break;
    }
  }
}

}  // namespace

size_t formatStatusJson(const DashboardState& state, char* out, size_t outSize) {
  Appender text{out, outSize};
  const SensorSnapshot& sensors = state.sensors;

  text.appendf("{\"version\":");
  appendJsonString(text, state.version);
  text.appendf(",\"uptime_ms\":%lu,\"free_heap\":%lu,\"mode\":",
               static_cast<unsigned long>(state.uptimeMs),
               static_cast<unsigned long>(state.freeHeapBytes));
  appendJsonString(text, state.mode);

  text.appendf(",\"wifi\":{\"mode\":");
  appendJsonString(text, wifiModeName(state.wifiMode));
  text.appendf(",\"connected\":%s,\"ssid\":", state.wifiConnected ? "true" : "false");
  appendJsonString(text, state.ssid);
  text.appendf(",\"ip\":");
  appendJsonString(text, state.ip);
  text.appendf(",\"rssi\":%d,\"ntp\":%s,\"time\":", static_cast<int>(state.rssi),
               state.ntpSynced ? "true" : "false");
  if (state.ntpSynced && state.timestamp[0] != '\0') {
    appendJsonString(text, state.timestamp);
  } else {
    text.appendf("null");
  }
  text.appendf("}");

  text.appendf(",\"sensors\":{\"bmp280\":");
  appendJsonString(text, sensors.bmp280Present ? sensors.bmp280Chip : "none");
  text.appendf(",\"temperature\":");
  appendJsonFloat(text, sensors.hasTemperature(), sensors.displayTemperatureC());
  text.appendf(",\"humidity\":");
  appendJsonFloat(text, sensors.hasHumidity(), sensors.humidityPct);
  text.appendf(",\"pressure\":");
  appendJsonFloat(text, sensors.hasPressure(), sensors.pressureHpa);
  text.appendf(",\"trend\":");
  appendJsonString(text, weatherTrendLabel(sensors.pressureTrend));
  text.appendf(",\"comfort\":");
  if (sensors.hasTemperature() && sensors.hasHumidity()) {
    const float index = discomfortIndex(sensors.displayTemperatureC(), sensors.humidityPct);
    text.appendf("{\"index\":%.1f,\"label\":", static_cast<double>(index));
    appendJsonString(text, comfortLabel(index));
    text.appendf("}");
  } else {
    text.appendf("null");
  }
  text.appendf("}");

  text.appendf(",\"alerts\":{\"high_temperature\":%s,\"low_humidity\":%s,"
               "\"pressure_jump\":%s}",
               state.alerts.highTemperature ? "true" : "false",
               state.alerts.lowHumidity ? "true" : "false",
               state.alerts.pressureJump ? "true" : "false");

  text.appendf(",\"log\":{\"ready\":%s,\"enabled\":%s,\"records\":%lu,"
               "\"interval_ms\":%lu,\"bytes\":%lu}",
               state.logStorageReady ? "true" : "false",
               state.loggingEnabled ? "true" : "false",
               static_cast<unsigned long>(state.logRecordCount),
               static_cast<unsigned long>(state.logIntervalMs),
               static_cast<unsigned long>(state.logFileBytes));

  text.appendf(",\"ble\":{\"enabled\":%s,\"hid\":%s,\"connected\":%s}",
               state.bleEnabled ? "true" : "false",
               state.bleHidMode ? "true" : "false",
               state.bleConnected ? "true" : "false");

  text.appendf(",\"timer\":{\"running\":%s,\"remaining\":",
               state.timerRunning ? "true" : "false");
  appendJsonString(text, state.timerRemaining);
  text.appendf(",\"phase\":");
  appendJsonString(text, state.timerPhase);
  text.appendf("}}");
  return text.needed;
}

size_t formatLogsJson(const LogSample* samples, size_t count, char* out,
                      size_t outSize) {
  Appender text{out, outSize};
  text.appendf("[");
  for (size_t index = 0; index < count; ++index) {
    const LogSample& sample = samples[index];
    text.appendf("%s{\"t\":", index == 0 ? "" : ",");
    if (sample.timestamp[0] != '\0') {
      appendJsonString(text, sample.timestamp);
    } else {
      text.appendf("null");
    }
    text.appendf(",\"uptime_ms\":%lu,\"temp_c\":",
                 static_cast<unsigned long>(sample.uptimeMs));
    appendJsonFloat(text, sample.hasTemperature, sample.temperatureC);
    text.appendf(",\"pressure_hpa\":");
    appendJsonFloat(text, sample.hasPressure, sample.pressureHpa);
    text.appendf(",\"humidity_pct\":");
    appendJsonFloat(text, sample.hasHumidity, sample.humidityPct);
    text.appendf("}");
  }
  text.appendf("]");
  return text.needed;
}

size_t formatLogsJsonFromCsv(const char* csv, size_t maxRows, char* out,
                             size_t outSize) {
  std::vector<LogSample> samples;
  if (csv != nullptr) {
    samples.reserve(maxRows == 0 ? 16 : maxRows);
    const char* cursor = csv;
    while (*cursor != '\0') {
      const char* end = std::strchr(cursor, '\n');
      const size_t length =
          end != nullptr ? static_cast<size_t>(end - cursor) : std::strlen(cursor);
      if (length > 0) {
        char line[160];
        const size_t copyLength = length < sizeof(line) - 1 ? length : sizeof(line) - 1;
        std::memcpy(line, cursor, copyLength);
        line[copyLength] = '\0';
        if (!isLogHeaderLine(line)) {
          LogSample sample;
          if (parseLogLine(line, &sample)) {
            samples.push_back(sample);
            if (maxRows > 0 && samples.size() > maxRows) {
              samples.erase(samples.begin());
            }
          }
        }
      }
      cursor = end != nullptr ? end + 1 : cursor + length;
    }
  }
  return formatLogsJson(samples.data(), samples.size(), out, outSize);
}

const char* dashboardHtml() {
  return R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Env Station</title>
<style>
:root{color-scheme:dark;--bg:#0d1117;--panel:#161b22;--line:#30363d;--text:#e6edf3;--muted:#8b949e;--accent:#58a6ff;--temp:#56d4dd;--pres:#f0a44b;--ok:#3fb950;--bad:#f85149}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:16px 20px;border-bottom:1px solid var(--line)}
h1{font-size:18px;margin:0;letter-spacing:.02em}
h2{font-size:14px;margin:0;color:var(--muted);text-transform:uppercase;letter-spacing:.08em}
main{max-width:860px;margin:0 auto;padding:20px}
.pill{font-size:12px;padding:4px 10px;border-radius:999px;background:var(--panel);border:1px solid var(--line);color:var(--muted)}
.pill.ok{color:var(--ok);border-color:var(--ok)}
.pill.bad{color:var(--bad);border-color:var(--bad)}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:14px 16px;display:flex;flex-direction:column;gap:6px}
.card .label{font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:.08em}
.card .value b{font-size:30px;font-weight:600;font-variant-numeric:tabular-nums}
.card .value small{font-size:14px;color:var(--muted);margin-left:4px}
.alert{margin-top:14px;padding:12px 16px;border-radius:12px;background:rgba(248,81,73,.12);border:1px solid var(--bad);color:#ffb3ad;font-weight:600}
.hidden{display:none}
.panel{margin-top:16px;background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:16px}
.panel-head{display:flex;align-items:center;justify-content:space-between;margin-bottom:12px}
canvas{width:100%;height:auto;display:block;border-radius:8px;background:#0b0f14}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:8px 20px;margin:0}
.grid div{display:flex;justify-content:space-between;gap:12px;border-bottom:1px dashed var(--line);padding:6px 0}
.grid dt{color:var(--muted)}
.grid dd{margin:0;font-variant-numeric:tabular-nums}
.actions{display:flex;flex-wrap:wrap;gap:10px;margin-top:14px}
.btn{appearance:none;border:1px solid var(--line);background:#21262d;color:var(--text);border-radius:10px;padding:9px 14px;font:inherit;text-decoration:none;cursor:pointer}
.btn:hover{border-color:var(--accent);color:var(--accent)}
.btn.active{border-color:var(--accent);color:var(--accent);background:#1f2937}
.modes{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}
.modes .btn{padding:8px 12px}
.muted{color:var(--muted);font-size:12px}
footer{max-width:860px;margin:0 auto;padding:8px 20px 28px;color:var(--muted);font-size:12px}
</style></head>
<body>
<header><h1>ESP32 Env Station</h1><span id="conn" class="pill">connecting…</span></header>
<main>
<section class="cards">
<div class="card"><span class="label">Temperature</span><span class="value"><b id="temp">--</b><small>°C</small></span></div>
<div class="card"><span class="label">Humidity</span><span class="value"><b id="hum">--</b><small>%</small></span></div>
<div class="card"><span class="label">Pressure</span><span class="value"><b id="pres">----</b><small>hPa</small></span></div>
<div class="card"><span class="label">Comfort</span><span class="value"><b id="comfort" style="font-size:22px">--</b></span></div>
</section>
<div id="alert" class="alert hidden"></div>
<section class="panel"><div class="panel-head"><h2>OLED remote</h2><span id="oled-state" class="muted">--</span></div>
<div class="actions">
<button class="btn" id="oled-power" onclick="oledPost({power:'toggle'})">Screen off</button>
<button class="btn" onclick="oledPost({action:'back'})">Back</button>
<button class="btn" onclick="oledPost({action:'prev'})">&#9664; Prev</button>
<button class="btn" onclick="oledPost({action:'next'})">Next &#9654;</button>
<button class="btn" onclick="oledPost({action:'select'})">Select</button>
<button class="btn" onclick="oledPost({action:'clock'})">Clock chime</button>
</div>
<div class="modes" id="oled-modes">
<button class="btn" data-mode="pet" onclick="oledPost({mode:'pet'})">Pet</button>
<button class="btn" data-mode="env" data-view="summary" onclick="oledPost({mode:'env',view:'summary'})">Monitor</button>
<button class="btn" data-mode="env" data-view="graph" onclick="oledPost({mode:'env',view:'graph'})">Graph</button>
<button class="btn" data-mode="env" data-view="detail" onclick="oledPost({mode:'env',view:'detail'})">Detail</button>
<button class="btn" data-mode="logs" onclick="oledPost({mode:'logs'})">Logs</button>
<button class="btn" data-mode="wifi" onclick="oledPost({mode:'wifi'})">Wi-Fi</button>
<button class="btn" data-mode="ble" onclick="oledPost({mode:'ble'})">BLE</button>
<button class="btn" data-mode="timer" onclick="oledPost({mode:'timer'})">Timer</button>
<button class="btn" data-mode="system" onclick="oledPost({mode:'system'})">System</button>
<button class="btn" data-mode="menu" onclick="oledPost({mode:'menu'})">Menu</button>
</div>
<p class="muted">The panel wakes on its own when a button on the device is pressed.</p></section>
<section class="panel"><div class="panel-head"><h2>History</h2><span id="range" class="muted"></span></div>
<canvas id="chart" width="640" height="220"></canvas></section>
<section class="panel"><div class="panel-head"><h2>Device</h2><span id="updated" class="muted"></span></div>
<dl id="info" class="grid"></dl>
<div class="actions">
<a class="btn" href="/download">Download /log.csv</a>
<button class="btn" onclick="clearLog()">Clear log</button>
<button class="btn" onclick="showQr()">Show QR on OLED</button>
<a class="btn" href="/setup">Wi-Fi setup</a>
</div></section>
</main>
<footer>Offline dashboard served by the ESP32. No external assets.</footer>
<script>
const $=id=>document.getElementById(id);
const f=(v,d=1)=>v==null?'--':Number(v).toFixed(d);
const esc=v=>String(v).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function row(k,v){return '<div><dt>'+esc(k)+'</dt><dd>'+esc(v)+'</dd></div>'}
async function tick(){
 try{
  const r=await fetch('/api/status',{cache:'no-store'});const s=await r.json();
  $('conn').textContent=s.wifi.connected?('online '+s.wifi.ip):'offline';
  $('conn').className='pill '+(s.wifi.connected?'ok':'bad');
  $('temp').textContent=f(s.sensors.temperature);
  $('hum').textContent=f(s.sensors.humidity);
  $('pres').textContent=f(s.sensors.pressure,1);
  $('comfort').textContent=s.sensors.comfort?s.sensors.comfort.label:'--';
  const a=[];
  if(s.alerts.high_temperature)a.push('High temperature');
  if(s.alerts.low_humidity)a.push('Low humidity');
  if(s.alerts.pressure_jump)a.push('Pressure jump');
  $('alert').classList.toggle('hidden',a.length===0);
  $('alert').textContent=a.join(' · ');
  $('info').innerHTML=[
   row('Firmware',s.version),row('Mode',s.mode),row('SSID',s.wifi.ssid||'--'),
   row('RSSI',s.wifi.rssi+' dBm'),row('NTP',s.wifi.time||'not synced'),
   row('Log',s.log.enabled?('recording · '+s.log.records+' rows'):'paused'),
   row('Log size',(s.log.bytes/1024).toFixed(1)+' KB'),
   row('BLE',s.ble.enabled?(s.ble.hid?'HID remote':'sensors'):'off'),
   row('Timer',s.timer.remaining+' ('+s.timer.phase+')'),
   row('Heap',(s.free_heap/1024).toFixed(0)+' KB'),row('Uptime',Math.floor(s.uptime_ms/1000)+' s')
  ].join('');
  $('updated').textContent=new Date().toLocaleTimeString();
 }catch(e){$('conn').textContent='offline';$('conn').className='pill bad'}
}
let last=[];
async function logs(){
 try{const r=await fetch('/api/logs',{cache:'no-store'});last=await r.json();draw()}catch(e){}
}
function draw(){
 const c=$('chart'),g=c.getContext('2d'),W=c.width,H=c.height;
 g.clearRect(0,0,W,H);
 if(!last.length){g.fillStyle='#8b949e';g.font='14px system-ui';g.fillText('No log rows yet',16,28);return}
 const rows=last.slice(-60),pad=28;
 const vals=rows.map(r=>r.temp_c).filter(v=>v!=null);
 const pres=rows.map(r=>r.pressure_hpa).filter(v=>v!=null);
 $('range').textContent=rows.length+' samples';
 g.strokeStyle='#30363d';g.lineWidth=1;
 for(let i=0;i<5;i++){const y=pad+(H-2*pad)*i/4;g.beginPath();g.moveTo(pad,y);g.lineTo(W-pad,y);g.stroke()}
 const line=(arr,color,scale)=>{
  if(arr.length<2)return;const lo=Math.min(...arr),hi=Math.max(...arr),span=(hi-lo)||1;
  g.strokeStyle=color;g.lineWidth=2;g.beginPath();
  arr.forEach((v,i)=>{const x=pad+(W-2*pad)*i/(arr.length-1);const y=H-pad-(H-2*pad)*((v-lo)/span);i?g.lineTo(x,y):g.moveTo(x,y)});
  g.stroke();
 };
 line(vals,'#56d4dd');line(pres,'#f0a44b');
 g.font='12px system-ui';
 if(vals.length){g.fillStyle='#56d4dd';g.fillText('Temp '+Math.min(...vals).toFixed(1)+'–'+Math.max(...vals).toFixed(1)+' °C',pad,16)}
 if(pres.length){g.fillStyle='#f0a44b';g.textAlign='right';g.fillText('Pressure '+Math.min(...pres).toFixed(0)+'–'+Math.max(...pres).toFixed(0)+' hPa',W-pad,16);g.textAlign='left'}
}
function renderOled(s){
 $('oled-state').textContent=(s.power?'screen on':'screen off')+' \u00b7 '+
  (s.screen==='menu'?'menu':s.mode+(s.view!=='summary'?' '+s.view:''));
 $('oled-power').textContent=s.power?'Screen off':'Screen on';
 document.querySelectorAll('#oled-modes .btn').forEach(function(b){
  const m=b.dataset.mode,v=b.dataset.view;
  const active=(m==='menu')?(s.screen==='menu'):(s.screen==='mode'&&m===s.mode&&(!v||v===s.view));
  b.classList.toggle('active',active);
 });
}
async function oledPost(fields){
 try{
  const r=await fetch('/api/oled',{method:'POST',
   headers:{'Content-Type':'application/x-www-form-urlencoded'},
   body:new URLSearchParams(fields)});
  renderOled(await r.json());
 }catch(e){}
}
async function oledTick(){
 try{const r=await fetch('/api/oled',{cache:'no-store'});renderOled(await r.json())}catch(e){}
}
function clearLog(){if(confirm('Delete /log.csv and start over?'))fetch('/api/log/clear',{method:'POST'}).then(logs)}
function showQr(){fetch('/api/qr',{method:'POST'})}
tick();logs();oledTick();setInterval(tick,2000);setInterval(logs,30000);setInterval(oledTick,2000);
</script>
</body></html>)HTML";
}

size_t formatSetupPage(const char* const* ssids, size_t count, const char* message,
                       const char* token, bool scanning, char* out, size_t outSize) {
  Appender text{out, outSize};
  text.appendf(
      "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  text.appendf(
      "<title>ESP32 Wi-Fi setup</title><style>"
      "body{margin:0;background:#0d1117;color:#e6edf3;font:15px/1.5 system-ui,sans-serif}"
      "main{max-width:460px;margin:0 auto;padding:24px}"
      "h1{font-size:20px}h2{font-size:14px;color:#8b949e;text-transform:uppercase;"
      "letter-spacing:.08em}label{display:block;margin:14px 0 4px;color:#8b949e;font-size:13px}"
      "input{width:100%;box-sizing:border-box;padding:11px 12px;border-radius:10px;"
      "border:1px solid #30363d;background:#161b22;color:#e6edf3;font:inherit}"
      "button{appearance:none;border:1px solid #30363d;background:#21262d;color:#e6edf3;"
      "border-radius:10px;padding:11px 14px;font:inherit;cursor:pointer}"
      "button.primary{background:#1f6feb;border-color:#1f6feb;width:100%;margin-top:18px}"
      ".msg{padding:10px 12px;border-radius:10px;background:rgba(248,81,73,.12);"
      "border:1px solid #f85149;color:#ffb3ad}"
      ".net{list-style:none;padding:0;margin:8px 0;display:flex;flex-wrap:wrap;gap:8px}"
      "</style></head><body><main><h1>Wi-Fi setup</h1>");
  if (message != nullptr && message[0] != '\0') {
    text.appendf("<p class=\"msg\">");
    appendHtmlEscaped(text, message);
    text.appendf("</p>");
  }
  text.appendf(
      "<form method=\"post\" action=\"/save\">"
      "<input type=\"hidden\" name=\"token\" value=\"");
  appendHtmlEscaped(text, token != nullptr ? token : "");
  text.appendf(
      "\">"
      "<label for=\"ssid\">Network name (SSID)</label>"
      "<input id=\"ssid\" name=\"ssid\" maxlength=\"32\" required autocomplete=\"off\">"
      "<label for=\"password\">Password (leave empty for an open network)</label>"
      "<input id=\"password\" name=\"password\" type=\"password\" maxlength=\"63\">"
      "<button class=\"primary\" type=\"submit\">Save and connect</button></form>"
      "<h2>Nearby networks</h2><ul class=\"net\">");
  for (size_t index = 0; index < count; ++index) {
    const char* ssid = ssids[index];
    if (ssid == nullptr || ssid[0] == '\0') {
      continue;
    }
    text.appendf("<li><button type=\"button\" onclick=\"document.getElementById('ssid')"
                 ".value=this.dataset.ssid\" data-ssid=\"");
    appendHtmlEscaped(text, ssid);
    text.appendf("\">");
    appendHtmlEscaped(text, ssid);
    text.appendf("</button></li>");
  }
  if (count == 0) {
    text.appendf("<li class=\"muted\">no networks found yet</li>");
  }
  text.appendf(
      "</ul><p style=\"color:#8b949e;font-size:12px\">The station saves the "
      "network in NVS and reconnects automatically. Hold BAK to leave setup "
      "mode without saving.</p>");
  if (scanning && count == 0) {
    // The scan is still running: reload once with the results, but never while
    // a network name or password is being typed (hidden SSIDs stay 0 results).
    text.appendf(
        "<script>setTimeout(function(){"
        "var ssid=document.getElementById('ssid');var pass="
        "document.getElementById('password');"
        "if(!ssid.value&&!pass.value){location.reload();}},6000);</script>");
  }
  text.appendf("</main></body></html>");
  return text.needed;
}

size_t formatSetupLockedPage(char* out, size_t outSize) {
  Appender text{out, outSize};
  text.appendf(
      "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>Wi-Fi setup</title><style>body{margin:0;background:#0d1117;"
      "color:#e6edf3;font:15px/1.5 system-ui,sans-serif}main{max-width:460px;"
      "margin:0 auto;padding:24px}code{background:#161b22;border:1px solid #30363d;"
      "border-radius:6px;padding:1px 5px}</style></head><body><main>"
      "<h1>Wi-Fi setup is locked</h1>"
      "<p>The station is connected to a saved network, so the Wi-Fi settings "
      "cannot be changed from this page.</p>"
      "<p>To change them, go to the Wi-Fi screen on the device and press "
      "<code>PSH</code> (or send <code>wifi ap</code> over serial). The "
      "<code>%s</code> access point starts with a password shown on the OLED; "
      "join it and this page opens.</p>"
      "<p><a href=\"/\" style=\"color:#58a6ff\">Back to the dashboard</a></p>"
      "</main></body></html>",
      kSetupApSsid);
  return text.needed;
}

size_t formatSavedPage(const char* ssid, char* out, size_t outSize) {
  Appender text{out, outSize};
  text.appendf(
      "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>Saved</title><style>body{margin:0;background:#0d1117;color:#e6edf3;"
      "font:15px/1.5 system-ui,sans-serif}main{max-width:460px;margin:0 auto;padding:32px}"
      "a{color:#58a6ff}</style></head><body><main><h1>Saved</h1><p>");
  appendHtmlEscaped(text, ssid != nullptr ? ssid : "");
  text.appendf(
      " was saved. The station is connecting now; this setup network will stay "
      "up for a few seconds.</p><p><a href=\"/\">Continue</a></p></main></body></html>");
  return text.needed;
}

}  // namespace fw
