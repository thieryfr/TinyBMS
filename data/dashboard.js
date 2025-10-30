/**
 * Dashboard Logic
 * Handles gauges, charts, and live data display
 */

// ============================================
// Global Variables
// ============================================

let gauges = {};
let historyChart = null;
let historyData = {
    voltage: [],
    current: [],
    soc: [],
    temperature: [],
    timestamps: []
};
const MAX_HISTORY_POINTS = 180; // 30 minutes at 10s intervals

// Long-term history storage (localStorage)
const HISTORY_STORAGE_KEY = 'tinybms_history_v1';
const MAX_HISTORY_AGE_MS = 7 * 24 * 60 * 60 * 1000; // 7 days
const HISTORY_SAVE_INTERVAL_MS = 60000; // Save every 60s
let lastHistorySave = Date.now();
let currentPeriod = '30m'; // '30m', '1h', '24h', '7d'

// Dashboard preferences storage
const DASHBOARD_PREFERENCES_STORAGE_KEY = 'tinybms_dashboard_prefs_v1';
const DASHBOARD_PREFERENCES_DEFAULT = Object.freeze({
    cellVoltage: {
        min_mv: 3000,
        max_mv: 3700,
        warning_delta_mv: 30,
        critical_delta_mv: 100
    },
    alerts: {
        soc_critical: 20,
        soc_low: 30,
        temp_warning: 45,
        temp_critical: 50,
        imbalance_warning: 150,
        imbalance_critical: 200,
        balancing_duration_warning_ms: 30 * 60 * 1000
    }
});

let dashboardPreferences = loadDashboardPreferences();
let lastLiveDataSnapshot = null;
let lastStatsSnapshot = null;
let lastCellVoltages = [];

// ============================================
// Long-term History Management
// ============================================

function loadHistoryFromStorage() {
    try {
        const stored = localStorage.getItem(HISTORY_STORAGE_KEY);
        if (!stored) return [];

        const history = JSON.parse(stored);

        // Clean old entries (> 7 days)
        const now = Date.now();
        const cleaned = history.filter(entry => {
            return (now - entry.timestamp) < MAX_HISTORY_AGE_MS;
        });

        // Save cleaned data back
        if (cleaned.length !== history.length) {
            localStorage.setItem(HISTORY_STORAGE_KEY, JSON.stringify(cleaned));
        }

        return cleaned;
    } catch (error) {
        console.error('[History] Failed to load from storage:', error);
        return [];
    }
}

function saveHistoryToStorage(dataPoint) {
    try {
        const history = loadHistoryFromStorage();
        history.push(dataPoint);

        // Keep only last 10,000 points (~11 days at 10s intervals)
        if (history.length > 10000) {
            history.shift();
        }

        localStorage.setItem(HISTORY_STORAGE_KEY, JSON.stringify(history));
    } catch (error) {
        console.error('[History] Failed to save to storage:', error);
        // If localStorage is full, remove old data and retry
        if (error.name === 'QuotaExceededError') {
            const history = loadHistoryFromStorage().slice(-5000);
            localStorage.setItem(HISTORY_STORAGE_KEY, JSON.stringify(history));
        }
    }
}

function getHistoryForPeriod(period) {
    const history = loadHistoryFromStorage();
    const now = Date.now();
    let maxAge;

    switch(period) {
        case '30m':
            maxAge = 30 * 60 * 1000;
            break;
        case '1h':
            maxAge = 60 * 60 * 1000;
            break;
        case '24h':
            maxAge = 24 * 60 * 60 * 1000;
            break;
        case '7d':
            maxAge = 7 * 24 * 60 * 60 * 1000;
            break;
        default:
            maxAge = 30 * 60 * 1000;
    }

    // Filter by period
    const filtered = history.filter(entry => {
        return (now - entry.timestamp) <= maxAge;
    });

    // Downsample if too many points
    return downsampleHistory(filtered, period);
}

function downsampleHistory(data, period) {
    // For longer periods, downsample to max 200 points
    const maxPoints = 200;

    if (data.length <= maxPoints) {
        return data;
    }

    const step = Math.ceil(data.length / maxPoints);
    const downsampled = [];

    for (let i = 0; i < data.length; i += step) {
        // Average the points in this bucket
        const bucket = data.slice(i, i + step);
        const avg = {
            timestamp: bucket[0].timestamp,
            voltage: bucket.reduce((sum, p) => sum + p.voltage, 0) / bucket.length,
            current: bucket.reduce((sum, p) => sum + p.current, 0) / bucket.length,
            soc: bucket.reduce((sum, p) => sum + p.soc, 0) / bucket.length,
            temperature: bucket.reduce((sum, p) => sum + p.temperature, 0) / bucket.length
        };
        downsampled.push(avg);
    }

    return downsampled;
}

