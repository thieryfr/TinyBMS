/**
 * Monitoring Logic
 * Handles CAN/UART message display, filtering, and statistics
 */

// ============================================
// Global Variables
// ============================================

let canMessages = [];
let uartMessages = [];
const MAX_CAN_MESSAGES = 50;
const MAX_UART_MESSAGES = 30;

let canStats = {
    tx: 0,
    rx: 0,
    errors: 0,
    idCounts: {}
};

let uartStats = {
    tx: 0,
    rx: 0,
    errors: 0,
    commandCounts: {}
};

let trafficChart = null;
let trafficData = {
    canTx: [],
    canRx: [],
    uart: [],
    timestamps: []
};
const MAX_TRAFFIC_POINTS = 60; // 1 minute at 1s intervals

let monitoringPaused = false;
let canRateCounter = 0;
let uartRateCounter = 0;
let lastRateUpdate = Date.now();

// ============================================
// Initialize Monitoring
// ============================================

function initMonitoring() {
    console.log('[Monitoring] Initializing...');
    
    // Create traffic chart
    createTrafficChart();
    
    // Setup WebSocket handlers
    wsHandler.on('onMessage', handleMonitoringData);
    
    // Setup filter listeners
    setupFilters();
    
    // Start rate calculation
    setInterval(updateRates, 1000);
    
    console.log('[Monitoring] Initialized');
}

// ============================================
// Traffic Chart
// ============================================

function createTrafficChart() {
    const ctx = document.getElementById('trafficChart');
    if (!ctx) return;
    
    trafficChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [
                {
                    label: 'CAN TX',
                    data: [],
                    borderColor: '#0d6efd',
                    backgroundColor: 'rgba(13, 110, 253, 0.1)',
                    tension: 0.4,
                    fill: true
                },
                {
                    label: 'CAN RX',
                    data: [],
                    borderColor: '#198754',
                    backgroundColor: 'rgba(25, 135, 84, 0.1)',
                    tension: 0.4,
                    fill: true
                },
                {
                    label: 'UART',
                    data: [],
                    borderColor: '#6f42c1',
                    backgroundColor: 'rgba(111, 66, 193, 0.1)',
                    tension: 0.4,
                    fill: true
                }
            ]
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
                    beginAtZero: true,
                    title: {
                        display: true,
                        text: 'Messages/second'
                    }
                }
            }
        }
    });
}

function updateTrafficChart() {
    if (!trafficChart) return;
    
    const now = new Date();
    const timeStr = now.toLocaleTimeString();
    
    trafficData.timestamps.push(timeStr);
    trafficData.canTx.push(canRateCounter);
    trafficData.canRx.push(0); // Will be populated from actual RX data
    trafficData.uart.push(uartRateCounter);
    
    // Keep only last MAX_TRAFFIC_POINTS
    if (trafficData.timestamps.length > MAX_TRAFFIC_POINTS) {
        trafficData.timestamps.shift();
        trafficData.canTx.shift();
        trafficData.canRx.shift();
        trafficData.uart.shift();
    }
    
    trafficChart.data.labels = trafficData.timestamps;
    trafficChart.data.datasets[0].data = trafficData.canTx;
    trafficChart.data.datasets[1].data = trafficData.canRx;
    trafficChart.data.datasets[2].data = trafficData.uart;
    trafficChart.update('none');
}

// ============================================
// Rate Calculation
// ============================================

function updateRates() {
    const now = Date.now();
    const deltaS = (now - lastRateUpdate) / 1000;
    
    // Calculate rates
    const canRate = Math.round(canRateCounter / deltaS);
    const uartRate = Math.round(uartRateCounter / deltaS);
    
    // Update UI
    document.getElementById('canRate').textContent = canRate;
    document.getElementById('uartRate').textContent = uartRate;
    
    // Update average rates
    const avgCanRate = Math.round((canStats.tx + canStats.rx) / (now / 1000));
    const avgUartRate = Math.round((uartStats.tx + uartStats.rx) / (now / 1000));
    
    document.getElementById('canAvgRate').textContent = `${avgCanRate} msg/s`;
    document.getElementById('uartAvgRate').textContent = `${avgUartRate} msg/s`;
    
    // Update traffic chart
    updateTrafficChart();
    
    // Reset counters
    canRateCounter = 0;
    uartRateCounter = 0;
    lastRateUpdate = now;
}

