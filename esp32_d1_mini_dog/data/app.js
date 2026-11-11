/* =========================================================
   DogBot Control Panel
   ========================================================= */
'use strict';

const $  = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

const MODE_LABEL = {
  stand: '待機', walk: '行走中', sit: '坐下',
  lie: '趴下', wave: '舉手', wag: '撒嬌', manual: '手動測試'
};
const MODE_CLASS = { walk: 'walk', manual: 'manual', lie: 'lie' };

const esc = (value) => String(value ?? '').replace(/[&<>"']/g, (c) => (
  { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]
));

function fmtBytes(bytes) {
  if (!bytes && bytes !== 0) return '—';
  if (bytes < 1024) return bytes + ' B';
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
  return (bytes / 1024 / 1024).toFixed(2) + ' MB';
}

function fmtUptime(sec) {
  sec = Math.max(0, Math.floor(sec));
  const d = Math.floor(sec / 86400);
  const h = Math.floor((sec % 86400) / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;
  if (d) return `${d} 天 ${h} 小時 ${m} 分`;
  if (h) return `${h} 小時 ${m} 分 ${s} 秒`;
  if (m) return `${m} 分 ${s} 秒`;
  return `${s} 秒`;
}

function signalLevel(rssi) {
  if (rssi >= -55) return 4;
  if (rssi >= -67) return 3;
  if (rssi >= -75) return 2;
  return 1;
}

async function api(path, data) {
  const opts = { cache: 'no-store', headers: {} };
  if (data !== undefined) {
    opts.method = 'POST';
    opts.headers['Content-Type'] = 'application/x-www-form-urlencoded;charset=UTF-8';
    opts.body = new URLSearchParams(data).toString();
  }
  const res = await fetch(path, opts);
  const text = await res.text();
  let json = {};
  try { json = text ? JSON.parse(text) : {}; } catch (_) { /* ignore */ }
  if (!res.ok) throw new Error(json.error || json.message || ('HTTP ' + res.status));
  return json;
}

/* ---------------- Toast ---------------- */
let toastTimer = null;
function toast(message, type = '') {
  const el = $('#toast');
  el.textContent = message;
  el.className = 'toast show ' + type;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.className = 'toast'; }, 2600);
}

/* ---------------- Sidebar / navigation ---------------- */
const sidebar = $('#sidebar');
const overlay = $('#overlay');

function openSidebar() { sidebar.classList.add('open'); overlay.classList.add('show'); }
function closeSidebar() { sidebar.classList.remove('open'); overlay.classList.remove('show'); }

$('#menuBtn').addEventListener('click', () => {
  sidebar.classList.contains('open') ? closeSidebar() : openSidebar();
});
overlay.addEventListener('click', closeSidebar);

$$('.nav-item').forEach((item) => {
  item.addEventListener('click', () => {
    $$('.nav-item').forEach((i) => i.classList.toggle('active', i === item));
    $$('.panel').forEach((p) => p.classList.toggle('active', p.id === 'panel-' + item.dataset.panel));
    $('#pageTitle').textContent = item.dataset.title;
    $('#pageSub').textContent = item.dataset.sub;
    closeSidebar();
  });
});

/* ---------------- Mode badge ---------------- */
function setMode(mode) {
  const label = MODE_LABEL[mode] || '待機';
  const badge = $('#topMode');
  badge.textContent = label;
  badge.className = 'mode-badge ' + (MODE_CLASS[mode] || '');
  $('#modePill').textContent = label;
  $('#joyMode').textContent = label;
  $('#motionModeText').textContent = label;
}

/* ---------------- Connection state ---------------- */
let online = null;
function setOnline(state) {
  if (online === state) return;
  const first = (online === null);
  online = state;
  $('#connDot').className = 'dot ' + (state ? 'online' : 'offline');
  $('#connText').textContent = state ? '裝置在線' : '無法連線';
  if (!state && !first) toast('與裝置的連線中斷', 'error');
}

/* ---------------- Status ---------------- */
let servoSynced = false;