function loadChartFromHistory(period) {
    const history = getHistoryForPeriod(period);

    if (history.length === 0) {
        showToast(`Pas de données pour la période ${period}`, 'info');
        return;
    }

    // Clear current in-memory history
    historyData.timestamps = [];
    historyData.voltage = [];
    historyData.current = [];
    historyData.soc = [];
    historyData.temperature = [];

    // Load from storage
    history.forEach(entry => {
        const date = new Date(entry.timestamp);
        const timeStr = period === '24h' || period === '7d'
            ? `${date.getMonth()+1}/${date.getDate()} ${date.getHours()}:${date.getMinutes().toString().padStart(2,'0')}`
            : date.toLocaleTimeString();

        historyData.timestamps.push(timeStr);
        historyData.voltage.push(entry.voltage);
        historyData.current.push(entry.current);
        historyData.soc.push(entry.soc);
        historyData.temperature.push(entry.temperature);
    });

    // Update chart with current selection
    const checkedRadio = document.querySelector('input[name="chartData"]:checked');
    if (checkedRadio) {
        const dataType = checkedRadio.id.replace('chart', '').toLowerCase();
        updateChartData(dataType);
    }

    showToast(`Chargé ${history.length} points (${period})`, 'success', 2000);
}

// ============================================
// Dashboard Preferences Helpers
// ============================================

function cloneDashboardPreferences(preferences) {
    return {
        cellVoltage: { ...preferences.cellVoltage },
        alerts: { ...preferences.alerts }
    };
}

function toNumber(value, fallback) {
    const numeric = Number(value);
    return Number.isFinite(numeric) ? numeric : fallback;
}

function mergeDashboardPreferences(base, updates = {}) {
    const merged = cloneDashboardPreferences(base);

    if (updates.cellVoltage) {
        const cell = updates.cellVoltage;
        if (cell.min_mv !== undefined) {
            merged.cellVoltage.min_mv = toNumber(cell.min_mv, merged.cellVoltage.min_mv);
        }
        if (cell.max_mv !== undefined) {
            merged.cellVoltage.max_mv = toNumber(cell.max_mv, merged.cellVoltage.max_mv);
        }
        if (cell.warning_delta_mv !== undefined) {
            merged.cellVoltage.warning_delta_mv = toNumber(cell.warning_delta_mv, merged.cellVoltage.warning_delta_mv);
        }
        if (cell.critical_delta_mv !== undefined) {
            merged.cellVoltage.critical_delta_mv = toNumber(cell.critical_delta_mv, merged.cellVoltage.critical_delta_mv);
        }
    }

    if (updates.alerts) {
        const alerts = updates.alerts;
        if (alerts.soc_critical !== undefined) {
            merged.alerts.soc_critical = toNumber(alerts.soc_critical, merged.alerts.soc_critical);
        }
        if (alerts.soc_low !== undefined) {
            merged.alerts.soc_low = toNumber(alerts.soc_low, merged.alerts.soc_low);
        }
        if (alerts.temp_warning !== undefined) {
            merged.alerts.temp_warning = toNumber(alerts.temp_warning, merged.alerts.temp_warning);
        }
        if (alerts.temp_critical !== undefined) {
            merged.alerts.temp_critical = toNumber(alerts.temp_critical, merged.alerts.temp_critical);
        }
        if (alerts.imbalance_warning !== undefined) {
            merged.alerts.imbalance_warning = toNumber(alerts.imbalance_warning, merged.alerts.imbalance_warning);
        }
        if (alerts.imbalance_critical !== undefined) {
            merged.alerts.imbalance_critical = toNumber(alerts.imbalance_critical, merged.alerts.imbalance_critical);
        }
        if (alerts.balancing_duration_warning_ms !== undefined) {
            merged.alerts.balancing_duration_warning_ms = toNumber(
                alerts.balancing_duration_warning_ms,
                merged.alerts.balancing_duration_warning_ms
            );
        }
    }

    merged.cellVoltage.max_mv = Math.max(merged.cellVoltage.max_mv, merged.cellVoltage.min_mv + 1);
    merged.cellVoltage.min_mv = Math.min(merged.cellVoltage.min_mv, merged.cellVoltage.max_mv - 1);
    merged.cellVoltage.warning_delta_mv = Math.max(1, merged.cellVoltage.warning_delta_mv);
    merged.cellVoltage.critical_delta_mv = Math.max(
        merged.cellVoltage.warning_delta_mv + 1,
        merged.cellVoltage.critical_delta_mv
    );

    merged.alerts.soc_low = Math.max(merged.alerts.soc_low, merged.alerts.soc_critical + 1);
    merged.alerts.temp_critical = Math.max(merged.alerts.temp_critical, merged.alerts.temp_warning);
    merged.alerts.imbalance_critical = Math.max(
        merged.alerts.imbalance_warning + 1,
        merged.alerts.imbalance_critical
    );
    merged.alerts.balancing_duration_warning_ms = Math.max(60000, merged.alerts.balancing_duration_warning_ms);

    return merged;
}

