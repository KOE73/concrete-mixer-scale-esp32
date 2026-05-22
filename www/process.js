const histories = {
  raw: [],
  ma_1s: [],
  ma_3s: [],
  ma_5s: [],
  ma_10s: []
};
const maxHistory = 90;
let latestWeightData = null;
let settingsData = null;

function formatNumber(value, digits = 2) {
  return Number.isFinite(value) ? value.toFixed(digits) : '--';
}

function primaryFilter(data) {
  return data.ma.find((item) => item.name === 'ma_3s') || data.ma.find((item) => item.name === 'moving_average') || data.ma[0] || null;
}

function showCalibrationMessage(message) {
  document.getElementById('calibrationMessage').textContent = message;
}

function escapeHtml(value) {
  return String(value ?? '').replace(/[&<>"']/g, (ch) => ({
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
    "'": '&#39;',
  }[ch]));
}

function isEnglishLetters(value) {
  return /^[A-Za-z]+$/.test(value);
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

  document.getElementById('sumOffset').value = settingsData.sumOffset ?? 0;
  document.getElementById('sumScale').value = settingsData.sumScale ?? 1;
  document.getElementById('calibration').textContent = '';
  renderUnitsEditor();
  renderSetpointsEditor();
}

function readSettingsFromForm() {
  return {
    sumOffset: Number.parseInt(document.getElementById('sumOffset').value || '0', 10),
    sumScale: Number.parseFloat(document.getElementById('sumScale').value) || 1,
    units: settingsData?.units ?? [],
    setpoints: settingsData?.setpoints ?? [],
  };
}

function renderUnitsEditor() {
  const units = settingsData?.units ?? [];
  document.getElementById('unitsEditor').innerHTML = units.map((unit, index) => `
    <tr>
      <td><input class="compact-input" data-unit-name="${index}" maxlength="5" value="${escapeHtml(unit.name)}"></td>
      <td><input class="compact-input" data-unit-raw="${index}" type="number" step="0.001" value="${Number(unit.rawPerUnit) || 0}"></td>
      <td>
        <button class="compact-button" data-save-unit="${index}" type="button">Save</button>
        <button class="compact-button" data-delete-unit="${index}" type="button">Delete</button>
      </td>
    </tr>
  `).join('');
}