function renderStatus(s) {
  const wifi = s.wifi || {};

  $('#wifiModePill').textContent =
    wifi.sta && wifi.ap ? 'STA + AP' :
    wifi.sta ? 'STA 模式' :
    wifi.ap ? 'AP 模式' : '未連線';
  $('#stSsid').textContent = wifi.ssid || '—';
  $('#stIp').textContent = wifi.ip || '—';
  $('#stMac').textContent = wifi.mac || '—';
  $('#stAp').textContent = wifi.ap
    ? (wifi.apSsid || '—') + (wifi.apIp ? ' · ' + wifi.apIp : '')
    : '—';
  $('#stRssi').textContent = wifi.sta ? (wifi.rssi + ' dBm') : '—';

  const level = wifi.sta ? signalLevel(wifi.rssi) : 0;
  $$('#signalBars i').forEach((bar, i) => bar.classList.toggle('on', i < level));

  $('#stFw').textContent = (s.name || 'DogBot') + ' v' + (s.fw || '—');
  $('#fwVer').textContent = 'v' + (s.fw || '—');
  $('#stUptime').textContent = fmtUptime(s.uptime || 0);
  $('#stHeap').textContent = fmtBytes(s.heap);

  const fs = s.fs || {};
  const used = fs.used || 0;
  const total = fs.total || 1;
  const pct = Math.min(100, (used / total) * 100);
  $('#fsBar').style.width = pct.toFixed(1) + '%';
  $('#stFs').textContent = pct.toFixed(0) + '% 已使用';
  $('#fsUsed').textContent = fmtBytes(used) + ' 已使用';
  $('#fsTotal').textContent = fmtBytes(total) + ' 總容量 · ' + fmtBytes(fs.free) + ' 可用';

  renderFileTable(fs.files || []);

  // 尚未連上家用 WiFi 時顯示 AP 連線提示（AP 熱點常駐）
  const banner = $('#apBanner');
  if (!wifi.sta && wifi.ap) {
    banner.classList.remove('hidden');
    banner.innerHTML =
      '<strong>尚未連線家用 WiFi</strong>：AP 熱點常駐，請連線到 WiFi <strong>' +
      esc(wifi.apSsid) + '</strong>（密碼 <strong>12345678</strong>），' +
      '再開啟 <strong>http://' + esc(wifi.apIp || '192.168.4.1') + '</strong> 設定網路。';
  } else {
    banner.classList.add('hidden');
  }

  setMode(s.dogMode);

  if (!servoSynced && s.servo && s.servo.angles) {
    setServoValues(s.servo.angles);
    setCalibValues(s.servo);
    servoSynced = true;
  }
}

function renderFileTable(files) {
  const body = $('#fileRows');
  if (!files.length) {
    body.innerHTML = '<tr><td colspan="2" class="empty">SPIFFS 內尚無檔案，請先上傳 data/ 資料夾</td></tr>';
    return;
  }
  files.sort((a, b) => a.path.localeCompare(b.path));
  body.innerHTML = files.map((f) =>
    '<tr><td>' + esc(f.path) + '</td><td class="num">' + fmtBytes(f.size) + '</td></tr>'
  ).join('');
}

async function refreshStatus(silent) {
  try {
    const s = await api('/api/status');
    setOnline(true);
    renderStatus(s);
    return s;
  } catch (err) {
    setOnline(false);
    if (!silent) toast(err.message || '無法取得狀態', 'error');
    return null;
  }
}

$('#refreshBtn').addEventListener('click', async () => {
  await refreshStatus(true);
  toast('狀態已更新', 'success');
});

/* ---------------- WiFi ---------------- */
const scanList = $('#scanList');

$('#scanBtn').addEventListener('click', async () => {
  const btn = $('#scanBtn');
  btn.disabled = true;
  btn.textContent = '掃描中…';
  scanList.innerHTML = '<p class="empty">掃描中，請稍候…</p>';
  try {
    const r = await api('/api/wifi/scan');
    renderNetworks(r.networks || []);
  } catch (err) {
    scanList.innerHTML = '<p class="empty">掃描失敗：' + esc(err.message) + '</p>';
  } finally {
    btn.disabled = false;
    btn.textContent = '掃描網路';
  }
});

