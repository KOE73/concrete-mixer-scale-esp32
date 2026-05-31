async function tick() {
  const data = await window.MixerScaleCbor.fetchState();
  const primary = data.ma.find((item) => item.name === 'ma_3s') || data.ma.find((item) => item.name.startsWith('ma_')) || data.ma[0];

  document.getElementById('weight').textContent =
    primary && primary.valid ? primary.weight.toFixed(2) : '--';
  document.getElementById('stage').textContent = data.target.stage;
  document.getElementById('remaining').textContent = data.target.remaining.toFixed(2);
  document.getElementById('shovels').textContent = data.target.remainingShovels.toFixed(1);
}

async function loadWifi() {
  const response = await fetch('/api/wifi');
  const data = await response.json();
  document.getElementById('apSsid').textContent = data.ap.started ? data.ap.ssid : '--';
  document.getElementById('staState').textContent =
    data.sta.connected ? data.sta.ssid : (data.sta.configured ? `${data.sta.ssid}...` : 'не задана');
  document.getElementById('staIp').textContent = data.sta.connected ? data.sta.ip : '--';
  document.getElementById('apMac').textContent = data.ap.mac || '--';
  document.getElementById('staMac').textContent = data.sta.mac || '--';
  if (!document.getElementById('wifiSsid').value) {
    document.getElementById('wifiSsid').value = data.sta.ssid || '';
  }

  const savedDiv = document.getElementById('savedNetworks');
  const listEl = document.getElementById('networksList');
  if (savedDiv && listEl) {
    if (data.networks && data.networks.length > 0) {
      savedDiv.style.display = 'block';
      listEl.innerHTML = '';
      data.networks.forEach(net => {
        const li = document.createElement('li');
        li.style.padding = '8px 12px';
        li.style.margin = '6px 0';
        li.style.background = 'rgba(255, 255, 255, 0.05)';
        li.style.borderRadius = '6px';
        li.style.display = 'flex';
        li.style.justifyContent = 'space-between';
        li.style.alignItems = 'center';
        li.style.cursor = 'pointer';
        li.style.transition = 'background 0.2s';
        
        li.onmouseover = () => { li.style.background = 'rgba(255, 255, 255, 0.1)'; };
        li.onmouseout = () => { li.style.background = 'rgba(255, 255, 255, 0.05)'; };

        li.onclick = () => {
          document.getElementById('wifiSsid').value = net.ssid;
          document.getElementById('wifiPassword').focus();
        };

        const ssidSpan = document.createElement('span');
        ssidSpan.textContent = net.ssid;
        if (data.sta.ssid === net.ssid) {
          ssidSpan.style.fontWeight = 'bold';
          ssidSpan.style.color = '#38bdf8';
          ssidSpan.textContent += ' ★';
        }

        const passSpan = document.createElement('span');
        passSpan.style.fontSize = '0.85em';
        passSpan.style.color = '#94a3b8';
        passSpan.textContent = net.hasPassword ? '🔒 с паролем' : '🔓 открытая';

        li.appendChild(ssidSpan);
        li.appendChild(passSpan);
        listEl.appendChild(li);
      });
    } else {
      savedDiv.style.display = 'none';
    }
  }
}

async function loadUdpTelemetry() {
  const response = await fetch('/api/udp-telemetry');
  const data = await response.json();
  document.getElementById('udpScaleId').textContent = data.scaleId;
  document.getElementById('udpTarget').textContent = `${data.targetHost}:${data.port}`;
  document.getElementById('udpEnabled').textContent = data.enabled ? 'включено' : 'выключено';
  document.getElementById('udpScaleIdInput').value = data.scaleId;
  document.getElementById('udpTargetHost').value = data.targetHost;
  document.getElementById('udpPort').value = data.port;
  document.getElementById('udpEnabledInput').checked = data.enabled;
}