// ============================================
// WebSocket Data Handler
// ============================================

function handleMonitoringData(data) {
    if (monitoringPaused) return;
    
    // Handle CAN messages
    if (data.can_message) {
        addCanMessage(data.can_message);
    }
    
    // Handle UART messages
    if (data.uart_message) {
        addUartMessage(data.uart_message);
    }
    
    // Update last update time
    const now = new Date().toLocaleTimeString();
    document.getElementById('monitorLastUpdate').textContent = now;
}

// ============================================
// CAN Messages
// ============================================

function addCanMessage(msg) {
    // Create message object
    const message = {
        timestamp: new Date().toLocaleTimeString() + '.' + new Date().getMilliseconds().toString().padStart(3, '0'),
        id: msg.id || '0x000',
        dlc: msg.dlc || 0,
        data: msg.data || [],
        direction: msg.direction || 'TX',
        raw: msg
    };
    
    // Add to array
    canMessages.unshift(message);
    
    // Keep only MAX messages
    if (canMessages.length > MAX_CAN_MESSAGES) {
        canMessages.pop();
    }
    
    // Update stats
    if (message.direction === 'TX') {
        canStats.tx++;
    } else {
        canStats.rx++;
    }
    
    // Count ID occurrences
    if (!canStats.idCounts[message.id]) {
        canStats.idCounts[message.id] = 0;
    }
    canStats.idCounts[message.id]++;
    
    // Update rate counter
    canRateCounter++;
    
    // Update display
    updateCanDisplay();
    updateCanStats();
}

function updateCanDisplay() {
    const tbody = document.getElementById('canMessagesTable');
    if (!tbody) return;
    
    // Apply filters
    const filterType = document.getElementById('canFilterType')?.value || 'all';
    const filterId = document.getElementById('canFilterId')?.value.toLowerCase() || '';
    const filterData = document.getElementById('canFilterData')?.value.toLowerCase() || '';
    
    const filtered = canMessages.filter(msg => {
        if (filterType !== 'all' && msg.direction.toLowerCase() !== filterType) return false;
        if (filterId && !msg.id.toLowerCase().includes(filterId)) return false;
        if (filterData && !msg.data.join('').toLowerCase().includes(filterData)) return false;
        return true;
    });
    
    // Update count
    document.getElementById('canMessageCount').textContent = filtered.length;
    
    // Build table HTML
    if (filtered.length === 0) {
        tbody.innerHTML = `
            <tr>
                <td colspan="6" class="text-center text-muted">
                    No messages matching filters
                </td>
            </tr>
        `;
        return;
    }
    
    tbody.innerHTML = filtered.slice(0, 50).map((msg, idx) => {
        const dataHex = msg.data.map(b => b.toString(16).toUpperCase().padStart(2, '0')).join(' ');
        const directionClass = msg.direction === 'TX' ? 'bg-primary' : 'bg-success';
        
        return `
            <tr onclick="showCanMessageDetail(${canMessages.indexOf(msg)})">
                <td><small>${msg.timestamp}</small></td>
                <td><span class="message-id">${msg.id}</span></td>
                <td>${msg.dlc}</td>
                <td><code class="message-data">${dataHex}</code></td>
                <td><span class="badge direction-badge ${directionClass}">${msg.direction}</span></td>
                <td>
                    <button class="btn btn-sm btn-outline-info" onclick="event.stopPropagation(); showCanMessageDetail(${canMessages.indexOf(msg)})">
                        <i class="fas fa-info-circle"></i>
                    </button>
                </td>
            </tr>
        `;
    }).join('');
}