function loadDashboardPreferences() {
    const defaults = cloneDashboardPreferences(DASHBOARD_PREFERENCES_DEFAULT);

    try {
        const stored = localStorage.getItem(DASHBOARD_PREFERENCES_STORAGE_KEY);
        if (!stored) {
            return defaults;
        }

        const parsed = JSON.parse(stored);
        return mergeDashboardPreferences(defaults, parsed);
    } catch (error) {
        console.warn('[Dashboard] Failed to load preferences, using defaults:', error);
        return defaults;
    }
}

function persistDashboardPreferences() {
    try {
        localStorage.setItem(
            DASHBOARD_PREFERENCES_STORAGE_KEY,
            JSON.stringify(dashboardPreferences)
        );
    } catch (error) {
        console.warn('[Dashboard] Failed to persist preferences:', error);
    }
}

function updateCellPreferenceLabels() {
    const rangeLabel = document.getElementById('cellVoltageRangeLabel');
    if (rangeLabel) {
        rangeLabel.textContent = `${(dashboardPreferences.cellVoltage.min_mv / 1000).toFixed(3)} - ${(dashboardPreferences.cellVoltage.max_mv / 1000).toFixed(3)} V`;
    }

    const imbalanceLegend = document.getElementById('cellImbalanceLegend');
    if (imbalanceLegend) {
        imbalanceLegend.textContent = `OK < ${dashboardPreferences.cellVoltage.warning_delta_mv} mV | Warning < ${dashboardPreferences.cellVoltage.critical_delta_mv} mV | High ≥ ${dashboardPreferences.cellVoltage.critical_delta_mv} mV`;
    }
}

function applyDashboardPreferences() {
    updateCellPreferenceLabels();

    if (lastCellVoltages.length > 0) {
        updateCellsDisplay([...lastCellVoltages]);
    }

    if (lastLiveDataSnapshot) {
        checkAlerts(lastLiveDataSnapshot, lastStatsSnapshot || {});
    }
}

function getDashboardPreferences() {
    return cloneDashboardPreferences(dashboardPreferences);
}

function getDashboardPreferenceDefaults() {
    return cloneDashboardPreferences(DASHBOARD_PREFERENCES_DEFAULT);
}

function setDashboardPreferences(newPreferences) {
    dashboardPreferences = mergeDashboardPreferences(dashboardPreferences, newPreferences || {});
    persistDashboardPreferences();
    applyDashboardPreferences();
    return getDashboardPreferences();
}

window.getDashboardPreferences = getDashboardPreferences;
window.getDashboardPreferenceDefaults = getDashboardPreferenceDefaults;
window.setDashboardPreferences = setDashboardPreferences;

// ============================================
// Initialize Dashboard
// ============================================

function initDashboard() {
    console.log('[Dashboard] Initializing...');

    // Create gauges
    createGauges();

    // Create history chart
    createHistoryChart();

    // Initialize cells grid
    initCellsGrid();

    // Apply stored dashboard preferences
    applyDashboardPreferences();

    // Setup WebSocket data handler
    wsHandler.on('onMessage', handleWebSocketData);

    // Setup chart data selector
    setupChartSelector();

    // Setup period selector
    setupPeriodSelector();

    // Load initial history
    loadChartFromHistory(currentPeriod);

    console.log('[Dashboard] Initialized');
}

// ============================================
// Gauges Creation
// ============================================

function createGauges() {
    // SOC Gauge (0-100%)
    gauges.soc = createGauge('socGauge', {
        max: 100,
        value: 0,
        color: '#28a745',
        label: '%'
    });
    
    // Voltage Gauge (44-58V)
    gauges.voltage = createGauge('voltageGauge', {
        max: 58,
        min: 44,
        value: 51,
        color: '#0d6efd',
        label: 'V'
    });
    
    // Temperature Gauge (0-60°C)
    gauges.temperature = createGauge('tempGauge', {
        max: 60,
        value: 25,
        color: '#fd7e14',
        label: '°C'
    });
    
    // Current Gauge (-90 to +90A)
    gauges.current = createGauge('currentGauge', {
        max: 90,
        min: -90,
        value: 0,
        color: '#6f42c1',
        label: 'A',
        isBidirectional: true
    });
}