document.getElementById('wifiForm').addEventListener('submit', async (event) => {
  event.preventDefault();
  const message = document.getElementById('wifiMessage');
  message.textContent = 'сохранение...';
  const response = await fetch('/api/wifi', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      ssid: document.getElementById('wifiSsid').value.trim(),
      password: document.getElementById('wifiPassword').value
    })
  });
  message.textContent = response.ok ? 'сохранено' : 'ошибка';
  document.getElementById('wifiPassword').value = '';
  await loadWifi();
});

document.getElementById('udpForm').addEventListener('submit', async (event) => {
  event.preventDefault();
  const message = document.getElementById('udpMessage');
  message.textContent = 'сохранение...';
  const response = await fetch('/api/udp-telemetry', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      enabled: document.getElementById('udpEnabledInput').checked,
      scaleId: Number(document.getElementById('udpScaleIdInput').value || 1),
      targetHost: document.getElementById('udpTargetHost').value.trim() || '255.255.255.255',
      port: Number(document.getElementById('udpPort').value || 4222)
    })
  });
  message.textContent = response.ok ? 'сохранено' : 'ошибка';
  await loadUdpTelemetry();
});

document.getElementById('btnScan').addEventListener('click', async () => {
  const statusEl = document.getElementById('scanStatus');
  const resultsDiv = document.getElementById('scanResults');
  const listEl = document.getElementById('scanList');
  
  if (!statusEl || !resultsDiv || !listEl) return;
  
  statusEl.textContent = 'сканирование...';
  resultsDiv.style.display = 'none';
  listEl.innerHTML = '';
  
  try {
    const response = await fetch('/api/wifi-scan');
    if (!response.ok) {
      throw new Error('Scan failed');
    }
    const networks = await response.json();
    
    if (networks && networks.length > 0) {
      statusEl.textContent = `найдено сетей: ${networks.length}`;
      resultsDiv.style.display = 'block';
      
      networks.forEach(net => {
        const li = document.createElement('li');
        li.style.padding = '8px 12px';
        li.style.margin = '4px 0';
        li.style.background = 'rgba(255, 255, 255, 0.05)';
        li.style.borderRadius = '4px';
        li.style.display = 'flex';
        li.style.justifyContent = 'space-between';
        li.style.alignItems = 'center';
        li.style.cursor = 'pointer';
        li.style.transition = 'background 0.2s';
        
        li.onmouseover = () => { li.style.background = 'rgba(255, 255, 255, 0.1)'; };
        li.onmouseout = () => { li.style.background = 'rgba(255, 255, 255, 0.05)'; };
        
        li.onclick = () => {
          document.getElementById('wifiSsid').value = net.ssid;
          document.getElementById('wifiPassword').focus();
          resultsDiv.style.display = 'none';
          statusEl.textContent = '';
        };
        
        const ssidSpan = document.createElement('span');
        ssidSpan.textContent = net.ssid;
        
        const infoSpan = document.createElement('span');
        infoSpan.style.fontSize = '0.85em';
        infoSpan.style.color = '#94a3b8';
        
        const lockStr = net.secure ? '🔒' : '🔓';
        const signalStr = getSignalIcon(net.rssi);
        infoSpan.textContent = `${lockStr} ${net.rssi} dBm (${signalStr})`;
        
        li.appendChild(ssidSpan);
        li.appendChild(infoSpan);
        listEl.appendChild(li);
      });
    } else {
      statusEl.textContent = 'сетей не найдено';
    }
  } catch (err) {
    statusEl.textContent = 'ошибка сканирования';
    console.error(err);
  }
});

function getSignalIcon(rssi) {
  if (rssi >= -50) return '⚡';
  if (rssi >= -70) return '📶';
  if (rssi >= -85) return '░';
  return '❗';
}

setInterval(tick, 500);
setInterval(loadWifi, 3000);
setInterval(loadUdpTelemetry, 3000);
tick();
loadWifi();
loadUdpTelemetry();
