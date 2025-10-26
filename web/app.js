/**
 * Application Main
 * Global functionality, theme, notifications
 */

// ============================================
// Theme Management
// ============================================

function initTheme() {
    const themeToggle = document.getElementById('themeToggle');
    const savedTheme = localStorage.getItem('theme') || 'light';
    
    // Apply saved theme
    document.documentElement.setAttribute('data-theme', savedTheme);
    updateThemeIcon(savedTheme);
    
    // Theme toggle handler
    if (themeToggle) {
        themeToggle.addEventListener('click', () => {
            const currentTheme = document.documentElement.getAttribute('data-theme');
            const newTheme = currentTheme === 'light' ? 'dark' : 'light';
            
            document.documentElement.setAttribute('data-theme', newTheme);
            localStorage.setItem('theme', newTheme);
            updateThemeIcon(newTheme);
            
            showToast(`Theme switched to ${newTheme} mode`, 'info');
        });
    }
}

function updateThemeIcon(theme) {
    const themeToggle = document.getElementById('themeToggle');
    if (themeToggle) {
        const icon = themeToggle.querySelector('i');
        if (icon) {
            icon.className = theme === 'light' ? 'fas fa-moon' : 'fas fa-sun';
        }
    }
}

// ============================================
// Toast Notifications
// ============================================

function showToast(message, type = 'info', duration = 3000) {
    const container = document.getElementById('toastContainer');
    if (!container) return;
    
    const toastId = `toast-${Date.now()}`;
    const icons = {
        success: 'fa-check-circle',
        error: 'fa-exclamation-circle',
        warning: 'fa-exclamation-triangle',
        info: 'fa-info-circle'
    };
    
    const toast = document.createElement('div');
    toast.id = toastId;
    toast.className = `toast toast-${type} show`;
    toast.setAttribute('role', 'alert');
    
    toast.innerHTML = `
        <div class="toast-header">
            <i class="fas ${icons[type]} me-2"></i>
            <strong class="me-auto">${type.charAt(0).toUpperCase() + type.slice(1)}</strong>
            <button type="button" class="btn-close btn-close-white" onclick="closeToast('${toastId}')"></button>
        </div>
        <div class="toast-body">
            ${message}
        </div>
    `;
    
    container.appendChild(toast);
    
    // Auto-dismiss
    if (duration > 0) {
        setTimeout(() => {
            closeToast(toastId);
        }, duration);
    }
}

function closeToast(toastId) {
    const toast = document.getElementById(toastId);
    if (toast) {
        toast.classList.remove('show');
        setTimeout(() => {
            toast.remove();
        }, 300);
    }
}

// ============================================
// Notifications Badge
// ============================================

let notificationCount = 0;
let notifications = [];

function addNotification(message, type = 'info') {
    const notification = {
        id: Date.now(),
        message,
        type,
        timestamp: new Date()
    };
    
    notifications.unshift(notification);
    notificationCount++;
    
    updateNotificationBadge();
    updateNotificationList();
    
    // Keep only last 20 notifications
    if (notifications.length > 20) {
        notifications = notifications.slice(0, 20);
    }
}

function updateNotificationBadge() {
    const badge = document.getElementById('notifBadge');
    if (badge) {
        if (notificationCount > 0) {
            badge.textContent = notificationCount;
            badge.style.display = 'inline-block';
        } else {
            badge.style.display = 'none';
        }
    }
}

function updateNotificationList() {
    const list = document.getElementById('notificationList');
    if (!list) return;
    
    // Clear existing items (except header and divider)
    while (list.children.length > 2) {
        list.removeChild(list.lastChild);
    }
    
    if (notifications.length === 0) {
        const item = document.createElement('li');
        item.className = 'dropdown-item text-muted';
        item.textContent = 'No notifications';
        list.appendChild(item);
        return;
    }
    
    notifications.forEach(notif => {
        const item = document.createElement('li');
        item.innerHTML = `
            <a class="dropdown-item d-flex justify-content-between align-items-start" href="#">
                <div class="me-auto">
                    <i class="fas fa-circle text-${notif.type} me-2" style="font-size: 0.5rem;"></i>
                    <small>${notif.message}</small>
                </div>
                <small class="text-muted">${formatTimestamp(notif.timestamp)}</small>
            </a>
        `;
        list.appendChild(item);
    });
    
    // Add clear all button
    const divider = document.createElement('li');
    divider.innerHTML = '<hr class="dropdown-divider">';
    list.appendChild(divider);
    
    const clearBtn = document.createElement('li');
    clearBtn.innerHTML = `
        <a class="dropdown-item text-center text-danger" href="#" onclick="clearNotifications()">
            <small><i class="fas fa-trash"></i> Clear All</small>
        </a>
    `;
    list.appendChild(clearBtn);
}

function clearNotifications() {
    notifications = [];
    notificationCount = 0;
    updateNotificationBadge();
    updateNotificationList();
}