function createGauge(canvasId, options) {
    const ctx = document.getElementById(canvasId);
    if (!ctx) return null;
    
    const config = {
        type: 'doughnut',
        data: {
            datasets: [{
                data: [options.value || 0, (options.max - (options.min || 0)) - (options.value || 0)],
                backgroundColor: [options.color, '#e9ecef'],
                borderWidth: 0
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            cutout: '75%',
            rotation: -90,
            circumference: 180,
            plugins: {
                legend: { display: false },
                tooltip: { enabled: false }
            }
        }
    };
    
    return new Chart(ctx, config);
}

function updateGauge(gauge, value, min = 0, max = 100) {
    if (!gauge) return;
    
    const normalizedValue = Math.max(min, Math.min(max, value));
    const range = max - min;
    const fillValue = normalizedValue - min;
    const emptyValue = range - fillValue;
    
    gauge.data.datasets[0].data = [fillValue, emptyValue];
    gauge.update('none'); // No animation for smooth updates
}

// ============================================
// History Chart
// ============================================

function createHistoryChart() {
    const ctx = document.getElementById('historyChart');
    if (!ctx) return;
    
    historyChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [{
                label: 'Voltage (V)',
                data: [],
                borderColor: '#0d6efd',
                backgroundColor: 'rgba(13, 110, 253, 0.1)',
                tension: 0.4,
                fill: true
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            interaction: {
                mode: 'index',
                intersect: false
            },
            plugins: {
                legend: {
                    display: true,
                    position: 'top'
                },
                tooltip: {
                    enabled: true
                }
            },
            scales: {
                x: {
                    display: true,
                    title: {
                        display: true,
                        text: 'Time'
                    }
                },
                y: {
                    display: true,
                    title: {
                        display: true,
                        text: 'Value'
                    }
                }
            }
        }
    });
}

function setupChartSelector() {
    const selectors = ['chartVoltage', 'chartCurrent', 'chartSOC', 'chartTemp'];

    selectors.forEach(id => {
        const element = document.getElementById(id);
        if (element) {
            element.addEventListener('change', (e) => {
                if (e.target.checked) {
                    updateChartData(id.replace('chart', '').toLowerCase());
                }
            });
        }
    });
}

function setupPeriodSelector() {
    const selectors = ['period30m', 'period1h', 'period24h', 'period7d'];

    selectors.forEach(id => {
        const element = document.getElementById(id);
        if (element) {
            element.addEventListener('change', (e) => {
                if (e.target.checked) {
                    const period = id.replace('period', '').toLowerCase();
                    currentPeriod = period;
                    loadChartFromHistory(period);
                }
            });
        }
    });
}

function updateChartData(dataType) {
    if (!historyChart) return;
    
    const dataMap = {
        'voltage': {
            data: historyData.voltage,
            label: 'Voltage (V)',
            color: '#0d6efd'
        },
        'current': {
            data: historyData.current,
            label: 'Current (A)',
            color: '#6f42c1'
        },
        'soc': {
            data: historyData.soc,
            label: 'SOC (%)',
            color: '#28a745'
        },
        'temperature': {
            data: historyData.temperature,
            label: 'Temperature (°C)',
            color: '#fd7e14'
        }
    };
    
    const config = dataMap[dataType];
    if (!config) return;
    
    historyChart.data.labels = historyData.timestamps;
    historyChart.data.datasets[0] = {
        label: config.label,
        data: config.data,
        borderColor: config.color,
        backgroundColor: config.color + '20',
        tension: 0.4,
        fill: true
    };
    
    historyChart.update();
}

function addHistoryPoint(voltage, current, soc, temperature) {
    const now = new Date();
    const timeStr = currentPeriod === '24h' || currentPeriod === '7d'
        ? `${now.getMonth()+1}/${now.getDate()} ${now.getHours()}:${now.getMinutes().toString().padStart(2,'0')}`
        : now.toLocaleTimeString();

    // Only add to in-memory if we're on 30m period (real-time)
    if (currentPeriod === '30m') {
        historyData.timestamps.push(timeStr);
        historyData.voltage.push(voltage);
        historyData.current.push(current);
        historyData.soc.push(soc);
        historyData.temperature.push(temperature);

        // Keep only last MAX_HISTORY_POINTS
        if (historyData.timestamps.length > MAX_HISTORY_POINTS) {
            historyData.timestamps.shift();
            historyData.voltage.shift();
            historyData.current.shift();
            historyData.soc.shift();
            historyData.temperature.shift();
        }

        // Update current chart
        const checkedRadio = document.querySelector('input[name="chartData"]:checked');
        if (checkedRadio) {
            const dataType = checkedRadio.id.replace('chart', '').toLowerCase();
            updateChartData(dataType);
        }
    }

    // Save to localStorage periodically
    const timeSinceLastSave = Date.now() - lastHistorySave;
    if (timeSinceLastSave >= HISTORY_SAVE_INTERVAL_MS) {
        const dataPoint = {
            timestamp: now.getTime(),
            voltage: voltage,
            current: current,
            soc: soc,
            temperature: temperature
        };
        saveHistoryToStorage(dataPoint);
        lastHistorySave = Date.now();
    }
}

// ============================================
// Cells Grid
// ============================================

function initCellsGrid() {
    const container = document.getElementById('cellsGrid');
    if (!container) return;
    
    container.innerHTML = '';
    
    for (let i = 1; i <= 16; i++) {
        const cellDiv = document.createElement('div');
        cellDiv.className = 'cell-item fade-in';
        cellDiv.id = `cell-${i}`;
        cellDiv.innerHTML = `
            <div class="cell-number">Cell ${i}</div>
            <div class="cell-voltage" id="cell-voltage-${i}">-- mV</div>
            <div class="cell-bar">
                <div class="cell-bar-fill" id="cell-bar-${i}" style="width: 0%"></div>
            </div>
            <div class="cell-balancing-indicator" id="cell-balancing-${i}">
                <i class="fas fa-circle text-secondary"></i>
            </div>
        `;
        container.appendChild(cellDiv);
    }
}

function updateCellsDisplay(cellsData) {
    if (!cellsData || cellsData.length === 0) return;

    lastCellVoltages = Array.isArray(cellsData) ? [...cellsData] : [];

    const cellPrefs = dashboardPreferences.cellVoltage;
    const minScale = Math.min(cellPrefs.min_mv, cellPrefs.max_mv - 1);
    const maxScale = Math.max(cellPrefs.max_mv, minScale + 1);
    const scaleRange = Math.max(1, maxScale - minScale);

    let minVoltage = Infinity;
    let maxVoltage = -Infinity;
    let minCell = 0;
    let maxCell = 0;
    let balancingCount = 0;
    
    // Update each cell
    cellsData.forEach((voltage, index) => {
        const cellNum = index + 1;
        const cellElement = document.getElementById(`cell-${cellNum}`);
        const voltageElement = document.getElementById(`cell-voltage-${cellNum}`);
        const barElement = document.getElementById(`cell-bar-${cellNum}`);
        
        if (voltageElement) {
            voltageElement.textContent = `${voltage} mV`;
        }
        
        // Update progress bar based on configured range
        if (barElement) {
            const clampedVoltage = Math.max(minScale, Math.min(maxScale, voltage));
            const percentage = Math.max(0, Math.min(100, ((clampedVoltage - minScale) / scaleRange) * 100));
            barElement.style.width = `${percentage}%`;
        }
        
        // Track min/max
        if (voltage < minVoltage) {
            minVoltage = voltage;
            minCell = cellNum;
        }
        if (voltage > maxVoltage) {
            maxVoltage = voltage;
            maxCell = cellNum;
        }
        
        // Remove old classes
        if (cellElement) {
            cellElement.classList.remove('cell-min', 'cell-max', 'cell-balancing');
        }
    });
    
    // Highlight min/max cells
    const minCellElement = document.getElementById(`cell-${minCell}`);
    const maxCellElement = document.getElementById(`cell-${maxCell}`);
    
    if (minCellElement) minCellElement.classList.add('cell-min');
    if (maxCellElement) maxCellElement.classList.add('cell-max');
    
    // Update stats
    document.getElementById('minCellValue').textContent = `${minVoltage} mV`;
    document.getElementById('minCellNumber').textContent = `Cell ${minCell}`;
    document.getElementById('maxCellValue').textContent = `${maxVoltage} mV`;
    document.getElementById('maxCellNumber').textContent = `Cell ${maxCell}`;
    
    const imbalance = maxVoltage - minVoltage;
    document.getElementById('imbalanceValue').textContent = `${imbalance} mV`;

    const imbalanceStatus = document.getElementById('imbalanceStatus');
    if (imbalance < cellPrefs.warning_delta_mv) {
        imbalanceStatus.textContent = 'OK';
        imbalanceStatus.className = 'badge bg-success';
    } else if (imbalance < cellPrefs.critical_delta_mv) {
        imbalanceStatus.textContent = 'Warning';
        imbalanceStatus.className = 'badge bg-warning';
    } else {
        imbalanceStatus.textContent = 'High';
        imbalanceStatus.className = 'badge bg-danger';
    }
}

function updateBalancingDisplay(balancingBits) {
    let activeCount = 0;
    let activeCells = [];
    
    for (let i = 1; i <= 16; i++) {
        const isBalancing = (balancingBits & (1 << (i - 1))) !== 0;
        const indicator = document.getElementById(`cell-balancing-${i}`);
        const cell = document.getElementById(`cell-${i}`);
        
        if (indicator) {
            if (isBalancing) {
                indicator.innerHTML = '<i class="fas fa-circle text-info"></i>';
                cell.classList.add('cell-balancing');
                activeCount++;
                activeCells.push(i);
            } else {
                indicator.innerHTML = '<i class="fas fa-circle text-secondary"></i>';
                cell.classList.remove('cell-balancing');
            }
        }
    }
    
    // Update status
    const statusElement = document.getElementById('balancingStatus');
    if (activeCount > 0) {
        statusElement.innerHTML = `<i class="fas fa-circle text-info"></i> Active on cells ${activeCells.join(', ')}`;
    } else {
        statusElement.innerHTML = `<i class="fas fa-circle text-secondary"></i> Inactive`;
    }
}

// ============================================
// Alert System
// ============================================

let balancingStartTime = null;
let lastAlerts = {
    soc_critical: false,
    soc_low: false,
    temp_warning: false,
    temp_critical: false,
    imbalance_warning: false,
    imbalance_critical: false,
    balancing_duration: false,
    victron_keepalive: false
};

function checkAlerts(liveData, stats) {
    const thresholds = dashboardPreferences.alerts;
    const currentAlerts = {
        soc_critical: false,
        soc_low: false,
        temp_warning: false,
        temp_critical: false,
        imbalance_warning: false,
        imbalance_critical: false,
        balancing_duration: false,
        victron_keepalive: false
    };

    // SOC Alerts
    const soc = liveData.soc_percent || 0;
    if (soc < thresholds.soc_critical) {
        currentAlerts.soc_critical = true;
        if (!lastAlerts.soc_critical) {
            createPersistentAlert('soc_critical', `⚠️ SOC Critique: ${soc.toFixed(1)}%`, 'danger');
            addNotification(`SOC Critical: ${soc.toFixed(1)}% - Charge immediately!`, 'danger');
        }
    } else if (soc < thresholds.soc_low) {
        currentAlerts.soc_low = true;
        if (!lastAlerts.soc_low) {
            createPersistentAlert('soc_low', `⚡ SOC Faible: ${soc.toFixed(1)}%`, 'warning');
            addNotification(`SOC Low: ${soc.toFixed(1)}% - Consider charging`, 'warning');
        }
    } else {
        removePersistentAlert('soc_critical');
        removePersistentAlert('soc_low');
    }

    // Temperature Alerts
    const temp = (liveData.temperature || 0) / 10;
    if (temp > thresholds.temp_critical) {
        currentAlerts.temp_critical = true;
        if (!lastAlerts.temp_critical) {
            createPersistentAlert('temp_critical', `🔥 Température Critique: ${temp.toFixed(1)}°C`, 'danger');
            addNotification(`Temperature Critical: ${temp.toFixed(1)}°C - Shutdown recommended!`, 'danger');
        }
    } else if (temp > thresholds.temp_warning) {
        currentAlerts.temp_warning = true;
        if (!lastAlerts.temp_warning) {
            createPersistentAlert('temp_warning', `🌡️ Température Élevée: ${temp.toFixed(1)}°C`, 'warning');
            addNotification(`Temperature High: ${temp.toFixed(1)}°C - Monitor closely`, 'warning');
        }
    } else {
        removePersistentAlert('temp_critical');
        removePersistentAlert('temp_warning');
    }

    // Cell Imbalance Alerts
    const minCell = liveData.min_cell_mv || 0;
    const maxCell = liveData.max_cell_mv || 0;
    const imbalance = maxCell - minCell;
    if (imbalance > thresholds.imbalance_critical) {
        currentAlerts.imbalance_critical = true;
        if (!lastAlerts.imbalance_critical) {
            createPersistentAlert('imbalance_critical', `⚡ Déséquilibre Critique: ${imbalance}mV`, 'danger');
            addNotification(`Cell Imbalance Critical: ${imbalance}mV - Check cells!`, 'danger');
        }
    } else if (imbalance > thresholds.imbalance_warning) {
        currentAlerts.imbalance_warning = true;
        if (!lastAlerts.imbalance_warning) {
            createPersistentAlert('imbalance_warning', `⚠️ Déséquilibre Élevé: ${imbalance}mV`, 'warning');
            addNotification(`Cell Imbalance High: ${imbalance}mV`, 'warning');
        }
    } else {
        removePersistentAlert('imbalance_critical');
        removePersistentAlert('imbalance_warning');
    }

    // Balancing Duration Alert
    const isBalancing = (liveData.balancing_bits || 0) > 0;
    if (isBalancing) {
        if (balancingStartTime === null) {
            balancingStartTime = Date.now();
        }
        const balancingDuration = Date.now() - balancingStartTime;
        if (balancingDuration > thresholds.balancing_duration_warning_ms) {
            currentAlerts.balancing_duration = true;
            if (!lastAlerts.balancing_duration) {
                const durationMin = Math.floor(balancingDuration / 60000);
                createPersistentAlert('balancing_duration', `⏱️ Balancing actif depuis ${durationMin}min`, 'info');
                addNotification(`Balancing active for ${durationMin} minutes`, 'info');
            }
        }
    } else {
        balancingStartTime = null;
        removePersistentAlert('balancing_duration');
    }

    // Victron Keepalive Alert
    if (!stats.victron_keepalive_ok) {
        currentAlerts.victron_keepalive = true;
        if (!lastAlerts.victron_keepalive) {
            createPersistentAlert('victron_keepalive', '📡 Victron Keepalive Perdu', 'warning');
            addNotification('Victron keepalive timeout - Check connection', 'warning');
        }
    } else {
        removePersistentAlert('victron_keepalive');
    }

    lastAlerts = currentAlerts;
}

function createPersistentAlert(id, message, type) {
    // Remove if exists
    removePersistentAlert(id);

    const alertsContainer = document.getElementById('persistentAlerts');
    if (!alertsContainer) {
        // Create container if it doesn't exist
        const container = document.createElement('div');
        container.id = 'persistentAlerts';
        container.className = 'persistent-alerts-container';
        document.querySelector('.main-content')?.prepend(container);
    }

    const alert = document.createElement('div');
    alert.id = `alert-${id}`;
    alert.className = `alert alert-${type} alert-dismissible fade show persistent-alert`;
    alert.setAttribute('role', 'alert');
    alert.innerHTML = `
        <strong>${message}</strong>
        <button type="button" class="btn-close" onclick="removePersistentAlert('${id}')"></button>
    `;

    document.getElementById('persistentAlerts')?.appendChild(alert);
}

function removePersistentAlert(id) {
    const alert = document.getElementById(`alert-${id}`);
    if (alert) {
        alert.remove();
    }
}

// ============================================
// WebSocket Data Handler
// ============================================

function handleWebSocketData(data) {
    if (!data || !data.live_data) return;

    const liveData = data.live_data;
    const stats = data.stats || {};

    lastLiveDataSnapshot = { ...liveData };
    lastStatsSnapshot = { ...stats };

    // Check for alerts FIRST
    checkAlerts(liveData, stats);

    // Update gauges
    if (gauges.soc) {
        updateGauge(gauges.soc, liveData.soc_percent || 0, 0, 100);
        document.getElementById('socValue').textContent = `${(liveData.soc_percent || 0).toFixed(1)}%`;
    }
    
    if (gauges.voltage) {
        updateGauge(gauges.voltage, liveData.voltage || 0, 44, 58);
        document.getElementById('voltageValue').textContent = `${(liveData.voltage || 0).toFixed(2)}V`;
    }
    
    if (gauges.temperature) {
        const tempC = (liveData.temperature || 0) / 10;
        updateGauge(gauges.temperature, tempC, 0, 60);
        document.getElementById('tempValue').textContent = `${tempC.toFixed(1)}°C`;
    }
    
    if (gauges.current) {
        updateGauge(gauges.current, liveData.current || 0, -90, 90);
        const currentStr = (liveData.current || 0) >= 0 ? 
            `+${(liveData.current || 0).toFixed(1)}A` : 
            `${(liveData.current || 0).toFixed(1)}A`;
        document.getElementById('currentValue').textContent = currentStr;
    }
    
    // Update cells (generate dummy data for now - will come from WebSocket later)
    // In production, this would be: updateCellsDisplay(liveData.cells);
    // For demo, generate 16 cells around min_cell_mv and max_cell_mv
    if (liveData.min_cell_mv && liveData.max_cell_mv) {
        const cellsData = [];
        const min = liveData.min_cell_mv;
        const max = liveData.max_cell_mv;
        const range = max - min;
        
        for (let i = 0; i < 16; i++) {
            cellsData.push(Math.round(min + (Math.random() * range)));
        }
        updateCellsDisplay(cellsData);
    }
    
    // Update balancing
    if (typeof liveData.balancing_bits !== 'undefined') {
        updateBalancingDisplay(liveData.balancing_bits);
    }
    
    // Update system status
    updateSystemStatus(liveData, stats);
    
    // Add to history
    addHistoryPoint(
        liveData.voltage || 0,
        liveData.current || 0,
        liveData.soc_percent || 0,
        (liveData.temperature || 0) / 10
    );
    
    // Update footer
    updateFooter(data);
}

function updateSystemStatus(liveData, stats) {
    // TinyBMS status
    const tinyStatus = document.getElementById('tinyBmsStatus');
    const tinyUpdate = document.getElementById('tinyBmsLastUpdate');
    
    if (liveData.online_status) {
        tinyStatus.textContent = 'Online';
        tinyStatus.className = 'badge bg-success';
        tinyUpdate.textContent = 'Just now';
    } else {
        tinyStatus.textContent = 'Offline';
        tinyStatus.className = 'badge bg-danger';
    }
    
    // Victron status
    const victronStatus = document.getElementById('victronStatus');
    const victronKeepalive = document.getElementById('victronKeepalive');
    
    if (stats.victron_keepalive_ok) {
        victronStatus.textContent = 'Active';
        victronStatus.className = 'badge bg-success';
        victronKeepalive.textContent = 'OK';
    } else {
        victronStatus.textContent = 'No keepalive';
        victronStatus.className = 'badge bg-warning';
        victronKeepalive.textContent = 'Timeout';
    }
    
    // CVL status
    const cvlState = document.getElementById('cvlState');
    const cvlTarget = document.getElementById('cvlTarget');
    
    const cvlStates = ['BULK', 'TRANSITION', 'FLOAT_APPROACH', 'FLOAT', 'IMBALANCE_HOLD'];
    if (typeof stats.cvl_state !== 'undefined' && stats.cvl_state < cvlStates.length) {
        cvlState.textContent = cvlStates[stats.cvl_state];
        cvlState.className = 'badge bg-info';
    }
    
    if (stats.cvl_current_v) {
        cvlTarget.textContent = `${stats.cvl_current_v.toFixed(1)}V`;
    }
}

function updateFooter(data) {
    // Uptime
    if (data.uptime_ms) {
        const uptimeS = Math.floor(data.uptime_ms / 1000);
        const days = Math.floor(uptimeS / 86400);
        const hours = Math.floor((uptimeS % 86400) / 3600);
        const minutes = Math.floor((uptimeS % 3600) / 60);
        
        document.getElementById('uptime').textContent = 
            `${days}d ${hours}h ${minutes}m`;
    }
    
    // Will be updated from /api/memory endpoint
    // For now, placeholder
}

// ============================================
// Logs
// ============================================

function addLog(message, type = 'info') {
    const container = document.getElementById('logsContainer');
    if (!container) return;
    
    // Remove "no logs" message
    if (container.querySelector('.text-muted')) {
        container.innerHTML = '';
    }
    
    const logEntry = document.createElement('div');
    logEntry.className = `log-entry log-${type} fade-in`;
    
    const timestamp = new Date().toLocaleTimeString();
    
    logEntry.innerHTML = `
        <span>${message}</span>
        <span class="log-timestamp">${timestamp}</span>
    `;
    
    container.insertBefore(logEntry, container.firstChild);
    
    // Keep only last 50 logs
    while (container.children.length > 50) {
        container.removeChild(container.lastChild);
    }
}

function clearLogs() {
    const container = document.getElementById('logsContainer');
    if (container) {
        container.innerHTML = '<div class="text-muted text-center py-3">No logs yet...</div>';
    }
}

// ============================================
// Initialize on Load
// ============================================

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initDashboard);
} else {
    initDashboard();
}

// Add logs when WebSocket connects/disconnects
wsHandler.on('onOpen', () => {
    addLog('WebSocket connected', 'success');
});

wsHandler.on('onClose', () => {
    addLog('WebSocket disconnected', 'warning');
});

wsHandler.on('onError', () => {
    addLog('WebSocket error', 'error');
});