function updateCanStats() {
    document.getElementById('canTxCount').textContent = canStats.tx.toLocaleString();
    document.getElementById('canRxCount').textContent = canStats.rx.toLocaleString();
    document.getElementById('canErrorCount').textContent = canStats.errors.toLocaleString();
    
    // Update top IDs
    const topIds = Object.entries(canStats.idCounts)
        .sort((a, b) => b[1] - a[1])
        .slice(0, 5);
    
    const topIdsHtml = topIds.length > 0 ? topIds.map(([id, count]) => {
        const total = canStats.tx + canStats.rx;
        const percentage = ((count / total) * 100).toFixed(1);
        return `
            <div class="top-id-item">
                <span class="message-id">${id}</span>
                <span><strong>${count}</strong> (${percentage}%)</span>
            </div>
        `;
    }).join('') : '<small class="text-muted">No data yet...</small>';
    
    document.getElementById('canTopIds').innerHTML = topIdsHtml;
}

function clearCanMessages() {
    if (!confirm('Clear all CAN messages?')) return;
    
    canMessages = [];
    canStats = { tx: 0, rx: 0, errors: 0, idCounts: {} };
    updateCanDisplay();
    updateCanStats();
    showToast('CAN messages cleared', 'info');
}

function clearCanFilters() {
    document.getElementById('canFilterType').value = 'all';
    document.getElementById('canFilterId').value = '';
    document.getElementById('canFilterData').value = '';
    updateCanDisplay();
}

function exportCanMessages() {
    if (canMessages.length === 0) {
        showToast('No messages to export', 'warning');
        return;
    }
    
    const csv = generateCanCSV();
    downloadCSV(csv, 'can_messages.csv');
    showToast('CAN messages exported', 'success');
}

function generateCanCSV() {
    let csv = 'Timestamp,ID,DLC,Data,Direction\n';
    
    canMessages.forEach(msg => {
        const dataHex = msg.data.map(b => b.toString(16).toUpperCase().padStart(2, '0')).join(' ');
        csv += `${msg.timestamp},${msg.id},${msg.dlc},"${dataHex}",${msg.direction}\n`;
    });
    
    return csv;
}

// ============================================
// UART Messages
// ============================================

function addUartMessage(msg) {
    // Create message object
    const message = {
        timestamp: new Date().toLocaleTimeString() + '.' + new Date().getMilliseconds().toString().padStart(3, '0'),
        direction: msg.direction || 'RX',
        command: msg.command || 'Unknown',
        data: msg.data || '--',
        status: msg.status || 'OK',
        raw: msg
    };
    
    // Add to array
    uartMessages.unshift(message);
    
    // Keep only MAX messages
    if (uartMessages.length > MAX_UART_MESSAGES) {
        uartMessages.pop();
    }
    
    // Update stats
    if (message.direction === 'TX') {
        uartStats.tx++;
    } else {
        uartStats.rx++;
    }
    
    if (message.status === 'ERROR') {
        uartStats.errors++;
    }
    
    // Count command occurrences
    if (!uartStats.commandCounts[message.command]) {
        uartStats.commandCounts[message.command] = 0;
    }
    uartStats.commandCounts[message.command]++;
    
    // Update rate counter
    uartRateCounter++;
    
    // Update display
    updateUartDisplay();
    updateUartStats();
}