function renderNetworks(list) {
  if (!list.length) {
    scanList.innerHTML = '<p class="empty">沒有找到可用的網路</p>';
    return;
  }
  list.sort((a, b) => b.rssi - a.rssi);
  scanList.innerHTML = list.map((n) => {
    const lvl = signalLevel(n.rssi);
    const bars = [1, 2, 3, 4].map((i) => `<i class="${i <= lvl ? 'on' : ''}"></i>`).join('');
    return (
      `<button type="button" class="scan-item" data-ssid="${esc(n.ssid)}">` +
      `<span class="ssid">${esc(n.ssid)}</span>` +
      (n.secure ? '<span class="lock">🔒</span>' : '') +
      `<span class="signal">${bars}</span>` +
      `<span class="rssi">${n.rssi} dBm</span>` +
      '</button>'
    );
  }).join('');

  $$('.scan-item', scanList).forEach((item) => {
    item.addEventListener('click', () => {
      $('#ssidInput').value = item.dataset.ssid;
      $('#passInput').focus();
      toast('已帶入 SSID：' + item.dataset.ssid);
    });
  });
}

$('#wifiForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const ssid = $('#ssidInput').value.trim();
  const pass = $('#passInput').value;
  if (!ssid) { toast('請輸入 SSID', 'error'); return; }
  try {
    const r = await api('/api/wifi', { ssid, pass });
    toast(r.message || '設定已儲存', 'success');
    setTimeout(pollAfterRestart, 4500);
  } catch (err) {
    toast(err.message, 'error');
  }
});

$('#togglePass').addEventListener('click', () => {
  const input = $('#passInput');
  const show = input.type === 'password';
  input.type = show ? 'text' : 'password';
  $('#togglePass').textContent = show ? '隱藏' : '顯示';
});

$('#clearWifiBtn').addEventListener('click', async () => {
  if (!confirm('確定要清除 WiFi 連線設定嗎？裝置將重新啟動，AP 熱點仍可使用。')) return;
  try {
    const r = await api('/api/wifi/clear', {});
    toast(r.message || '已清除設定', 'success');
    setTimeout(pollAfterRestart, 4500);
  } catch (err) {
    toast(err.message, 'error');
  }
});

function pollAfterRestart() {
  let tries = 0;
  const timer = setInterval(async () => {
    tries++;
    const s = await refreshStatus(true);
    if (s) {
      clearInterval(timer);
      toast('裝置已重新啟動', 'success');
    } else if (tries > 20) {
      clearInterval(timer);
    }
  }, 2000);
}

/* ---------------- Joystick ---------------- */
const joypad = $('#joystick');
const joystickKnob = $('#joyKnob');
const joyDir = $('#joyDir');
const joySpeed = $('#joySpeed');

let joyVec = { x: 0, y: 0 };
let joyPointer = null;
let joySendAt = 0;
let joySending = false;
let joyDirty = false;

function joyRadius() {
  return joypad.clientWidth / 2 - joystickKnob.offsetWidth / 2 - 4;
}

function placeKnob(px, py) {
  joystickKnob.style.transform = `translate(calc(-50% + ${px}px), calc(-50% + ${py}px))`;
}

function pointerPos(e) {
  const r = joypad.getBoundingClientRect();
  return { x: e.clientX - r.left - r.width / 2, y: e.clientY - r.top - r.height / 2 };
}

function updateJoyVector(px, py) {
  const radius = joyRadius();
  const dist = Math.hypot(px, py);
  const k = dist > radius ? radius / dist : 1;
  const x = px * k;
  const y = py * k;
  placeKnob(x, y);
  joyVec.x = +(x / radius).toFixed(3);
  joyVec.y = +(-y / radius).toFixed(3);
  updateJoyReadout();
}

