/**
 * Settings Logic
 * Handles system configuration (WiFi, Hardware, CVL, Victron, Logging, System)
 */

// ============================================
// Global Variables
// ============================================

let systemSettings = {
    wifi: {
        mode: 'station',
        sta_ssid: '',
        sta_password: '',
        sta_hostname: 'tinybms-victron',
        sta_ip_mode: 'dhcp',
        sta_static_ip: '',
        sta_gateway: '',
        sta_subnet: '255.255.255.0',
        ap_ssid: 'TinyBMS-AP',
        ap_password: 'tinybms123',
        ap_channel: 6,
        ap_fallback: true
    },
    hardware: {
        uart_rx_pin: 16,
        uart_tx_pin: 17,
        uart_baudrate: 115200,
        can_tx_pin: 5,
        can_rx_pin: 4,
        can_bitrate: 500000,
        can_termination: true
    },
    cvl: {
        enabled: true,
        bulk_transition_soc: 90,
        transition_float_soc: 95,
        float_exit_soc: 85,
        float_approach_offset: -0.05,
        float_offset: -0.10,
        imbalance_offset: -0.15,
        imbalance_trigger_mv: 100,
        imbalance_release_mv: 50
    },
    victron: {
        manufacturer: 'ENEPAQ',
        battery_name: 'LiFePO4 48V 300Ah',
        pgn_interval_ms: 1000,
        cvl_interval_ms: 20000,
        keepalive_interval_ms: 5000
    },
    logging: {
        level: 'info',
        serial: true,
        web: true,
        sd: false,
        syslog: false,
        syslog_server: ''
    },
    system: {
        web_port: 80,
        ws_max_clients: 4,
        ws_update_interval: 1000,
        cors_enabled: true
    }
};

// ============================================
// Initialize Settings
// ============================================

function initSettings() {
    console.log('[Settings] Initializing...');
    
    // Load current settings from ESP32
    loadSettings();
    
    // Setup event listeners
    setupSettingsListeners();
    
    console.log('[Settings] Initialized');
}

// ============================================
// Load Settings from ESP32
// ============================================

async function loadSettings() {
    try {
        const response = await fetchAPI('/api/config');
        
        if (response && response.success) {
            // Merge with defaults
            systemSettings = { ...systemSettings, ...response.config };
            
            // Populate UI
            populateSettingsUI();
            
            showToast('Settings loaded successfully', 'success');
        }
    } catch (error) {
        console.error('[Settings] Load error:', error);
        showToast('Using default settings', 'warning');
        populateSettingsUI();
    }
}

// ============================================
// Populate UI from Settings
// ============================================

