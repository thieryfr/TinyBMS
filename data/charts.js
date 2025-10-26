/**
 * Chart.js Helper Functions
 * Utility functions for chart creation and management
 */

// ============================================
// Chart Configuration Defaults
// ============================================

const CHART_DEFAULTS = {
    // Colors
    colors: {
        primary: 'rgba(13, 110, 253, 0.8)',
        success: 'rgba(25, 135, 84, 0.8)',
        danger: 'rgba(220, 53, 69, 0.8)',
        warning: 'rgba(255, 193, 7, 0.8)',
        info: 'rgba(13, 202, 240, 0.8)',
        secondary: 'rgba(108, 117, 125, 0.8)'
    },
    
    // Common options
    options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: {
            duration: 300
        }
    }
};

// ============================================
// Chart Helper Functions
// ============================================

/**
 * Create a line chart with default settings
 */
function createLineChart(canvasId, datasets, labels = []) {
    const ctx = document.getElementById(canvasId);
    if (!ctx) return null;
    
    return new Chart(ctx, {
        type: 'line',
        data: {
            labels: labels,
            datasets: datasets.map(ds => ({
                ...ds,
                tension: ds.tension || 0.4,
                borderWidth: ds.borderWidth || 2,
                pointRadius: ds.pointRadius || 0
            }))
        },
        options: {
            ...CHART_DEFAULTS.options,
            ...datasets[0].options
        }
    });
}

/**
 * Update chart data efficiently
 */
function updateChartData(chart, newData, newLabels = null) {
    if (!chart) return;
    
    if (newLabels) {
        chart.data.labels = newLabels;
    }
    
    newData.forEach((data, index) => {
        if (chart.data.datasets[index]) {
            chart.data.datasets[index].data = data;
        }
    });
    
    chart.update('none'); // Update without animation for performance
}

/**
 * Destroy chart safely
 */
function destroyChart(chart) {
    if (chart) {
        chart.destroy();
    }
}

/**
 * Get color by name
 */
function getChartColor(name, alpha = 0.8) {
    const colors = {
        primary: `rgba(13, 110, 253, ${alpha})`,
        success: `rgba(25, 135, 84, ${alpha})`,
        danger: `rgba(220, 53, 69, ${alpha})`,
        warning: `rgba(255, 193, 7, ${alpha})`,
        info: `rgba(13, 202, 240, ${alpha})`,
        secondary: `rgba(108, 117, 125, ${alpha})`
    };
    
    return colors[name] || colors.primary;
}

// ============================================
// Initialize Chart.js Defaults
// ============================================

function initChartDefaults() {
    if (typeof Chart === 'undefined') {
        console.warn('[Charts] Chart.js not loaded yet');
        return;
    }
    
    // Set global defaults
    Chart.defaults.font.family = '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial';
    Chart.defaults.font.size = 12;
    Chart.defaults.color = '#666';
    
    console.log('[Charts] Chart.js defaults initialized');
}

// Initialize when Chart.js is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => {
        setTimeout(initChartDefaults, 100);
    });
} else {
    setTimeout(initChartDefaults, 100);
}
