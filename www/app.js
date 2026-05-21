async function tick() {
  const response = await fetch('/api/weight');
  const data = await response.json();
  const primary = data.filters.find((item) => item.name === 'moving_average') || data.filters[0];

  document.getElementById('weight').textContent =
    primary && primary.valid ? primary.weight.toFixed(2) : '--';
  document.getElementById('stage').textContent = data.target.stage;
  document.getElementById('remaining').textContent = data.target.remaining.toFixed(2);
  document.getElementById('shovels').textContent = data.target.remainingShovels.toFixed(1);
  document.getElementById('channels').innerHTML = data.channels.map((channel) =>
    `<tr><td>${channel.name}</td><td>${channel.raw}</td><td>${channel.weight.toFixed(2)}</td></tr>`
  ).join('');
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

setInterval(tick, 500);
setInterval(loadWifi, 3000);
setInterval(loadUdpTelemetry, 3000);
tick();
loadWifi();
loadUdpTelemetry();