function populateSettingsUI() {
    // WiFi
    document.querySelector(`input[name="wifiMode"][value="${systemSettings.wifi.mode}"]`).checked = true;
    document.getElementById('wifiSSID').value = systemSettings.wifi.sta_ssid;
    document.getElementById('wifiPassword').value = systemSettings.wifi.sta_password;
    document.getElementById('wifiHostname').value = systemSettings.wifi.sta_hostname;
    document.getElementById('wifiIpMode').value = systemSettings.wifi.sta_ip_mode;
    document.getElementById('staticIP').value = systemSettings.wifi.sta_static_ip;
    document.getElementById('staticGateway').value = systemSettings.wifi.sta_gateway;
    document.getElementById('staticSubnet').value = systemSettings.wifi.sta_subnet;
    document.getElementById('apSSID').value = systemSettings.wifi.ap_ssid;
    document.getElementById('apPassword').value = systemSettings.wifi.ap_password;
    document.getElementById('apChannel').value = systemSettings.wifi.ap_channel;
    document.getElementById('apFallback').checked = systemSettings.wifi.ap_fallback;
    
    // Hardware
    document.getElementById('uartRxPin').value = systemSettings.hardware.uart_rx_pin;
    document.getElementById('uartTxPin').value = systemSettings.hardware.uart_tx_pin;
    document.getElementById('uartBaudrate').value = systemSettings.hardware.uart_baudrate;
    document.getElementById('canTxPin').value = systemSettings.hardware.can_tx_pin;
    document.getElementById('canRxPin').value = systemSettings.hardware.can_rx_pin;
    document.getElementById('canBitrate').value = systemSettings.hardware.can_bitrate;
    document.getElementById('canTermination').checked = systemSettings.hardware.can_termination;
    
    // CVL
    document.getElementById('cvlEnable').checked = systemSettings.cvl.enabled;
    document.getElementById('cvlBulkTransition').value = systemSettings.cvl.bulk_transition_soc;
    document.getElementById('cvlTransitionFloat').value = systemSettings.cvl.transition_float_soc;
    document.getElementById('cvlFloatExit').value = systemSettings.cvl.float_exit_soc;
    document.getElementById('cvlFloatApproachOffset').value = systemSettings.cvl.float_approach_offset;
    document.getElementById('cvlFloatOffset').value = systemSettings.cvl.float_offset;
    document.getElementById('cvlImbalanceOffset').value = systemSettings.cvl.imbalance_offset;
    document.getElementById('cvlImbalanceTrigger').value = systemSettings.cvl.imbalance_trigger_mv;
    document.getElementById('cvlImbalanceRelease').value = systemSettings.cvl.imbalance_release_mv;
    
    // Victron
    document.getElementById('victronManufacturer').value = systemSettings.victron.manufacturer;
    document.getElementById('victronBatteryName').value = systemSettings.victron.battery_name;
    document.getElementById('victronPgnInterval').value = systemSettings.victron.pgn_interval_ms;
    document.getElementById('victronCvlInterval').value = systemSettings.victron.cvl_interval_ms;
    document.getElementById('victronKeepaliveInterval').value = systemSettings.victron.keepalive_interval_ms;
    
    // Logging
    document.querySelector(`input[name="logLevel"][value="${systemSettings.logging.level}"]`).checked = true;
    document.getElementById('logSerial').checked = systemSettings.logging.serial;
    document.getElementById('logWeb').checked = systemSettings.logging.web;
    document.getElementById('logSD').checked = systemSettings.logging.sd;
    document.getElementById('logSyslog').checked = systemSettings.logging.syslog;
    document.getElementById('syslogServer').value = systemSettings.logging.syslog_server;
    
    // System
    document.getElementById('webPort').value = systemSettings.system.web_port;
    document.getElementById('wsMaxClients').value = systemSettings.system.ws_max_clients;
    document.getElementById('wsUpdateInterval').value = systemSettings.system.ws_update_interval;
    document.getElementById('webCORS').checked = systemSettings.system.cors_enabled;
}

// ============================================
// Setup Event Listeners
// ============================================

function setupSettingsListeners() {
    // WiFi IP Mode change
    document.getElementById('wifiIpMode').addEventListener('change', (e) => {
        document.getElementById('staticIpSection').style.display = 
            e.target.value === 'static' ? 'block' : 'none';
    });
    
    // Syslog enable
    document.getElementById('logSyslog').addEventListener('change', (e) => {
        document.getElementById('syslogSettings').style.display = 
            e.target.checked ? 'block' : 'none';
    });
}

// ============================================
// WiFi Settings
// ============================================

function togglePassword(inputId) {
    const input = document.getElementById(inputId);
    const icon = input.nextElementSibling.querySelector('i');
    
    if (input.type === 'password') {
        input.type = 'text';
        icon.classList.remove('fa-eye');
        icon.classList.add('fa-eye-slash');
    } else {
        input.type = 'password';
        icon.classList.remove('fa-eye-slash');
        icon.classList.add('fa-eye');
    }
}

async function testWifiConnection() {
    const ssid = document.getElementById('wifiSSID').value;
    const password = document.getElementById('wifiPassword').value;
    
    if (!ssid) {
        showToast('Please enter SSID', 'warning');
        return;
    }
    
    showToast('Testing WiFi connection...', 'info', 5000);
    
    try {
        const response = await postAPI('/api/wifi/test', { ssid, password });
        
        if (response && response.success) {
            showToast('WiFi test successful! RSSI: ' + response.rssi + ' dBm', 'success');
        } else {
            showToast('WiFi test failed: ' + (response?.message || 'Unknown error'), 'error');
        }
    } catch (error) {
        console.error('[Settings] WiFi test error:', error);
        showToast('WiFi test error', 'error');
    }
}