function updateUartDisplay() {
    const tbody = document.getElementById('uartMessagesTable');
    if (!tbody) return;
    
    // Update count
    document.getElementById('uartMessageCount').textContent = uartMessages.length;
    
    if (uartMessages.length === 0) {
        tbody.innerHTML = `
            <tr>
                <td colspan="5" class="text-center text-muted">
                    <i class="fas fa-spinner fa-spin"></i> Waiting for messages...
                </td>
            </tr>
        `;
        return;
    }
    
    tbody.innerHTML = uartMessages.map(msg => {
        const directionClass = msg.direction === 'TX' ? 'bg-primary' : 'bg-success';
        const statusClass = msg.status === 'OK' ? 'bg-success' : 'bg-danger';
        
        return `
            <tr>
                <td><small>${msg.timestamp}</small></td>
                <td><span class="badge direction-badge ${directionClass}">${msg.direction}</span></td>
                <td><strong>${msg.command}</strong></td>
                <td><code class="message-data">${msg.data}</code></td>
                <td><span class="badge ${statusClass}">${msg.status}</span></td>
            </tr>
        `;
    }).join('');
}

function updateUartStats() {
    document.getElementById('uartTxCount').textContent = uartStats.tx.toLocaleString();
    document.getElementById('uartRxCount').textContent = uartStats.rx.toLocaleString();
    document.getElementById('uartErrorCount').textContent = uartStats.errors.toLocaleString();
    
    // Update top commands
    const topCommands = Object.entries(uartStats.commandCounts)
        .sort((a, b) => b[1] - a[1])
        .slice(0, 5);
    
    const topCommandsHtml = topCommands.length > 0 ? topCommands.map(([cmd, count]) => {
        const total = uartStats.tx + uartStats.rx;
        const percentage = ((count / total) * 100).toFixed(1);
        return `
            <div class="top-id-item">
                <span><strong>${cmd}</strong></span>
                <span>${count} (${percentage}%)</span>
            </div>
        `;
    }).join('') : '<small class="text-muted">No data yet...</small>';
    
    document.getElementById('uartTopCommands').innerHTML = topCommandsHtml;
}

function clearUartMessages() {
    if (!confirm('Clear all UART messages?')) return;
    
    uartMessages = [];
    uartStats = { tx: 0, rx: 0, errors: 0, commandCounts: {} };
    updateUartDisplay();
    updateUartStats();
    showToast('UART messages cleared', 'info');
}

function exportUartMessages() {
    if (uartMessages.length === 0) {
        showToast('No messages to export', 'warning');
        return;
    }
    
    const csv = generateUartCSV();
    downloadCSV(csv, 'uart_messages.csv');
    showToast('UART messages exported', 'success');
}

function generateUartCSV() {
    let csv = 'Timestamp,Direction,Command,Data,Status\n';
    
    uartMessages.forEach(msg => {
        csv += `${msg.timestamp},${msg.direction},${msg.command},"${msg.data}",${msg.status}\n`;
    });
    
    return csv;
}

// ============================================
// Message Details Modal
// ============================================