function updateJoyReadout() {
  const { x, y } = joyVec;
  const mag = Math.min(1, Math.hypot(x, y));
  let dir = '停止';
  if (mag > 0.12) {
    const ang = Math.atan2(x, y) * 180 / Math.PI;
    if (ang > -22.5 && ang <= 22.5) dir = '前進';
    else if (ang > 22.5 && ang <= 67.5) dir = '右前';
    else if (ang > 67.5 && ang <= 112.5) dir = '右轉';
    else if (ang > 112.5 && ang <= 157.5) dir = '右後';
    else if (ang > 157.5 || ang <= -157.5) dir = '後退';
    else if (ang > -157.5 && ang <= -112.5) dir = '左後';
    else if (ang > -112.5 && ang <= -67.5) dir = '左轉';
    else dir = '左前';
  }
  joyDir.textContent = dir;
  joySpeed.textContent = Math.round(mag * 100) + '%';
}

async function sendMove(force) {
  const now = performance.now();
  if (!force && now - joySendAt < 90) return;
  joySendAt = now;
  if (joySending) { joyDirty = true; return; }
  joySending = true;
  try {
    const r = await api('/api/move', { x: joyVec.x, y: joyVec.y });
    if (r && r.mode) setMode(r.mode);
  } catch (_) { /* ignore transient */ } finally {
    joySending = false;
    if (joyDirty) {
      joyDirty = false;
      setTimeout(() => sendMove(true), 30);
    }
  }
}

joypad.addEventListener('pointerdown', (e) => {
  joyPointer = e.pointerId;
  joypad.setPointerCapture(e.pointerId);
  const p = pointerPos(e);
  updateJoyVector(p.x, p.y);
  sendMove(true);
});

joypad.addEventListener('pointermove', (e) => {
  if (joyPointer !== e.pointerId) return;
  const p = pointerPos(e);
  updateJoyVector(p.x, p.y);
  sendMove(false);
});

function endJoy(e) {
  if (joyPointer !== e.pointerId) return;
  joyPointer = null;
  joyVec = { x: 0, y: 0 };
  placeKnob(0, 0);
  updateJoyReadout();
  sendMove(true);
}
joypad.addEventListener('pointerup', endJoy);
joypad.addEventListener('pointercancel', endJoy);

/* 鍵盤方向鍵控制 */
const keyState = new Set();
let keyTimer = null;

function keyVec() {
  const x = (keyState.has('ArrowRight') ? 1 : 0) - (keyState.has('ArrowLeft') ? 1 : 0);
  const y = (keyState.has('ArrowUp') ? 1 : 0) - (keyState.has('ArrowDown') ? 1 : 0);
  return { x, y };
}

document.addEventListener('keydown', (e) => {
  if (e.target.matches('input, textarea, select')) return;
  if (!['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'].includes(e.key)) return;
  e.preventDefault();
  keyState.add(e.key);
  const v = keyVec();
  if (!v.x && !v.y) return;
  const radius = joyRadius();
  updateJoyVector(v.x * radius * 0.85, -v.y * radius * 0.85);
  if (!keyTimer) keyTimer = setInterval(() => sendMove(true), 130);
  sendMove(true);
});

document.addEventListener('keyup', (e) => {
  if (!keyState.has(e.key)) return;
  keyState.delete(e.key);
  const v = keyVec();
  if (v.x || v.y) return;
  clearInterval(keyTimer);
  keyTimer = null;
  joyVec = { x: 0, y: 0 };
  placeKnob(0, 0);
  updateJoyReadout();
  sendMove(true);
});

$('#stopBtn').addEventListener('click', async () => {
  joyVec = { x: 0, y: 0 };
  placeKnob(0, 0);
  updateJoyReadout();
  try {
    const r = await api('/api/action', { action: 'stop' });
    setMode(r.mode);
    toast('已停止', 'success');
  } catch (err) {
    toast(err.message, 'error');
  }
});

/* ---------------- Special actions ---------------- */
$$('.action-btn').forEach((btn) => {
  btn.addEventListener('click', async () => {
    btn.classList.add('busy');
    try {
      const r = await api('/api/action', { action: btn.dataset.action });
      setMode(r.mode);
      toast(btn.querySelector('span:last-child').textContent + ' · ' + (MODE_LABEL[r.mode] || '執行中'), 'success');
    } catch (err) {
      toast(err.message, 'error');
    } finally {
      setTimeout(() => btn.classList.remove('busy'), 320);
    }
  });
});