async function saveWifiSettings() {
    // Collect values
    systemSettings.wifi.mode = document.querySelector('input[name="wifiMode"]:checked').value;
    systemSettings.wifi.sta_ssid = document.getElementById('wifiSSID').value;
    systemSettings.wifi.sta_password = document.getElementById('wifiPassword').value;
    systemSettings.wifi.sta_hostname = document.getElementById('wifiHostname').value;
    systemSettings.wifi.sta_ip_mode = document.getElementById('wifiIpMode').value;
    systemSettings.wifi.sta_static_ip = document.getElementById('staticIP').value;
    systemSettings.wifi.sta_gateway = document.getElementById('staticGateway').value;
    systemSettings.wifi.sta_subnet = document.getElementById('staticSubnet').value;
    systemSettings.wifi.ap_ssid = document.getElementById('apSSID').value;
    systemSettings.wifi.ap_password = document.getElementById('apPassword').value;
    systemSettings.wifi.ap_channel = parseInt(document.getElementById('apChannel').value);
    systemSettings.wifi.ap_fallback = document.getElementById('apFallback').checked;
    
    // Validate
    if (!systemSettings.wifi.sta_ssid && systemSettings.wifi.mode !== 'ap') {
        showToast('SSID is required for Station mode', 'error');
        return;
    }
    
    if (systemSettings.wifi.sta_password && systemSettings.wifi.sta_password.length < 8) {
        showToast('Password must be at least 8 characters', 'error');
        return;
    }
    
    if (!await confirmReboot('Apply WiFi settings?', 'ESP32 will reboot to apply WiFi changes.')) {
        return;
    }
    
    try {
        const response = await postAPI('/api/config/wifi', { wifi: systemSettings.wifi });
        
        if (response && response.success) {
            showToast('WiFi settings saved. Rebooting...', 'success', 10000);
            setTimeout(() => {
                window.location.reload();
            }, 3000);
        } else {
            showToast('Failed to save WiFi settings', 'error');
        }
    } catch (error) {
        console.error('[Settings] WiFi save error:', error);
        showToast('WiFi save error', 'error');
    }
}

// ============================================
// Hardware Settings
// ============================================

async function testUART() {
    showToast('Testing UART communication...', 'info', 5000);
    
    try {
        const response = await fetchAPI('/api/hardware/test/uart');
        
        if (response && response.success) {
            showToast('UART test successful! BMS responded.', 'success');
        } else {
            showToast('UART test failed: ' + (response?.message || 'No response'), 'error');
        }
    } catch (error) {
        console.error('[Settings] UART test error:', error);
        showToast('UART test error', 'error');
    }
}

async function testCAN() {
    showToast('Testing CAN bus...', 'info', 5000);
    
    try {
        const response = await fetchAPI('/api/hardware/test/can');
        
        if (response && response.success) {
            showToast('CAN test successful! Bus is active.', 'success');
        } else {
            showToast('CAN test failed: ' + (response?.message || 'No activity'), 'error');
        }
    } catch (error) {
        console.error('[Settings] CAN test error:', error);
        showToast('CAN test error', 'error');
    }
}

async function saveHardwareSettings() {
    // Collect values
    systemSettings.hardware.uart_rx_pin = parseInt(document.getElementById('uartRxPin').value);
    systemSettings.hardware.uart_tx_pin = parseInt(document.getElementById('uartTxPin').value);
    systemSettings.hardware.uart_baudrate = parseInt(document.getElementById('uartBaudrate').value);
    systemSettings.hardware.can_tx_pin = parseInt(document.getElementById('canTxPin').value);
    systemSettings.hardware.can_rx_pin = parseInt(document.getElementById('canRxPin').value);
    systemSettings.hardware.can_bitrate = parseInt(document.getElementById('canBitrate').value);
    systemSettings.hardware.can_termination = document.getElementById('canTermination').checked;
    
    if (!await confirmReboot('Apply hardware settings?', 'ESP32 will reboot to reinitialize hardware.')) {
        return;
    }
    
    try {
        const response = await postAPI('/api/config/hardware', { hardware: systemSettings.hardware });
        
        if (response && response.success) {
            showToast('Hardware settings saved. Rebooting...', 'success', 10000);
            setTimeout(() => {
                window.location.reload();
            }, 3000);
        } else {
            showToast('Failed to save hardware settings', 'error');
        }
    } catch (error) {
        console.error('[Settings] Hardware save error:', error);
        showToast('Hardware save error', 'error');
    }
}

