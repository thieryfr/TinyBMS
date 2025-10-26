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
    
    // Setup WebSocket data handler
    wsHandler.on('onMessage', handleWebSocketData);
    
    // Setup chart data selector
    setupChartSelector();
    
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
    const timeStr = now.toLocaleTimeString();
    
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
        
        // Update progress bar (3000-3700mV range)
        if (barElement) {
            const percentage = Math.max(0, Math.min(100, ((voltage - 3000) / 700) * 100));
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
    if (imbalance < 30) {
        imbalanceStatus.textContent = 'OK';
        imbalanceStatus.className = 'badge bg-success';
    } else if (imbalance < 100) {
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
// WebSocket Data Handler
// ============================================

function handleWebSocketData(data) {
    if (!data || !data.live_data) return;
    
    const liveData = data.live_data;
    const stats = data.stats || {};
    
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
