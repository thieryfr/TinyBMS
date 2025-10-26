/**
 * @file app_bootstrap.js
 * @brief Application JavaScript pour interface Bootstrap 5
 * @version 3.0
 */

// ============================================================================
// GLOBAL STATE
// ============================================================================

let ws = null;
let reconnectInterval = null;
let liveData = {};
let historyData = {
    timestamps: [],
    voltage: [],
    current: [],
    soc: [],
    temperature: []
};
let charts = {};

// ============================================================================
// INITIALIZATION
// ============================================================================

document.addEventListener('DOMContentLoaded', function() {
    console.log('[APP] Initializing Bootstrap Dashboard v3.0...');
    
    // Initialize WebSocket
    initWebSocket();
    
    // Initialize Charts
    initCharts();
    
    // Initialize Cell Display
    initCellsDisplay();
    
    // Setup Event Listeners
    setupEventListeners();
    
    // Load Settings from LocalStorage
    loadSettings();
    
    console.log('[APP] Initialization complete');
});

// ============================================================================
// WEBSOCKET
// ============================================================================

function initWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws`;
    
    ws = new WebSocket(wsUrl);
    
    ws.onopen = function() {
        console.log('[WS] Connected');
        updateConnectionStatus(true);
        showToast('Connecté au serveur', 'success');
        
        if (reconnectInterval) {
            clearInterval(reconnectInterval);
            reconnectInterval = null;
        }
    };
    
    ws.onmessage = function(event) {
        try {
            const data = JSON.parse(event.data);
            handleWebSocketData(data);
        } catch (e) {
            console.error('[WS] Parse error:', e);
        }
    };
    
    ws.onclose = function() {
        console.log('[WS] Disconnected');
        updateConnectionStatus(false);
        showToast('Connexion perdue, reconnexion...', 'warning');
        
        // Auto-reconnect
        if (!reconnectInterval) {
            reconnectInterval = setInterval(() => {
                console.log('[WS] Attempting reconnect...');
                initWebSocket();
            }, 5000);
        }
    };
    
    ws.onerror = function(error) {
        console.error('[WS] Error:', error);
    };
}

function handleWebSocketData(data) {
    liveData = data;
    
    // Update Dashboard
    updateDashboard(data);
    
    // Update History
    updateHistory(data);
    
    // Check Alerts
    checkAlerts(data);
}

function updateConnectionStatus(connected) {
    const statusDot = document.getElementById('wsStatus');
    const statusText = document.getElementById('wsStatusText');
    
    if (connected) {
        statusDot.classList.remove('disconnected');
        statusDot.classList.add('connected');
        statusText.textContent = 'Connecté';
    } else {
        statusDot.classList.remove('connected');
        statusDot.classList.add('disconnected');
        statusText.textContent = 'Déconnecté';
    }
}

// ============================================================================
// DASHBOARD UPDATE
// ============================================================================

function updateDashboard(data) {
    if (!data.live_data) return;
    
    const live = data.live_data;
    const stats = data.stats || {};
    
    // SOC
    const soc = live.soc_percent || 0;
    document.getElementById('socValue').textContent = soc.toFixed(1);
    document.getElementById('socProgress').style.width = soc + '%';
    
    // Voltage
    const voltage = live.voltage || 0;
    document.getElementById('voltageValue').textContent = voltage.toFixed(2);
    document.getElementById('cellMaxVoltage').textContent = live.max_cell_mv || '--';
    
    // Current
    const current = live.current || 0;
    const currentEl = document.getElementById('currentValue');
    currentEl.textContent = Math.abs(current).toFixed(1);
    currentEl.className = 'stat-value ' + (current > 0 ? 'text-success' : 'text-danger');
    
    const currentDir = document.getElementById('currentDirection');
    currentDir.textContent = current > 0 ? 'Charge' : 'Décharge';
    currentDir.className = 'badge mt-2 ' + (current > 0 ? 'bg-success' : 'bg-danger');
    
    // Temperature
    const temp = (live.temperature || 0) / 10;
    document.getElementById('tempValue').textContent = temp.toFixed(1);
    
    // System Status
    document.getElementById('tinyBmsStatus').textContent = 
        live.online_status >= 0x91 && live.online_status <= 0x97 ? 'OK' : 'Erreur';
    
    document.getElementById('victronStatus').textContent = 
        stats.victron_keepalive_ok ? 'Connecté' : 'Déconnecté';
    
    // CVL State
    const cvlStates = ['BULK', 'TRANSITION', 'FLOAT_APPROACH', 'FLOAT', 'IMBALANCE_HOLD'];
    document.getElementById('cvlState').textContent = cvlStates[stats.cvl_state] || 'N/A';
    
    // Balancing
    document.getElementById('balancingStatus').textContent = 
        live.balancing_bits ? 'Actif' : 'Inactif';
    
    // SOH
    document.getElementById('sohValue').textContent = live.soh_percent.toFixed(1) + '%';
    
    // Statistics
    document.getElementById('canTxCount').textContent = stats.can_tx_count || 0;
    document.getElementById('canRxCount').textContent = stats.can_rx_count || 0;
    document.getElementById('uartErrors').textContent = stats.uart_errors || 0;
    document.getElementById('cvlVoltage').textContent = (stats.cvl_current_v || 0).toFixed(2);
    
    // Uptime
    const uptime = data.uptime_ms || 0;
    document.getElementById('uptime').textContent = formatUptime(uptime);
    
    // Free Heap
    fetchSystemInfo();
    
    // Update Gauges
    updateGauges(soc, voltage * current);
    
    // Update Charts
    updateCharts();
}

function updateGauges(soc, power) {
    // Update SOC Gauge
    if (charts.socGauge) {
        charts.socGauge.data.datasets[0].data = [soc, 100 - soc];
        charts.socGauge.update('none');
    }
    
    // Update Power Gauge
    if (charts.powerGauge) {
        const powerKw = Math.abs(power) / 1000;
        charts.powerGauge.data.datasets[0].data = [powerKw, 10 - powerKw];
        charts.powerGauge.update('none');
    }
}

// ============================================================================
// CHARTS INITIALIZATION
// ============================================================================

function initCharts() {
    // SOC Gauge (Doughnut)
    const socGaugeCtx = document.getElementById('socGauge').getContext('2d');
    charts.socGauge = new Chart(socGaugeCtx, {
        type: 'doughnut',
        data: {
            labels: ['SOC', 'Restant'],
            datasets: [{
                data: [0, 100],
                backgroundColor: ['#0d6efd', '#e9ecef'],
                borderWidth: 0
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            plugins: {
                legend: { display: false },
                tooltip: { enabled: false }
            },
            cutout: '75%'
        }
    });
    
    // Power Gauge (Doughnut)
    const powerGaugeCtx = document.getElementById('powerGauge').getContext('2d');
    charts.powerGauge = new Chart(powerGaugeCtx, {
        type: 'doughnut',
        data: {
            labels: ['Puissance', 'Max'],
            datasets: [{
                data: [0, 10],
                backgroundColor: ['#198754', '#e9ecef'],
                borderWidth: 0
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            plugins: {
                legend: { display: false },
                tooltip: { enabled: false }
            },
            cutout: '75%'
        }
    });
    
    // History Chart (Line)
    const historyCtx = document.getElementById('historyChart').getContext('2d');
    charts.historyChart = new Chart(historyCtx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [
                {
                    label: 'Tension (V)',
                    data: [],
                    borderColor: '#0d6efd',
                    backgroundColor: 'rgba(13, 110, 253, 0.1)',
                    yAxisID: 'y',
                    tension: 0.4
                },
                {
                    label: 'Courant (A)',
                    data: [],
                    borderColor: '#dc3545',
                    backgroundColor: 'rgba(220, 53, 69, 0.1)',
                    yAxisID: 'y1',
                    tension: 0.4
                }
            ]
        },
        options: {
            responsive: true,
            interaction: {
                mode: 'index',
                intersect: false,
            },
            plugins: {
                legend: {
                    position: 'top',
                }
            },
            scales: {
                y: {
                    type: 'linear',
                    display: true,
                    position: 'left',
                    title: {
                        display: true,
                        text: 'Tension (V)'
                    }
                },
                y1: {
                    type: 'linear',
                    display: true,
                    position: 'right',
                    title: {
                        display: true,
                        text: 'Courant (A)'
                    },
                    grid: {
                        drawOnChartArea: false,
                    },
                },
            }
        }
    });
    
    // Long History Chart
    const longHistoryCtx = document.getElementById('longHistoryChart').getContext('2d');
    charts.longHistoryChart = new Chart(longHistoryCtx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [
                {
                    label: 'SOC (%)',
                    data: [],
                    borderColor: '#0d6efd',
                    tension: 0.4
                },
                {
                    label: 'Température (°C)',
                    data: [],
                    borderColor: '#ffc107',
                    tension: 0.4
                }
            ]
        },
        options: {
            responsive: true,
            plugins: {
                legend: {
                    position: 'top',
                }
            }
        }
    });
}

function updateHistory(data) {
    if (!data.live_data) return;
    
    const now = new Date();
    const timeLabel = now.toLocaleTimeString();
    
    // Keep only last 60 points (1 minute at 1Hz)
    if (historyData.timestamps.length >= 60) {
        historyData.timestamps.shift();
        historyData.voltage.shift();
        historyData.current.shift();
        historyData.soc.shift();
        historyData.temperature.shift();
    }
    
    historyData.timestamps.push(timeLabel);
    historyData.voltage.push(data.live_data.voltage || 0);
    historyData.current.push(data.live_data.current || 0);
    historyData.soc.push(data.live_data.soc_percent || 0);
    historyData.temperature.push((data.live_data.temperature || 0) / 10);
}

function updateCharts() {
    // Update History Chart
    if (charts.historyChart) {
        charts.historyChart.data.labels = historyData.timestamps;
        charts.historyChart.data.datasets[0].data = historyData.voltage;
        charts.historyChart.data.datasets[1].data = historyData.current;
        charts.historyChart.update('none');
    }
    
    // Update Long History Chart
    if (charts.longHistoryChart) {
        charts.longHistoryChart.data.labels = historyData.timestamps;
        charts.longHistoryChart.data.datasets[0].data = historyData.soc;
        charts.longHistoryChart.data.datasets[1].data = historyData.temperature;
        charts.longHistoryChart.update('none');
    }
}

// ============================================================================
// CELLS DISPLAY
// ============================================================================

function initCellsDisplay() {
    const container = document.getElementById('cellsContainer');
    
    for (let i = 1; i <= 16; i++) {
        const cellHtml = `
            <div class="col-lg-3 col-md-4 col-sm-6">
                <div class="card">
                    <div class="card-body text-center">
                        <h6 class="text-muted">Cellule ${i}</h6>
                        <div class="stat-value" id="cell${i}Voltage">-- mV</div>
                        <div class="cell-voltage-bar mt-2">
                            <div class="cell-voltage-indicator" id="cell${i}Indicator"></div>
                        </div>
                        <small class="text-muted" id="cell${i}Status">--</small>
                    </div>
                </div>
            </div>
        `;
        container.innerHTML += cellHtml;
    }
}

function updateCellsDisplay(cells) {
    if (!cells || cells.length === 0) return;
    
    let min = Infinity, max = -Infinity, sum = 0;
    
    cells.forEach((voltage, index) => {
        const cellNum = index + 1;
        document.getElementById(`cell${cellNum}Voltage`).textContent = voltage + ' mV';
        
        // Update indicator position (3000-4200mV range)
        const percentage = ((voltage - 3000) / 1200) * 100;
        const indicator = document.getElementById(`cell${cellNum}Indicator`);
        if (indicator) {
            indicator.style.bottom = Math.max(0, Math.min(100, percentage)) + '%';
        }
        
        min = Math.min(min, voltage);
        max = Math.max(max, voltage);
        sum += voltage;
    });
    
    const avg = sum / cells.length;
    const imbalance = max - min;
    
    document.getElementById('cellMax').textContent = max.toFixed(0) + ' mV';
    document.getElementById('cellMin').textContent = min.toFixed(0) + ' mV';
    document.getElementById('cellAvg').textContent = avg.toFixed(0) + ' mV';
    document.getElementById('cellImbalance').textContent = imbalance.toFixed(0) + ' mV';
}

// ============================================================================
// ALERTS
// ============================================================================

function checkAlerts(data) {
    if (!data.live_data) return;
    
    const live = data.live_data;
    const settings = getSettings();
    
    // Low SOC
    if (settings.notifyLowSoc && live.soc_percent < 20) {
        showAlert('SOC Bas', `État de charge: ${live.soc_percent.toFixed(1)}%`, 'warning');
    }
    
    // High Temperature
    if (settings.notifyHighTemp && (live.temperature / 10) > 50) {
        showAlert('Température Élevée', `Température: ${(live.temperature / 10).toFixed(1)}°C`, 'danger');
    }
    
    // Cell Imbalance
    if (settings.notifyImbalance && live.cell_imbalance_mv > 100) {
        showAlert('Déséquilibre Cellules', `Déséquilibre: ${live.cell_imbalance_mv} mV`, 'warning');
    }
}

function showAlert(title, message, type) {
    const container = document.getElementById('alertContainer');
    const alertHtml = `
        <div class="alert alert-${type} alert-dismissible fade show" role="alert">
            <strong>${title}:</strong> ${message}
            <button type="button" class="btn-close" data-bs-dismiss="alert"></button>
        </div>
    `;
    
    // Avoid duplicates
    if (!container.innerHTML.includes(title)) {
        container.innerHTML += alertHtml;
        
        // Auto-dismiss after 10 seconds
        setTimeout(() => {
            const alerts = container.querySelectorAll('.alert');
            if (alerts.length > 0) {
                alerts[0].remove();
            }
        }, 10000);
    }
}

// ============================================================================
// TOAST NOTIFICATIONS
// ============================================================================

function showToast(message, type = 'info') {
    const toastEl = document.getElementById('toastNotification');
    const toastBody = document.getElementById('toastMessage');
    
    toastBody.textContent = message;
    toastEl.className = `toast bg-${type} text-white`;
    
    const toast = new bootstrap.Toast(toastEl);
    toast.show();
}

// ============================================================================
// EVENT LISTENERS
// ============================================================================

function setupEventListeners() {
    // CVL Settings Sliders
    ['bulkThreshold', 'floatThreshold', 'floatExit'].forEach(id => {
        const slider = document.getElementById(id);
        slider?.addEventListener('input', function() {
            document.getElementById(id + 'Value').textContent = this.value;
        });
    });
    
    // Refresh Rate Slider
    document.getElementById('refreshRate')?.addEventListener('input', function() {
        document.getElementById('refreshRateValue').textContent = this.value;
    });
    
    // Save CVL Settings
    document.getElementById('saveCvlSettings')?.addEventListener('click', saveCvlSettings);
    
    // Save Display Settings
    document.getElementById('saveDisplaySettings')?.addEventListener('click', saveDisplaySettings);
    
    // Quick Actions
    document.getElementById('btnRefreshData')?.addEventListener('click', () => {
        fetch('/api/status').then(r => r.json()).then(data => {
            handleWebSocketData(data);
            showToast('Données rafraîchies', 'success');
        });
    });
    
    document.getElementById('btnExportData')?.addEventListener('click', exportData);
    document.getElementById('btnResetStats')?.addEventListener('click', resetStats);
}

// ============================================================================
// SETTINGS MANAGEMENT
// ============================================================================

function getSettings() {
    const defaults = {
        cvlEnabled: true,
        bulkThreshold: 90,
        floatThreshold: 100,
        floatExit: 88,
        autoRefresh: true,
        refreshRate: 1000,
        theme: 'light',
        unitSystem: 'metric',
        notifyLowSoc: true,
        notifyHighTemp: true,
        notifyImbalance: true,
        notifyDisconnect: false
    };
    
    const saved = localStorage.getItem('dashboardSettings');
    return saved ? { ...defaults, ...JSON.parse(saved) } : defaults;
}

function saveSettings(settings) {
    localStorage.setItem('dashboardSettings', JSON.stringify(settings));
    showToast('Paramètres sauvegardés', 'success');
}

function loadSettings() {
    const settings = getSettings();
    
    // Apply settings to UI
    document.getElementById('cvlEnabled').checked = settings.cvlEnabled;
    document.getElementById('bulkThreshold').value = settings.bulkThreshold;
    document.getElementById('floatThreshold').value = settings.floatThreshold;
    document.getElementById('floatExit').value = settings.floatExit;
    document.getElementById('autoRefresh').checked = settings.autoRefresh;
    document.getElementById('refreshRate').value = settings.refreshRate;
    document.getElementById('unitSystem').value = settings.unitSystem;
    
    // Update display values
    document.getElementById('bulkThresholdValue').textContent = settings.bulkThreshold;
    document.getElementById('floatThresholdValue').textContent = settings.floatThreshold;
    document.getElementById('floatExitValue').textContent = settings.floatExit;
    document.getElementById('refreshRateValue').textContent = settings.refreshRate;
}

function saveCvlSettings() {
    const settings = getSettings();
    settings.cvlEnabled = document.getElementById('cvlEnabled').checked;
    settings.bulkThreshold = parseFloat(document.getElementById('bulkThreshold').value);
    settings.floatThreshold = parseFloat(document.getElementById('floatThreshold').value);
    settings.floatExit = parseFloat(document.getElementById('floatExit').value);
    
    // TODO: Send to ESP32 via API
    fetch('/api/config/cvl', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(settings)
    }).then(() => {
        saveSettings(settings);
    }).catch(err => {
        console.error('Failed to save CVL settings:', err);
        showToast('Erreur sauvegarde CVL', 'danger');
    });
}

function saveDisplaySettings() {
    const settings = getSettings();
    settings.autoRefresh = document.getElementById('autoRefresh').checked;
    settings.refreshRate = parseInt(document.getElementById('refreshRate').value);
    settings.unitSystem = document.getElementById('unitSystem').value;
    
    saveSettings(settings);
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

function formatUptime(ms) {
    const seconds = Math.floor(ms / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);
    const days = Math.floor(hours / 24);
    
    if (days > 0) return `${days}j ${hours % 24}h`;
    if (hours > 0) return `${hours}h ${minutes % 60}m`;
    if (minutes > 0) return `${minutes}m ${seconds % 60}s`;
    return `${seconds}s`;
}

function exportData() {
    // Export history data as CSV
    let csv = 'Timestamp,Voltage,Current,SOC,Temperature\n';
    
    for (let i = 0; i < historyData.timestamps.length; i++) {
        csv += `${historyData.timestamps[i]},`;
        csv += `${historyData.voltage[i]},`;
        csv += `${historyData.current[i]},`;
        csv += `${historyData.soc[i]},`;
        csv += `${historyData.temperature[i]}\n`;
    }
    
    const blob = new Blob([csv], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `tinybms_data_${new Date().toISOString()}.csv`;
    a.click();
    
    showToast('Données exportées', 'success');
}

function resetStats() {
    if (confirm('Réinitialiser toutes les statistiques ?')) {
        // TODO: Send reset command to ESP32
        fetch('/api/stats/reset', { method: 'POST' })
            .then(() => {
                showToast('Statistiques réinitialisées', 'success');
            })
            .catch(err => {
                console.error('Reset failed:', err);
                showToast('Erreur réinitialisation', 'danger');
            });
    }
}

async function fetchSystemInfo() {
    try {
        const response = await fetch('/api/system');
        const data = await response.json();
        
        // Update system tab
        document.getElementById('ipAddress').textContent = data.wifi?.ip || '--';
        document.getElementById('wifiSsid').textContent = data.wifi?.ssid || '--';
        document.getElementById('wifiRssi').textContent = (data.wifi?.rssi || '--') + ' dBm';
        document.getElementById('systemUptime').textContent = formatUptime((data.uptime_s || 0) * 1000);
        document.getElementById('systemFreeHeap').textContent = 
            ((data.free_heap || 0) / 1024).toFixed(1) + ' KB';
        document.getElementById('freeHeap').textContent = 
            ((data.free_heap || 0) / 1024).toFixed(0);
        
        // SPIFFS usage
        if (data.spiffs_used && data.spiffs_total) {
            const percentage = ((data.spiffs_used / data.spiffs_total) * 100).toFixed(1);
            document.getElementById('spiffsUsage').textContent = 
                `${(data.spiffs_used / 1024).toFixed(1)} / ${(data.spiffs_total / 1024).toFixed(1)} KB (${percentage}%)`;
        }
    } catch (err) {
        console.error('Failed to fetch system info:', err);
    }
}

// Fetch system info every 30 seconds
setInterval(fetchSystemInfo, 30000);