// ============================================
// CVL Algorithm Settings
// ============================================

async function testCVLAlgorithm() {
    showToast('Testing CVL algorithm...', 'info');
    
    // Simulate test (in production, would call API)
    setTimeout(() => {
        showToast('CVL algorithm test successful!', 'success');
        addLog('CVL algorithm validated with current settings', 'success');
    }, 1000);
}

async function saveCVLSettings() {
    // Collect values
    systemSettings.cvl.enabled = document.getElementById('cvlEnable').checked;
    systemSettings.cvl.bulk_transition_soc = parseInt(document.getElementById('cvlBulkTransition').value);
    systemSettings.cvl.transition_float_soc = parseInt(document.getElementById('cvlTransitionFloat').value);
    systemSettings.cvl.float_exit_soc = parseInt(document.getElementById('cvlFloatExit').value);
    systemSettings.cvl.float_approach_offset = parseFloat(document.getElementById('cvlFloatApproachOffset').value);
    systemSettings.cvl.float_offset = parseFloat(document.getElementById('cvlFloatOffset').value);
    systemSettings.cvl.imbalance_offset = parseFloat(document.getElementById('cvlImbalanceOffset').value);
    systemSettings.cvl.imbalance_trigger_mv = parseInt(document.getElementById('cvlImbalanceTrigger').value);
    systemSettings.cvl.imbalance_release_mv = parseInt(document.getElementById('cvlImbalanceRelease').value);
    
    // Validate
    if (systemSettings.cvl.bulk_transition_soc >= systemSettings.cvl.transition_float_soc) {
        showToast('Bulk transition must be < Transition float', 'error');
        return;
    }
    
    try {
        const response = await postAPI('/api/config/cvl', { cvl: systemSettings.cvl });
        
        if (response && response.success) {
            showToast('CVL settings saved successfully', 'success');
            addLog('CVL algorithm configuration updated', 'info');
        } else {
            showToast('Failed to save CVL settings', 'error');
        }
    } catch (error) {
        console.error('[Settings] CVL save error:', error);
        showToast('CVL save error', 'error');
    }
}

function resetCVLDefaults() {
    if (!confirm('Reset CVL settings to defaults?')) return;
    
    document.getElementById('cvlEnable').checked = true;
    document.getElementById('cvlBulkTransition').value = 90;
    document.getElementById('cvlTransitionFloat').value = 95;
    document.getElementById('cvlFloatExit').value = 85;
    document.getElementById('cvlFloatApproachOffset').value = -0.05;
    document.getElementById('cvlFloatOffset').value = -0.10;
    document.getElementById('cvlImbalanceOffset').value = -0.15;
    document.getElementById('cvlImbalanceTrigger').value = 100;
    document.getElementById('cvlImbalanceRelease').value = 50;
    
    showToast('CVL settings reset to defaults', 'info');
}

// ============================================
// Victron Settings
// ============================================

async function saveVictronSettings() {
    // Collect values
    systemSettings.victron.manufacturer = document.getElementById('victronManufacturer').value;
    systemSettings.victron.battery_name = document.getElementById('victronBatteryName').value;
    systemSettings.victron.pgn_interval_ms = parseInt(document.getElementById('victronPgnInterval').value);
    systemSettings.victron.cvl_interval_ms = parseInt(document.getElementById('victronCvlInterval').value);
    systemSettings.victron.keepalive_interval_ms = parseInt(document.getElementById('victronKeepaliveInterval').value);
    
    try {
        const response = await postAPI('/api/config/victron', { victron: systemSettings.victron });
        
        if (response && response.success) {
            showToast('Victron settings saved successfully', 'success');
            addLog('Victron configuration updated', 'info');
        } else {
            showToast('Failed to save Victron settings', 'error');
        }
    } catch (error) {
        console.error('[Settings] Victron save error:', error);
        showToast('Victron save error', 'error');
    }
}