function formatTimestamp(date) {
    const now = new Date();
    const diff = Math.floor((now - date) / 1000); // seconds
    
    if (diff < 60) return 'Just now';
    if (diff < 3600) return `${Math.floor(diff / 60)}m ago`;
    if (diff < 86400) return `${Math.floor(diff / 3600)}h ago`;
    return `${Math.floor(diff / 86400)}d ago`;
}

// ============================================
// Tab Management
// ============================================

function showTab(tabName) {
    const tabButton = document.getElementById(`${tabName}-tab`);
    if (tabButton) {
        tabButton.click();
    }
}

// ============================================
// API Calls
// ============================================

async function fetchAPI(endpoint) {
    try {
        const response = await fetch(endpoint);
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        return await response.json();
    } catch (error) {
        console.error(`[API] Error fetching ${endpoint}:`, error);
        return null;
    }
}

async function postAPI(endpoint, data) {
    try {
        const response = await fetch(endpoint, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(data)
        });
        
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        return await response.json();
    } catch (error) {
        console.error(`[API] Error posting to ${endpoint}:`, error);
        return null;
    }
}

// ============================================
// Periodic Updates (Footer data)
// ============================================

async function updateSystemInfo() {
    // Fetch memory info
    const memData = await fetchAPI('/api/memory');
    if (memData) {
        const freeHeapKB = Math.round(memData.free_heap / 1024);
        document.getElementById('freeHeap').textContent = `${freeHeapKB} KB`;
    }
    
    // Fetch system info for RSSI
    const sysData = await fetchAPI('/api/system');
    if (sysData && sysData.wifi) {
        document.getElementById('rssi').textContent = sysData.wifi.rssi || '--';
    }
}

// Update every 10 seconds
setInterval(updateSystemInfo, 10000);

// ============================================
// Auto-refresh Toggle
// ============================================

function setupAutoRefresh() {
    const toggle = document.getElementById('autoRefresh');
    if (toggle) {
        toggle.addEventListener('change', (e) => {
            if (e.target.checked) {
                addLog('Auto-refresh enabled', 'info');
                showToast('Auto-refresh enabled', 'success');
            } else {
                addLog('Auto-refresh paused', 'warning');
                showToast('Auto-refresh paused', 'warning');
            }
        });
    }
}

// ============================================
// Utility Functions
// ============================================

function formatNumber(num, decimals = 2) {
    return Number(num).toFixed(decimals);
}

function formatBytes(bytes) {
    if (bytes === 0) return '0 Bytes';
    
    const k = 1024;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    
    return Math.round(bytes / Math.pow(k, i) * 100) / 100 + ' ' + sizes[i];
}

function copyToClipboard(text) {
    navigator.clipboard.writeText(text).then(() => {
        showToast('Copied to clipboard', 'success');
    }).catch(() => {
        showToast('Failed to copy', 'error');
    });
}

// ============================================
// Keyboard Shortcuts
// ============================================

document.addEventListener('keydown', (e) => {
    // Ctrl+D: Toggle Dashboard
    if (e.ctrlKey && e.key === 'd') {
        e.preventDefault();
        showTab('dashboard');
    }
    
    // Ctrl+M: Toggle Monitoring
    if (e.ctrlKey && e.key === 'm') {
        e.preventDefault();
        showTab('monitoring');
    }
    
    // Ctrl+T: Toggle Theme
    if (e.ctrlKey && e.key === 't') {
        e.preventDefault();
        document.getElementById('themeToggle')?.click();
    }
});

// ============================================
// Error Handling
// ============================================

window.addEventListener('error', (event) => {
    console.error('[Global Error]', event.error);
    addNotification('An error occurred. Check console.', 'error');
});

window.addEventListener('unhandledrejection', (event) => {
    console.error('[Unhandled Promise]', event.reason);
    addNotification('Promise rejection. Check console.', 'error');
});

// ============================================
// Initialize on Load
// ============================================

function initApp() {
    console.log('[App] Initializing...');
    
    // Initialize theme
    initTheme();
    
    // Setup auto-refresh toggle
    setupAutoRefresh();
    
    // Initial system info update
    updateSystemInfo();
    
    // Show welcome notification
    setTimeout(() => {
        showToast('Welcome to TinyBMS-Victron Bridge!', 'success', 5000);
    }, 1000);
    
    console.log('[App] Initialized');
}

// Initialize
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initApp);
} else {
    initApp();
}

// ============================================
// Console Info
// ============================================

console.log('%c🔋 TinyBMS-Victron Bridge v3.0', 'color: #0d6efd; font-size: 16px; font-weight: bold');
console.log('%cArchitecture Modulaire | Session 1 Complete', 'color: #28a745; font-size: 12px');
console.log('%cKeyboard Shortcuts:', 'color: #6c757d; font-size: 11px');
console.log('  Ctrl+D: Dashboard');
console.log('  Ctrl+M: Monitoring');
console.log('  Ctrl+T: Toggle Theme');