function showCanMessageDetail(index) {
    const msg = canMessages[index];
    if (!msg) return;
    
    // Decode message if possible
    const decoded = decodeCanMessage(msg);
    
    // Create modal
    const modalHtml = `
        <div class="modal fade" id="messageDetailModal" tabindex="-1">
            <div class="modal-dialog modal-lg">
                <div class="modal-content">
                    <div class="modal-header">
                        <h5 class="modal-title">
                            <i class="fas fa-info-circle"></i> CAN Message Details
                        </h5>
                        <button type="button" class="btn-close" data-bs-dismiss="modal"></button>
                    </div>
                    <div class="modal-body">
                        <h6>Raw Message</h6>
                        <div class="message-detail-row">
                            <span class="message-detail-label">Timestamp:</span>
                            <span class="message-detail-value">${msg.timestamp}</span>
                        </div>
                        <div class="message-detail-row">
                            <span class="message-detail-label">ID (Hex):</span>
                            <span class="message-detail-value">${msg.id}</span>
                        </div>
                        <div class="message-detail-row">
                            <span class="message-detail-label">ID (Dec):</span>
                            <span class="message-detail-value">${parseInt(msg.id, 16)}</span>
                        </div>
                        <div class="message-detail-row">
                            <span class="message-detail-label">DLC:</span>
                            <span class="message-detail-value">${msg.dlc} bytes</span>
                        </div>
                        <div class="message-detail-row">
                            <span class="message-detail-label">Data (Hex):</span>
                            <span class="message-detail-value">${msg.data.map(b => b.toString(16).toUpperCase().padStart(2, '0')).join(' ')}</span>
                        </div>
                        <div class="message-detail-row">
                            <span class="message-detail-label">Data (Dec):</span>
                            <span class="message-detail-value">${msg.data.join(', ')}</span>
                        </div>
                        <div class="message-detail-row">
                            <span class="message-detail-label">Direction:</span>
                            <span class="message-detail-value"><span class="badge ${msg.direction === 'TX' ? 'bg-primary' : 'bg-success'}">${msg.direction}</span></span>
                        </div>
                        
                        ${decoded ? `
                            <hr>
                            <h6>Decoded (${decoded.protocol})</h6>
                            ${decoded.fields.map(f => `
                                <div class="message-detail-row">
                                    <span class="message-detail-label">${f.name}:</span>
                                    <span class="message-detail-value">${f.value}</span>
                                </div>
                            `).join('')}
                        ` : `
                            <hr>
                            <div class="alert alert-info mb-0">
                                <i class="fas fa-info-circle"></i> No decoder available for this message ID
                            </div>
                        `}
                    </div>
                    <div class="modal-footer">
                        <button type="button" class="btn btn-secondary" onclick="copyMessageHex(${index})">
                            <i class="fas fa-copy"></i> Copy Hex
                        </button>
                        <button type="button" class="btn btn-primary" onclick="resendMessage(${index})">
                            <i class="fas fa-paper-plane"></i> Resend
                        </button>
                        <button type="button" class="btn btn-secondary" data-bs-dismiss="modal">Close</button>
                    </div>
                </div>
            </div>
        </div>
    `;
    
    // Remove existing modal
    const existing = document.getElementById('messageDetailModal');
    if (existing) existing.remove();
    
    // Add new modal
    document.body.insertAdjacentHTML('beforeend', modalHtml);
    
    // Show modal
    const modal = new bootstrap.Modal(document.getElementById('messageDetailModal'));
    modal.show();
}

function decodeCanMessage(msg) {
    const id = parseInt(msg.id, 16);
    
    // Victron CVL/CCL/DCL (0x351)
    if (id === 0x351 && msg.dlc === 8) {
        const cvl = (msg.data[1] << 8) | msg.data[0];
        const ccl = (msg.data[3] << 8) | msg.data[2];
        const dcl = (msg.data[5] << 8) | msg.data[4];
        const dvl = (msg.data[7] << 8) | msg.data[6];
        
        return {
            protocol: 'Victron PGN 0x351',
            fields: [
                { name: 'CVL (Charge Voltage Limit)', value: `${(cvl / 10).toFixed(1)}V` },
                { name: 'CCL (Charge Current Limit)', value: `${(ccl / 10).toFixed(1)}A` },
                { name: 'DCL (Discharge Current Limit)', value: `${(dcl / 10).toFixed(1)}A` },
                { name: 'DVL (Discharge Voltage Limit)', value: `${(dvl / 10).toFixed(1)}V` }
            ]
        };
    }
    
    // Victron SOC/SOH (0x355)
    if (id === 0x355 && msg.dlc === 8) {
        const soc = (msg.data[1] << 8) | msg.data[0];
        const soh = (msg.data[3] << 8) | msg.data[2];
        
        return {
            protocol: 'Victron PGN 0x355',
            fields: [
                { name: 'SOC (State of Charge)', value: `${(soc / 100).toFixed(1)}%` },
                { name: 'SOH (State of Health)', value: `${(soh / 100).toFixed(1)}%` }
            ]
        };
    }
    
    // Add more decoders as needed
    
    return null;
}