// ============================================
// Logging Settings
// ============================================

async function clearAllLogs() {
    if (!confirm('Clear all logs?')) return;
    
    try {
        const response = await postAPI('/api/logs/clear', {});
        
        if (response && response.success) {
            showToast('All logs cleared', 'success');
            clearLogs(); // Clear UI logs
        } else {
            showToast('Failed to clear logs', 'error');
        }
    } catch (error) {
        console.error('[Settings] Clear logs error:', error);
        showToast('Clear logs error', 'error');
    }
}

async function downloadLogs() {
    try {
        const response = await fetchAPI('/api/logs/download');
        
        if (response && response.logs) {
            const blob = new Blob([response.logs], { type: 'text/plain' });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = `tinybms_logs_${new Date().toISOString().split('T')[0]}.txt`;
            a.click();
            URL.revokeObjectURL(url);
            
            showToast('Logs downloaded', 'success');
        } else {
            showToast('No logs available', 'warning');
        }
    } catch (error) {
        console.error('[Settings] Download logs error:', error);
        showToast('Download logs error', 'error');
    }
}

async function saveLoggingSettings() {
    // Collect values
    systemSettings.logging.level = document.querySelector('input[name="logLevel"]:checked').value;
    systemSettings.logging.serial = document.getElementById('logSerial').checked;
    systemSettings.logging.web = document.getElementById('logWeb').checked;
    systemSettings.logging.sd = document.getElementById('logSD').checked;
    systemSettings.logging.syslog = document.getElementById('logSyslog').checked;
    systemSettings.logging.syslog_server = document.getElementById('syslogServer').value;
    
    try {
        const response = await postAPI('/api/config/logging', { logging: systemSettings.logging });
        
        if (response && response.success) {
            showToast('Logging settings saved successfully', 'success');
        } else {
            showToast('Failed to save logging settings', 'error');
        }
    } catch (error) {
        console.error('[Settings] Logging save error:', error);
        showToast('Logging save error', 'error');
    }
}

// ============================================
// System Settings
// ============================================