$('#releaseManual').addEventListener('click', async () => {
  try {
    const r = await api('/api/action', { action: 'stand' });
    setMode(r.mode);
    toast('回到待機', 'success');
  } catch (err) {
    toast(err.message, 'error');
  }
});

/* ---------------- SG90 sliders ---------------- */
const servoPending = {};
const servoTimers = {};

function paintServo(input) {
  const pct = (input.value / 180) * 100;
  input.style.setProperty('--val', pct + '%');
  const out = $('#srv' + input.dataset.ch + 'Val');
  if (out) out.textContent = input.value + '°';
}

function setServoValues(angles) {
  Object.keys(servoTimers).forEach((ch) => {
    clearTimeout(servoTimers[ch]);
    delete servoTimers[ch];
    delete servoPending[ch];
  });
  angles.forEach((angle, ch) => {
    const input = $('#srv' + ch);
    if (!input) return;
    input.value = angle;
    paintServo(input);
  });
}

function setCalibValues(servo) {
  if (!servo || !servo.trim || !servo.rev) return;
  servo.trim.forEach((trim, ch) => {
    const el = $('#trim' + ch);
    if (el && document.activeElement !== el) el.value = trim;
  });
  servo.rev.forEach((rev, ch) => {
    const el = $('#rev' + ch);
    if (el) el.checked = !!rev;
  });
}

$$('input[type="range"][data-ch]').forEach((input) => {
  paintServo(input);
  input.addEventListener('input', () => {
    paintServo(input);
    const ch = input.dataset.ch;
    servoPending[ch] = input.value;
    clearTimeout(servoTimers[ch]);
    servoTimers[ch] = setTimeout(async () => {
      const angle = servoPending[ch];
      delete servoPending[ch];
      try {
        const r = await api('/api/servo', { ch, angle });
        if (r && r.mode) setMode(r.mode);
      } catch (_) { /* ignore */ }
    }, 90);
  });
});

/* 校正微調 / 反轉 */
const calibTimers = {};

function sendCalib(ch) {
  clearTimeout(calibTimers[ch]);
  calibTimers[ch] = setTimeout(async () => {
    const trimInput = $('#trim' + ch);
    const revInput = $('#rev' + ch);
    if (!trimInput || !revInput) return;
    const trim = parseInt(trimInput.value, 10) || 0;
    const rev = revInput.checked ? 1 : 0;
    try {
      const r = await api('/api/servo/calib', { ch, trim, rev });
      if (r && r.mode) setMode(r.mode);
    } catch (err) {
      toast(err.message, 'error');
    }
  }, 120);
}

$$('input[data-trim]').forEach((input) => {
  input.addEventListener('input', () => sendCalib(input.dataset.trim));
  input.addEventListener('change', () => sendCalib(input.dataset.trim));
});

$$('input[data-rev]').forEach((input) => {
  input.addEventListener('change', () => sendCalib(input.dataset.rev));
});

$('#resetCalib').addEventListener('click', async () => {
  try {
    const r = await api('/api/servo/calib/reset', {});
    setMode(r.mode);
    setCalibValues(r);
    toast('校正已歸零', 'success');
  } catch (err) {
    toast(err.message, 'error');
  }
});

$('#centerServo').addEventListener('click', async () => {
  try {
    const r = await api('/api/servo/center', {});
    setMode(r.mode);
    setServoValues(r.angles);
    setCalibValues(r);
    toast('全部置中 90°', 'success');
  } catch (err) {
    toast(err.message, 'error');
  }
});

$('#syncServo').addEventListener('click', async () => {
  try {
    const r = await api('/api/servo', {});
    setMode(r.mode);
    setServoValues(r.angles);
    setCalibValues(r);
    toast('已同步目前角度', 'success');
  } catch (err) {
    toast('同步失敗', 'error');
  }
});