function copyMessageHex(index) {
    const msg = canMessages[index];
    if (!msg) return;
    
    const hex = msg.data.map(b => b.toString(16).toUpperCase().padStart(2, '0')).join(' ');
    copyToClipboard(hex);
}

function resendMessage(index) {
    const msg = canMessages[index];
    if (!msg) return;
    
    // Send via WebSocket
    wsHandler.send({
        action: 'send_can',
        id: msg.id,
        data: msg.data
    });
    
    showToast('Message sent', 'success');
    
    // Close modal
    const modal = bootstrap.Modal.getInstance(document.getElementById('messageDetailModal'));
    if (modal) modal.hide();
}

// ============================================
// Pause/Resume
// ============================================

function toggleMonitorPause() {
    monitoringPaused = !monitoringPaused;
    
    const btn = document.getElementById('monitorPauseBtn');
    if (monitoringPaused) {
        btn.innerHTML = '<i class="fas fa-play"></i> Resume';
        btn.classList.add('btn-paused');
        showToast('Monitoring paused', 'warning');
    } else {
        btn.innerHTML = '<i class="fas fa-pause"></i> Pause';
        btn.classList.remove('btn-paused');
        showToast('Monitoring resumed', 'success');
    }
}

// ============================================
// Filters
// ============================================

function setupFilters() {
    // CAN filters
    const canFilterType = document.getElementById('canFilterType');
    const canFilterId = document.getElementById('canFilterId');
    const canFilterData = document.getElementById('canFilterData');
    
    if (canFilterType) {
        canFilterType.addEventListener('change', updateCanDisplay);
    }
    
    if (canFilterId) {
        canFilterId.addEventListener('input', debounce(updateCanDisplay, 300));
    }
    
    if (canFilterData) {
        canFilterData.addEventListener('input', debounce(updateCanDisplay, 300));
    }
}

// Debounce helper
function debounce(func, wait) {
    let timeout;
    return function(...args) {
        clearTimeout(timeout);
        timeout = setTimeout(() => func.apply(this, args), wait);
    };
}

// ============================================
// CSV Download Helper
// ============================================

function downloadCSV(csv, filename) {
    const blob = new Blob([csv], { type: 'text/csv' });
    const url = window.URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    a.click();
    window.URL.revokeObjectURL(url);
}

// ============================================
// Demo Data Generator (for testing)
// ============================================

function startMonitoringDemo() {
    console.log('[Monitoring] Starting demo mode...');
    
    // Generate fake CAN messages
    setInterval(() => {
        if (monitoringPaused) return;
        
        const ids = ['0x351', '0x355', '0x356', '0x305'];
        const randomId = ids[Math.floor(Math.random() * ids.length)];
        
        const fakeCanMsg = {
            id: randomId,
            dlc: 8,
            data: Array.from({length: 8}, () => Math.floor(Math.random() * 256)),
            direction: Math.random() > 0.5 ? 'TX' : 'RX'
        };
        
        addCanMessage(fakeCanMsg);
    }, 500);
    
    // Generate fake UART messages
    setInterval(() => {
        if (monitoringPaused) return;
        
        const commands = ['Read Reg 36', 'Read Reg 46', 'Write Reg 300', 'Status Request'];
        const randomCmd = commands[Math.floor(Math.random() * commands.length)];
        
        const fakeUartMsg = {
            direction: Math.random() > 0.5 ? 'TX' : 'RX',
            command: randomCmd,
            data: `Value: ${Math.floor(Math.random() * 1000)}`,
            status: Math.random() > 0.9 ? 'ERROR' : 'OK'
        };
        
        addUartMessage(fakeUartMsg);
    }, 800);
}

// ============================================
// Initialize
// ============================================

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initMonitoring);
} else {
    initMonitoring();
}

// Start demo if not connected (for testing UI)
// Uncomment for testing without backend:
// setTimeout(() => {
//     if (!wsHandler.isConnected()) {
//         startMonitoringDemo();
//     }
// }, 2000);