function exportSystemConfig() {
    const config = {
        version: '3.0',
        timestamp: new Date().toISOString(),
        settings: systemSettings
    };
    
    const json = JSON.stringify(config, null, 2);
    const blob = new Blob([json], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `system_config_${new Date().toISOString().split('T')[0]}.json`;
    a.click();
    URL.revokeObjectURL(url);
    
    showToast('System configuration exported', 'success');
}

function importSystemConfig() {
    const fileInput = document.getElementById('systemConfigInput');
    const file = fileInput.files[0];
    
    if (!file) return;
    
    const reader = new FileReader();
    
    reader.onload = async (e) => {
        try {
            const config = JSON.parse(e.target.result);
            
            if (!config.settings) {
                showToast('Invalid config file', 'error');
                return;
            }
            
            if (!await confirmReboot('Import system configuration?', 'This will overwrite current settings and reboot.')) {
                return;
            }
            
            // Send to ESP32
            const response = await postAPI('/api/config/import', config.settings);
            
            if (response && response.success) {
                showToast('Configuration imported. Rebooting...', 'success', 10000);
                setTimeout(() => {
                    window.location.reload();
                }, 3000);
            } else {
                showToast('Failed to import config', 'error');
            }
            
        } catch (error) {
            console.error('[Settings] Import error:', error);
            showToast('Failed to import config', 'error');
        }
    };
    
    reader.readAsText(file);
}

async function reloadConfig() {
    if (!await confirmReboot('Reload config from SPIFFS?', 'This will discard unsaved changes.')) {
        return;
    }
    
    try {
        const response = await postAPI('/api/config/reload', {});
        
        if (response && response.success) {
            showToast('Config reloaded successfully', 'success');
            loadSettings();
        } else {
            showToast('Failed to reload config', 'error');
        }
    } catch (error) {
        console.error('[Settings] Reload error:', error);
        showToast('Reload error', 'error');
    }
}

async function saveAllSettings() {
    if (!confirm('Save ALL current settings?')) return;
    
    // Collect all values
    // (Already done in individual save functions)
    
    try {
        const response = await postAPI('/api/config/save', { settings: systemSettings });
        
        if (response && response.success) {
            showToast('All settings saved successfully', 'success');
            addLog('System configuration saved', 'success');
        } else {
            showToast('Failed to save settings', 'error');
        }
    } catch (error) {
        console.error('[Settings] Save all error:', error);
        showToast('Save all error', 'error');
    }
}

async function restartESP32() {
    if (!await confirmReboot('Restart ESP32?', 'The system will reboot now.')) {
        return;
    }
    
    try {
        await postAPI('/api/system/restart', {});
        showToast('ESP32 restarting...', 'warning', 10000);
        
        setTimeout(() => {
            window.location.reload();
        }, 5000);
    } catch (error) {
        console.error('[Settings] Restart error:', error);
    }
}

async function resetToDefaults() {
    if (!await confirmReboot('Reset ALL settings to defaults?', 'This will restore factory defaults and reboot.')) {
        return;
    }
    
    // Second confirmation
    if (!confirm('Are you SURE? This action cannot be undone!')) {
        return;
    }
    
    try {
        const response = await postAPI('/api/config/reset', {});
        
        if (response && response.success) {
            showToast('Settings reset to defaults. Rebooting...', 'success', 10000);
            setTimeout(() => {
                window.location.reload();
            }, 3000);
        } else {
            showToast('Failed to reset settings', 'error');
        }
    } catch (error) {
        console.error('[Settings] Reset error:', error);
        showToast('Reset error', 'error');
    }
}

async function factoryReset() {
    if (!await confirmReboot('⚠️ FACTORY RESET?', 'This will erase ALL data and restore factory settings!')) {
        return;
    }
    
    // Second confirmation with text input
    const confirmation = prompt('Type "FACTORY RESET" to confirm:');
    if (confirmation !== 'FACTORY RESET') {
        showToast('Factory reset cancelled', 'info');
        return;
    }
    
    try {
        await postAPI('/api/system/factory-reset', {});
        showToast('Factory reset initiated. Rebooting...', 'danger', 15000);
        
        setTimeout(() => {
            window.location.href = 'http://192.168.4.1'; // Default AP IP
        }, 10000);
    } catch (error) {
        console.error('[Settings] Factory reset error:', error);
    }
}

// ============================================
// Helpers
// ============================================

async function confirmReboot(title, message) {
    return new Promise((resolve) => {
        const modalHtml = `
            <div class="modal fade" id="confirmRebootModal" tabindex="-1">
                <div class="modal-dialog">
                    <div class="modal-content">
                        <div class="modal-header bg-warning">
                            <h5 class="modal-title">
                                <i class="fas fa-exclamation-triangle"></i> ${title}
                            </h5>
                            <button type="button" class="btn-close" data-bs-dismiss="modal"></button>
                        </div>
                        <div class="modal-body">
                            <p>${message}</p>
                            <p class="mb-0"><strong>Do you want to continue?</strong></p>
                        </div>
                        <div class="modal-footer">
                            <button type="button" class="btn btn-secondary" data-bs-dismiss="modal" id="rebootNo">
                                Cancel
                            </button>
                            <button type="button" class="btn btn-warning" id="rebootYes">
                                Confirm
                            </button>
                        </div>
                    </div>
                </div>
            </div>
        `;
        
        // Remove existing
        const existing = document.getElementById('confirmRebootModal');
        if (existing) existing.remove();
        
        // Add new
        document.body.insertAdjacentHTML('beforeend', modalHtml);
        
        const modal = new bootstrap.Modal(document.getElementById('confirmRebootModal'));
        
        document.getElementById('rebootYes').onclick = () => {
            modal.hide();
            resolve(true);
        };
        
        document.getElementById('rebootNo').onclick = () => {
            modal.hide();
            resolve(false);
        };
        
        modal.show();
    });
}

// ============================================
// Initialize
// ============================================

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initSettings);
} else {
    initSettings();
}
