const histories = {
  raw: [],
  ma_1s: [],
  ma_3s: [],
  ma_5s: [],
  ma_10s: []
};
const channelHistory = new Map();
const maxHistory = 90;
let latestWeightData = null;
let settingsData = null;

function formatNumber(value, digits = 2) {
  return Number.isFinite(value) ? value.toFixed(digits) : '--';
}

function primaryFilter(data) {
  return data.filters.find((item) => item.name === 'ma_3s') || data.filters.find((item) => item.name === 'moving_average') || data.filters[0] || null;
}

function svgIdForChannel(name) {
  return `svg-${name.replace(/_/g, '-')}`;
}

function showCalibrationMessage(message) {
  document.getElementById('calibrationMessage').textContent = message;
}

async function loadSettings() {
  const response = await fetch('/api/settings');
  settingsData = await response.json();
  renderCalibration();
}

function renderCalibration() {
  if (!settingsData) {
    return;
  }

  document.getElementById('globalScale').value = settingsData.globalScale ?? 1;
  document.getElementById('calibration').innerHTML = settingsData.channels.map((channel) => `
    <label>${channel.name}
      <input
        type="number"
        step="0.000001"
        value="${channel.scale ?? 1}"
        data-scale-index="${channel.index}">
    </label>
  `).join('');
}

function readSettingsFromForm() {
  const next = {
    globalScale: Number.parseFloat(document.getElementById('globalScale').value) || 1,
    channels: settingsData.channels.map((channel) => ({ ...channel })),
  };

  document.querySelectorAll('[data-scale-index]').forEach((input) => {
    const index = Number.parseInt(input.dataset.scaleIndex, 10);
    const channel = next.channels.find((item) => item.index === index);
    if (channel) {
      channel.scale = Number.parseFloat(input.value) || 1;
    }
  });

  return next;
}

async function saveSettings(nextSettings) {
  const response = await fetch('/api/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(nextSettings),
  });
  if (!response.ok) {
    throw new Error(await response.text());
  }
  settingsData = await response.json();
  renderCalibration();
}

function drawChart(targetWeight) {
  const canvas = document.getElementById('weightChart');
  const ctx = canvas.getContext('2d');
  const width = canvas.width;
  const height = canvas.height;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = '#111';
  ctx.fillRect(0, 0, width, height);

  const allValues = [targetWeight || 0];
  Object.values(histories).forEach((history) => {
    history.forEach((val) => {
      if (Number.isFinite(val)) {
        allValues.push(val);
      }
    });
  });

  const maxValue = Math.max(...allValues, 1);
  const minValue = Math.min(0, ...allValues);
  const range = Math.max(maxValue - minValue, 1);

  if (targetWeight > 0) {
    const y = height - ((targetWeight - minValue) / range) * height;
    ctx.strokeStyle = '#333';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
    ctx.stroke();
  }

  const configs = [
    { key: 'raw', color: '#666', label: 'Raw', width: 1.5 },
    { key: 'ma_1s', color: '#36a2eb', label: 'MA 1s', width: 2 },
    { key: 'ma_3s', color: '#ff6384', label: 'MA 3s', width: 2 },
    { key: 'ma_5s', color: '#ff9f40', label: 'MA 5s', width: 2 },
    { key: 'ma_10s', color: '#4bc0c0', label: 'MA 10s', width: 2 }
  ];

  configs.forEach((cfg) => {
    const history = histories[cfg.key];
    if (history.length < 2) {
      return;
    }
    ctx.strokeStyle = cfg.color;
    ctx.lineWidth = cfg.width;
    ctx.beginPath();
    let first = true;
    history.forEach((value, index) => {
      if (value === null || !Number.isFinite(value)) {
        return;
      }
      const x = (index / (maxHistory - 1)) * width;
      const y = height - ((value - minValue) / range) * height;
      if (first) {
        ctx.moveTo(x, y);
        first = false;
      } else {
        ctx.lineTo(x, y);
      }
    });
    ctx.stroke();
  });

  ctx.fillStyle = 'rgba(0, 0, 0, 0.7)';
  ctx.fillRect(5, 5, 95, configs.length * 15 + 5);
  ctx.strokeStyle = '#444';
  ctx.lineWidth = 1;
  ctx.strokeRect(5, 5, 95, configs.length * 15 + 5);

  ctx.font = '10px sans-serif';
  ctx.textBaseline = 'middle';
  configs.forEach((cfg, idx) => {
    const y = 15 + idx * 15;
    ctx.fillStyle = cfg.color;
    ctx.fillRect(10, y - 4, 12, 8);
    ctx.fillStyle = '#fff';
    ctx.fillText(cfg.label, 28, y);
  });
}

