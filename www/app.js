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
  if (!document.getElementById('wifiSsid').value) {
    document.getElementById('wifiSsid').value = data.sta.ssid || '';
  }
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

setInterval(tick, 500);
setInterval(loadWifi, 3000);
tick();
loadWifi();