/* ---------------- OTA ---------------- */
const dropZone = $('#dropZone');
const fwFile = $('#fwFile');
const fileName = $('#fileName');
const otaBtn = $('#otaBtn');
const otaWrap = $('#otaProgressWrap');
const otaBar = $('#otaBar');
const otaPct = $('#otaPct');
const otaStatus = $('#otaStatus');
const otaPill = $('#otaPill');
const otaDropTitle = $('#otaDropTitle');
const otaType = $('#otaType');

let selectedFile = null;
let otaTarget = 'fw';

function pickFile(file) {
  selectedFile = file || null;
  fileName.textContent = file ? file.name + ' · ' + fmtBytes(file.size) : '';
  otaBtn.disabled = !file;
  setOtaPill(file ? '已選擇' : '閒置');
}

function setOtaPill(text) { otaPill.textContent = text; }

$$('.seg-btn', otaType).forEach((btn) => {
  btn.addEventListener('click', () => {
    if (otaTarget === btn.dataset.ota) return;
    otaTarget = btn.dataset.ota;
    $$('.seg-btn', otaType).forEach((b) => b.classList.toggle('active', b === btn));
    otaDropTitle.innerHTML = otaTarget === 'fs'
      ? '<strong>拖曳 spiffs.bin 到此</strong>'
      : '<strong>拖曳韌體 .bin 到此</strong>';
    pickFile(null);
  });
});

dropZone.addEventListener('click', () => fwFile.click());
dropZone.addEventListener('keydown', (e) => {
  if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); fwFile.click(); }
});
fwFile.addEventListener('change', () => pickFile(fwFile.files[0]));

['dragenter', 'dragover'].forEach((ev) =>
  dropZone.addEventListener(ev, (e) => { e.preventDefault(); dropZone.classList.add('dragover'); }));
['dragleave', 'drop'].forEach((ev) =>
  dropZone.addEventListener(ev, (e) => { e.preventDefault(); dropZone.classList.remove('dragover'); }));
dropZone.addEventListener('drop', (e) => {
  const file = e.dataTransfer.files[0];
  if (file) pickFile(file);
});

otaBtn.addEventListener('click', () => {
  if (selectedFile) startOta(selectedFile);
});

function startOta(file) {
  const isFs = otaTarget === 'fs';

  otaBtn.disabled = true;
  otaWrap.classList.remove('hidden');
  otaBar.style.width = '0%';
  otaPct.textContent = '0%';
  otaStatus.textContent = isFs ? '上傳 SPIFFS 映像中…' : '上傳韌體中…';
  setOtaPill('更新中');

  const fd = new FormData();
  fd.append(isFs ? 'spiffs' : 'firmware', file, file.name);

  const xhr = new XMLHttpRequest();
  xhr.open('POST', isFs ? '/api/ota/fs' : '/api/ota');

  xhr.upload.onprogress = (e) => {
    if (!e.lengthComputable) return;
    const pct = Math.round((e.loaded / e.total) * 100);
    otaBar.style.width = pct + '%';
    otaPct.textContent = pct + '%';
  };

  xhr.onload = () => {
    let res = {};
    try { res = JSON.parse(xhr.responseText); } catch (_) { /* ignore */ }
    if (xhr.status >= 200 && xhr.status < 300 && res.ok) {
      otaStatus.textContent = '完成，裝置重新啟動中…';
      setOtaPill('完成');
      toast(res.message || 'OTA 更新成功', 'success');
      setTimeout(pollAfterRestart, 3500);
    } else {
      otaStatus.textContent = res.error || '更新失敗';
      setOtaPill('失敗');
      toast(res.error || 'OTA 更新失敗', 'error');
      otaBtn.disabled = false;
    }
  };

  xhr.onerror = () => {
    otaStatus.textContent = '連線中斷';
    setOtaPill('失敗');
    toast('上傳連線中斷', 'error');
    otaBtn.disabled = false;
  };

  xhr.send(fd);
}

/* ---------------- Init ---------------- */
setMode('stand');
refreshStatus(true);
setInterval(() => { if (!document.hidden) refreshStatus(true); }, 5000);