async function updateProcess() {
  const response = await fetch('/api/weight');
  const data = await response.json();
  latestWeightData = data;
  const primary = primaryFilter(data);
  const weight = primary && primary.valid ? primary.weight : (data.valid ? data.weight : null);

  const rawWeight = data.valid ? data.weight : null;
  histories.raw.push(rawWeight);
  while (histories.raw.length > maxHistory) {
    histories.raw.shift();
  }

  ['ma_1s', 'ma_3s', 'ma_5s', 'ma_10s'].forEach((key) => {
    const filter = data.filters.find((f) => f.name === key);
    const val = filter && filter.valid ? filter.weight : null;
    histories[key].push(val);
    while (histories[key].length > maxHistory) {
      histories[key].shift();
    }
  });

  document.getElementById('stage').textContent = data.target.stage;
  document.getElementById('weight').textContent = formatNumber(weight);
  document.getElementById('target').textContent = formatNumber(data.target.weight);
  document.getElementById('remaining').textContent = formatNumber(data.target.remaining);
  document.getElementById('shovels').textContent = formatNumber(data.target.remainingShovels, 1);
  document.getElementById('sample').textContent = `${data.sequence} ${data.valid ? 'valid' : 'invalid'}`;

  data.channels.forEach((channel) => {
    const samples = channelHistory.get(channel.name) || [];
    if (Number.isFinite(channel.raw)) {
      samples.push(channel.raw);
      while (samples.length > 24) {
        samples.shift();
      }
    }
    channelHistory.set(channel.name, samples);
  });

  document.getElementById('channels').innerHTML = data.channels.map((channel) =>
    `<tr><td>${channel.name}</td><td>${channel.ready ? 'yes' : 'no'}</td><td>${channel.raw}</td><td>${adcActivity(channel.name)}</td><td>${formatNumber(channel.weight)}</td></tr>`
  ).join('');

  data.channels.forEach((channel) => {
    const svgValue = document.getElementById(svgIdForChannel(channel.name));
    if (svgValue) {
      svgValue.textContent = `${formatNumber(channel.weight)} kg`;
    }
  });

  document.getElementById('filters').innerHTML = data.filters.map((filter) =>
    `<tr><td>${filter.name}</td><td>${filter.valid ? 'yes' : 'no'}</td><td>${formatNumber(filter.weight)}</td></tr>`
  ).join('');

  drawChart(data.target.weight);
}

setInterval(updateProcess, 500);
updateProcess();
loadSettings().catch((error) => showCalibrationMessage(error.message));

document.getElementById('saveCalibration').addEventListener('click', async () => {
  try {
    if (!settingsData) {
      await loadSettings();
    }
    await saveSettings(readSettingsFromForm());
    showCalibrationMessage('Saved');
  } catch (error) {
    showCalibrationMessage(error.message);
  }
});

document.getElementById('zeroOffset').addEventListener('click', async () => {
  try {
    if (!settingsData) {
      await loadSettings();
    }
    if (!latestWeightData) {
      throw new Error('No sample yet');
    }

    const next = readSettingsFromForm();
    next.channels = next.channels.map((channel) => {
      const live = latestWeightData.channels.find((item) => item.index === channel.index);
      return live ? { ...channel, offset: live.raw } : channel;
    });

    await saveSettings(next);
    showCalibrationMessage('Zero offsets saved');
  } catch (error) {
    showCalibrationMessage(error.message);
  }
});

function adcActivity(name) {
  const samples = channelHistory.get(name) || [];
  if (samples.length < 2) {
    return '<span class="adc-idle"></span>';
  }

  const min = Math.min(...samples);
  const max = Math.max(...samples);
  const range = Math.max(max - min, 1);
  return `<span class="adc-bars">${samples.slice(-12).map((value) => {
    const height = Math.max(2, Math.round(((value - min) / range) * 18));
    return `<i style="height:${height}px"></i>`;
  }).join('')}</span>`;
}