function renderSetpointsEditor() {
  const setpoints = settingsData?.setpoints ?? [];
  document.getElementById('setpointsEditor').innerHTML = setpoints.map((setpoint, index) => `
    <tr>
      <td><input class="compact-input" data-setpoint-name="${index}" maxlength="24" value="${escapeHtml(setpoint.name)}"></td>
      <td><input class="compact-input" data-setpoint-raw="${index}" type="number" step="1" value="${Number(setpoint.rawValue) || 0}"></td>
      <td>
        <button class="compact-button" data-save-setpoint="${index}" type="button">Save</button>
        <button class="compact-button" data-delete-setpoint="${index}" type="button">Delete</button>
      </td>
    </tr>
  `).join('');
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
  const data = await window.MixerScaleCbor.fetchState();
  latestWeightData = data;
  const primary = primaryFilter(data);
  const weight = primary && primary.valid ? primary.weight : (data.valid ? data.weight : null);

  const rawWeight = data.valid ? data.weight : null;
  histories.raw.push(rawWeight);
  while (histories.raw.length > maxHistory) {
    histories.raw.shift();
  }

  ['ma_1s', 'ma_3s', 'ma_5s', 'ma_10s'].forEach((key) => {
    const filter = data.ma.find((f) => f.name === key);
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

  document.getElementById('filters').innerHTML = data.ma.map((filter) =>
    `<tr><td>${filter.name}</td><td>${filter.valid ? 'yes' : 'no'}</td><td>${filter.rawSum}</td><td>${formatNumber(filter.weight)}</td></tr>`
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
document.getElementById('addUnit').addEventListener('click', async () => {
  try {
    if (!settingsData) {
      await loadSettings();
    }
    const name = document.getElementById('newUnitName').value.trim();
    const rawPerUnit = Number.parseFloat(document.getElementById('newUnitRaw').value);
    if (!isEnglishLetters(name)) {
      throw new Error('Unit must contain English letters only');
    }
    if (!Number.isFinite(rawPerUnit) || rawPerUnit <= 0) {
      throw new Error('Raw/unit must be positive');
    }
    const next = readSettingsFromForm();
    next.units = [...(settingsData.units ?? []), { name: name.slice(0, 5), rawPerUnit }];
    await saveSettings(next);
    document.getElementById('newUnitName').value = '';
    document.getElementById('newUnitRaw').value = '';
    showCalibrationMessage('Unit saved');
  } catch (error) {
    showCalibrationMessage(error.message);
  }
});
document.getElementById('unitsEditor').addEventListener('click', async (event) => {
  const saveIndex = event.target.dataset.saveUnit;
  const deleteIndex = event.target.dataset.deleteUnit;
  if (saveIndex === undefined && deleteIndex === undefined) {
    return;
  }
  try {
    const units = [...(settingsData?.units ?? [])];
    if (saveIndex !== undefined) {
      const index = Number(saveIndex);
      const name = document.querySelector(`[data-unit-name="${index}"]`).value.trim();
      const rawPerUnit = Number.parseFloat(document.querySelector(`[data-unit-raw="${index}"]`).value);
      if (!isEnglishLetters(name)) {
        throw new Error('Unit must contain English letters only');
      }
      if (!Number.isFinite(rawPerUnit) || rawPerUnit <= 0) {
        throw new Error('Raw/unit must be positive');
      }
      units[index] = { name: name.slice(0, 5), rawPerUnit };
    } else {
      units.splice(Number(deleteIndex), 1);
    }
    const next = readSettingsFromForm();
    next.units = units;
    await saveSettings(next);
    showCalibrationMessage('Units saved');
  } catch (error) {
    showCalibrationMessage(error.message);
  }
});

document.getElementById('addSetpoint').addEventListener('click', async () => {
  try {
    if (!settingsData) {
      await loadSettings();
    }
    const name = document.getElementById('newSetpointName').value.trim();
    const rawValue = Number.parseInt(document.getElementById('newSetpointRaw').value || '0', 10);
    if (!name) {
      throw new Error('Setpoint name is required');
    }
    const next = readSettingsFromForm();
    next.setpoints = [...(settingsData.setpoints ?? []), { name: name.slice(0, 24), rawValue }];
    await saveSettings(next);
    document.getElementById('newSetpointName').value = '';
    document.getElementById('newSetpointRaw').value = '';
    showCalibrationMessage('Setpoint saved');
  } catch (error) {
    showCalibrationMessage(error.message);
  }
});

document.getElementById('setpointsEditor').addEventListener('click', async (event) => {
  const saveIndex = event.target.dataset.saveSetpoint;
  const deleteIndex = event.target.dataset.deleteSetpoint;
  if (saveIndex === undefined && deleteIndex === undefined) {
    return;
  }
  try {
    const setpoints = [...(settingsData?.setpoints ?? [])];
    if (saveIndex !== undefined) {
      const index = Number(saveIndex);
      const name = document.querySelector(`[data-setpoint-name="${index}"]`).value.trim();
      const rawValue = Number.parseInt(document.querySelector(`[data-setpoint-raw="${index}"]`).value || '0', 10);
      if (!name) {
        throw new Error('Setpoint name is required');
      }
      setpoints[index] = { name: name.slice(0, 24), rawValue };
    } else {
      setpoints.splice(Number(deleteIndex), 1);
    }
    const next = readSettingsFromForm();
    next.setpoints = setpoints;
    await saveSettings(next);
    showCalibrationMessage('Setpoints saved');
  } catch (error) {
    showCalibrationMessage(error.message);
  }
});
