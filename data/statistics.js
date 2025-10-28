/**
 * Statistics & Analytics Logic
 * Handles KPIs, multi-series charts, tables, and data aggregation
 */

// ============================================
// Global Variables
// ============================================

let statsData = {
    period: '24h',
    startDate: null,
    endDate: null,
    kpis: {},
    history: {
        soc: [],
        voltage: [],
        current: [],
        power: [],
        temperature: [],
        timestamps: []
    },
    events: []
};

let statsCharts = {
    socVoltage: null,
    energyPie: null,
    currentPower: null,
    temperatureHistogram: null,
    dailySummary: null
};

// ============================================
// Initialize Statistics
// ============================================

function initStatistics() {
    console.log('[Statistics] Initializing...');
    
    // Create all charts
    createSocVoltageChart();
    createEnergyPieChart();
    createCurrentPowerChart();
    createTemperatureHistogram();
    createDailySummaryChart();
    
    // Load initial data
    loadStatisticsData();
    
    // Setup period selector
    setupPeriodSelector();
    
    // Auto-refresh every 30s
    setInterval(refreshStatistics, 30000);
    
    console.log('[Statistics] Initialized');
}

// ============================================
// Period Selector
// ============================================

function setupPeriodSelector() {
    const periodSelect = document.getElementById('statsPeriod');
    if (periodSelect) {
        periodSelect.addEventListener('change', function() {
            if (this.value === 'custom') {
                document.getElementById('customDateStart').style.display = 'block';
                document.getElementById('customDateEnd').style.display = 'block';
            } else {
                document.getElementById('customDateStart').style.display = 'none';
                document.getElementById('customDateEnd').style.display = 'none';
                updateStatisticsPeriod();
            }
        });
    }
}

function updateStatisticsPeriod() {
    statsData.period = document.getElementById('statsPeriod').value;
    refreshStatistics();
}

function applyCustomPeriod() {
    const start = document.getElementById('statsStartDate').value;
    const end = document.getElementById('statsEndDate').value;
    
    if (!start || !end) {
        showToast('Please select both start and end dates', 'warning');
        return;
    }
    
    statsData.startDate = new Date(start);
    statsData.endDate = new Date(end);
    statsData.period = 'custom';
    
    refreshStatistics();
}

// ============================================
// Load Statistics Data
// ============================================

async function loadStatisticsData() {
    try {
        const params = {
            period: statsData.period,
            start: statsData.startDate?.toISOString(),
            end: statsData.endDate?.toISOString()
        };
        
        const response = await fetchAPI('/api/statistics', params);
        
        if (response && response.success && response.data) {
            statsData = { ...statsData, ...response.data };
            updateAllStatistics();
        } else {
            // Generate demo data
            generateDemoStatistics();
        }
    } catch (error) {
        console.error('[Statistics] Load error:', error);
        generateDemoStatistics();
    }
}

function refreshStatistics() {
    showToast('Refreshing statistics...', 'info', 2000);
    loadStatisticsData();
}

// ============================================
// Update All Statistics
// ============================================

function updateAllStatistics() {
    updateKPIs();
    updateCharts();
    updateTables();
    updateEventsLog();
}

// ============================================
// KPI Cards
// ============================================

function updateKPIs() {
    // Average SOC
    const avgSoc = statsData.kpis.avg_soc || 0;
    const socTrend = statsData.kpis.soc_trend || 0;
    document.getElementById('kpiAvgSoc').textContent = `${avgSoc.toFixed(1)}%`;
    
    const socTrendBadge = document.getElementById('kpiSocTrend');
    if (socTrend >= 0) {
        socTrendBadge.className = 'badge bg-success';
        socTrendBadge.innerHTML = `<i class="fas fa-arrow-up"></i> +${socTrend.toFixed(1)}%`;
    } else {
        socTrendBadge.className = 'badge bg-danger';
        socTrendBadge.innerHTML = `<i class="fas fa-arrow-down"></i> ${socTrend.toFixed(1)}%`;
    }
    
    // Energy Charged
    const energyCharged = statsData.kpis.energy_charged || 0;
    document.getElementById('kpiEnergyCharged').textContent = `${energyCharged.toFixed(1)} kWh`;
    document.getElementById('kpiChargedTrend').innerHTML = `<i class="fas fa-arrow-up"></i> ${energyCharged.toFixed(0)} kWh`;
    
    // Average Temperature
    const avgTemp = statsData.kpis.avg_temp || 0;
    const tempTrend = statsData.kpis.temp_trend || 0;
    document.getElementById('kpiAvgTemp').textContent = `${avgTemp.toFixed(1)}°C`;
    
    const tempTrendBadge = document.getElementById('kpiTempTrend');
    if (tempTrend >= 0) {
        tempTrendBadge.className = 'badge bg-warning';
        tempTrendBadge.innerHTML = `<i class="fas fa-arrow-up"></i> +${tempTrend.toFixed(1)}°C`;
    } else {
        tempTrendBadge.className = 'badge bg-success';
        tempTrendBadge.innerHTML = `<i class="fas fa-arrow-down"></i> ${tempTrend.toFixed(1)}°C`;
    }
    
    // Total Cycles
    const cycles = statsData.kpis.total_cycles || 0;
    const cyclesDelta = statsData.kpis.cycles_delta || 0;
    document.getElementById('kpiCycles').textContent = cycles;
    document.getElementById('kpiCyclesTrend').textContent = `+${cyclesDelta} cycles`;
}

// ============================================
// SOC & Voltage Chart
// ============================================

function createSocVoltageChart() {
    const ctx = document.getElementById('socVoltageChart');
    if (!ctx) return;
    
    statsCharts.socVoltage = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [
                {
                    label: 'SOC (%)',
                    data: [],
                    borderColor: '#0d6efd',
                    backgroundColor: 'rgba(13, 110, 253, 0.1)',
                    yAxisID: 'y',
                    tension: 0.4,
                    fill: true
                },
                {
                    label: 'Voltage (V)',
                    data: [],
                    borderColor: '#198754',
                    backgroundColor: 'rgba(25, 135, 84, 0.1)',
                    yAxisID: 'y1',
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
                    type: 'linear',
                    display: true,
                    position: 'left',
                    title: {
                        display: true,
                        text: 'SOC (%)'
                    },
                    min: 0,
                    max: 100
                },
                y1: {
                    type: 'linear',
                    display: true,
                    position: 'right',
                    title: {
                        display: true,
                        text: 'Voltage (V)'
                    },
                    grid: {
                        drawOnChartArea: false
                    }
                }
            }
        }
    });
}

function updateSocVoltageChart() {
    if (!statsCharts.socVoltage) return;
    
    statsCharts.socVoltage.data.labels = statsData.history.timestamps;
    statsCharts.socVoltage.data.datasets[0].data = statsData.history.soc;
    statsCharts.socVoltage.data.datasets[1].data = statsData.history.voltage;
    statsCharts.socVoltage.update('none');
}

// ============================================
// Energy Pie Chart
// ============================================

function createEnergyPieChart() {
    const ctx = document.getElementById('energyPieChart');
    if (!ctx) return;
    
    statsCharts.energyPie = new Chart(ctx, {
        type: 'doughnut',
        data: {
            labels: ['Charged', 'Discharged', 'Lost'],
            datasets: [{
                data: [50, 45, 5],
                backgroundColor: [
                    'rgba(25, 135, 84, 0.8)',
                    'rgba(13, 110, 253, 0.8)',
                    'rgba(220, 53, 69, 0.8)'
                ],
                borderWidth: 2,
                borderColor: '#fff'
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: {
                    display: true,
                    position: 'bottom'
                },
                tooltip: {
                    callbacks: {
                        label: function(context) {
                            const label = context.label || '';
                            const value = context.parsed || 0;
                            const total = context.dataset.data.reduce((a, b) => a + b, 0);
                            const percentage = ((value / total) * 100).toFixed(1);
                            return `${label}: ${value} kWh (${percentage}%)`;
                        }
                    }
                }
            }
        }
    });
}

function updateEnergyPieChart() {
    if (!statsCharts.energyPie) return;
    
    const charged = statsData.kpis.energy_charged || 0;
    const discharged = statsData.kpis.energy_discharged || 0;
    const lost = charged - discharged;
    
    statsCharts.energyPie.data.datasets[0].data = [charged, discharged, Math.max(0, lost)];
    statsCharts.energyPie.update('none');
}

// ============================================
// Current & Power Chart
// ============================================

function createCurrentPowerChart() {
    const ctx = document.getElementById('currentPowerChart');
    if (!ctx) return;
    
    statsCharts.currentPower = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [
                {
                    label: 'Current (A)',
                    data: [],
                    borderColor: '#ffc107',
                    backgroundColor: 'rgba(255, 193, 7, 0.1)',
                    yAxisID: 'y',
                    tension: 0.4,
                    fill: true
                },
                {
                    label: 'Power (W)',
                    data: [],
                    borderColor: '#dc3545',
                    backgroundColor: 'rgba(220, 53, 69, 0.1)',
                    yAxisID: 'y1',
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
                    type: 'linear',
                    display: true,
                    position: 'left',
                    title: {
                        display: true,
                        text: 'Current (A)'
                    }
                },
                y1: {
                    type: 'linear',
                    display: true,
                    position: 'right',
                    title: {
                        display: true,
                        text: 'Power (W)'
                    },
                    grid: {
                        drawOnChartArea: false
                    }
                }
            }
        }
    });
}

function updateCurrentPowerChart() {
    if (!statsCharts.currentPower) return;
    
    statsCharts.currentPower.data.labels = statsData.history.timestamps;
    statsCharts.currentPower.data.datasets[0].data = statsData.history.current;
    statsCharts.currentPower.data.datasets[1].data = statsData.history.power;
    statsCharts.currentPower.update('none');
}

// ============================================
// Temperature Histogram
// ============================================

function createTemperatureHistogram() {
    const ctx = document.getElementById('temperatureHistogram');
    if (!ctx) return;
    
    statsCharts.temperatureHistogram = new Chart(ctx, {
        type: 'bar',
        data: {
            labels: ['<15°C', '15-20°C', '20-25°C', '25-30°C', '30-35°C', '>35°C'],
            datasets: [{
                label: 'Frequency',
                data: [5, 120, 350, 280, 45, 2],
                backgroundColor: [
                    'rgba(13, 110, 253, 0.8)',
                    'rgba(25, 135, 84, 0.8)',
                    'rgba(25, 135, 84, 0.8)',
                    'rgba(255, 193, 7, 0.8)',
                    'rgba(255, 193, 7, 0.8)',
                    'rgba(220, 53, 69, 0.8)'
                ],
                borderWidth: 1,
                borderColor: '#fff'
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: {
                    display: false
                }
            },
            scales: {
                x: {
                    title: {
                        display: true,
                        text: 'Temperature Range'
                    }
                },
                y: {
                    beginAtZero: true,
                    title: {
                        display: true,
                        text: 'Occurrences'
                    }
                }
            }
        }
    });
}

function updateTemperatureHistogram() {
    if (!statsCharts.temperatureHistogram) return;
    
    // Calculate histogram from temperature data
    const temps = statsData.history.temperature || [];
    const bins = [0, 0, 0, 0, 0, 0];
    
    temps.forEach(temp => {
        if (temp < 15) bins[0]++;
        else if (temp < 20) bins[1]++;
        else if (temp < 25) bins[2]++;
        else if (temp < 30) bins[3]++;
        else if (temp < 35) bins[4]++;
        else bins[5]++;
    });
    
    statsCharts.temperatureHistogram.data.datasets[0].data = bins;
    statsCharts.temperatureHistogram.update('none');
}

// ============================================
// Daily Summary Chart
// ============================================

function createDailySummaryChart() {
    const ctx = document.getElementById('dailySummaryChart');
    if (!ctx) return;
    
    statsCharts.dailySummary = new Chart(ctx, {
        type: 'bar',
        data: {
            labels: [],
            datasets: [
                {
                    label: 'Charged (kWh)',
                    data: [],
                    backgroundColor: 'rgba(25, 135, 84, 0.8)',
                    stack: 'Stack 0'
                },
                {
                    label: 'Discharged (kWh)',
                    data: [],
                    backgroundColor: 'rgba(13, 110, 253, 0.8)',
                    stack: 'Stack 1'
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: {
                    display: true,
                    position: 'top'
                }
            },
            scales: {
                x: {
                    title: {
                        display: true,
                        text: 'Date'
                    }
                },
                y: {
                    beginAtZero: true,
                    title: {
                        display: true,
                        text: 'Energy (kWh)'
                    }
                }
            }
        }
    });
}

function updateDailySummaryChart() {
    if (!statsCharts.dailySummary) return;
    
    const days = statsData.daily_summary?.dates || [];
    const charged = statsData.daily_summary?.charged || [];
    const discharged = statsData.daily_summary?.discharged || [];
    
    statsCharts.dailySummary.data.labels = days;
    statsCharts.dailySummary.data.datasets[0].data = charged;
    statsCharts.dailySummary.data.datasets[1].data = discharged;
    statsCharts.dailySummary.update('none');
}

// ============================================
// Update All Charts
// ============================================

function updateCharts() {
    updateSocVoltageChart();
    updateEnergyPieChart();
    updateCurrentPowerChart();
    updateTemperatureHistogram();
    updateDailySummaryChart();
}

// ============================================
// Statistics Tables
// ============================================

function updateTables() {
    updatePeakValuesTable();
    updateEnergyStatsTable();
    updateCellStatsTable();
}

function updatePeakValuesTable() {
    const tbody = document.getElementById('peakValuesTable');
    if (!tbody) return;
    
    const peaks = statsData.peaks || {};
    
    tbody.innerHTML = `
        <tr>
            <td><i class="fas fa-arrow-up text-success"></i> Max SOC</td>
            <td class="text-end"><strong>${(peaks.max_soc || 0).toFixed(1)}%</strong></td>
        </tr>
        <tr>
            <td><i class="fas fa-arrow-down text-danger"></i> Min SOC</td>
            <td class="text-end"><strong>${(peaks.min_soc || 0).toFixed(1)}%</strong></td>
        </tr>
        <tr>
            <td><i class="fas fa-bolt text-warning"></i> Max Voltage</td>
            <td class="text-end"><strong>${(peaks.max_voltage || 0).toFixed(1)}V</strong></td>
        </tr>
        <tr>
            <td><i class="fas fa-bolt text-info"></i> Min Voltage</td>
            <td class="text-end"><strong>${(peaks.min_voltage || 0).toFixed(1)}V</strong></td>
        </tr>
        <tr>
            <td><i class="fas fa-fire text-danger"></i> Max Temp</td>
            <td class="text-end"><strong>${(peaks.max_temp || 0).toFixed(1)}°C</strong></td>
        </tr>
        <tr>
            <td><i class="fas fa-snowflake text-primary"></i> Min Temp</td>
            <td class="text-end"><strong>${(peaks.min_temp || 0).toFixed(1)}°C</strong></td>
        </tr>
    `;
}

function updateEnergyStatsTable() {
    const tbody = document.getElementById('energyStatsTable');
    if (!tbody) return;
    
    const energy = statsData.energy_stats || {};
    
    tbody.innerHTML = `
        <tr>
            <td><i class="fas fa-arrow-up text-success"></i> Total Charged</td>
            <td class="text-end"><strong>${(energy.total_charged || 0).toFixed(0)} kWh</strong></td>
        </tr>
        <tr>
            <td><i class="fas fa-arrow-down text-primary"></i> Total Discharged</td>
            <td class="text-end"><strong>${(energy.total_discharged || 0).toFixed(0)} kWh</strong></td>
        </tr>
        <tr>
            <td><i class="fas fa-percent text-info"></i> Efficiency</td>
            <td class="text-end"><strong>${(energy.efficiency || 0).toFixed(1)}%</strong></td>
        </tr>
        <tr>
            <td><i class="fas fa-clock text-warning"></i> Charge Time</td>
            <td class="text-end"><strong>${(energy.charge_hours || 0).toFixed(0)} hrs</strong></td>
        </tr>
        <tr>
            <td><i class="fas fa-clock text-danger"></i> Discharge Time</td>
            <td class="text-end"><strong>${(energy.discharge_hours || 0).toFixed(0)} hrs</strong></td>
        </tr>
        <tr>
            <td><i class="fas fa-plug text-secondary"></i> Idle Time</td>
            <td class="text-end"><strong>${(energy.idle_hours || 0).toFixed(0)} hrs</strong></td>
        </tr>
    `;
}

function updateCellStatsTable() {
    const tbody = document.getElementById('cellStatsTable');
    if (!tbody) return;
    
    const cells = statsData.cell_stats || {};
    
    tbody.innerHTML = `
        <tr>
            <td><i class="fas fa-balance-scale text-success"></i> Avg Balance</td>
            <td class="text-end"><strong>${(cells.avg_balance || 0).toFixed(0)} mV</strong></td>
        </tr>
        <tr>
            <td><i class="fas fa-exclamation-triangle text-warning"></i> Max Imbalance</td>
            <td class="text-end"><strong>${(cells.max_imbalance || 0).toFixed(0)} mV</strong></td>
        </tr>
        <tr>
            <td><i class="fas fa-arrow-up text-primary"></i> Highest Cell</td>
            <td class="text-end"><strong>Cell ${cells.highest_cell || '-'}</strong></td>
        </tr>
        <tr>
            <td><i class="fas fa-arrow-down text-info"></i> Lowest Cell</td>
            <td class="text-end"><strong>Cell ${cells.lowest_cell || '-'}</strong></td>
        </tr>
        <tr>
            <td><i class="fas fa-fire text-danger"></i> Hottest Cell</td>
            <td class="text-end"><strong>Cell ${cells.hottest_cell || '-'}</strong></td>
        </tr>
        <tr>
            <td><i class="fas fa-snowflake text-primary"></i> Coldest Cell</td>
            <td class="text-end"><strong>Cell ${cells.coldest_cell || '-'}</strong></td>
        </tr>
    `;
}

// ============================================
// Events Log
// ============================================

function updateEventsLog() {
    const tbody = document.getElementById('eventsLogTable');
    if (!tbody) return;
    
    const events = statsData.events || [];
    
    if (events.length === 0) {
        tbody.innerHTML = `
            <tr>
                <td colspan="4" class="text-center text-muted py-3">
                    No events in selected period
                </td>
            </tr>
        `;
        return;
    }
    
    tbody.innerHTML = events.map(event => {
        const typeClass = event.type === 'error' ? 'danger' : 
                         event.type === 'warning' ? 'warning' : 
                         event.type === 'success' ? 'success' : 'info';
        
        return `
            <tr data-event-type="${event.type}">
                <td><small>${event.timestamp}</small></td>
                <td><span class="badge bg-${typeClass}">${event.type.toUpperCase()}</span></td>
                <td>${event.message}</td>
                <td class="text-end">${event.value || '-'}</td>
            </tr>
        `;
    }).join('');
}

function filterEvents(type) {
    const rows = document.querySelectorAll('#eventsLogTable tr[data-event-type]');
    
    rows.forEach(row => {
        if (type === 'all' || row.dataset.eventType === type) {
            row.style.display = '';
        } else {
            row.style.display = 'none';
        }
    });
}

// ============================================
// Export Report
// ============================================

function exportStatisticsReport() {
    const format = confirm('Export as CSV? (Cancel for JSON)') ? 'csv' : 'json';
    
    if (format === 'csv') {
        exportStatisticsCSV();
    } else {
        exportStatisticsJSON();
    }
}

function exportStatisticsCSV() {
    let csv = 'TinyBMS Statistics Report\n';
    csv += `Period: ${statsData.period}\n`;
    csv += `Generated: ${new Date().toISOString()}\n\n`;
    
    // KPIs
    csv += 'Key Performance Indicators\n';
    csv += 'Metric,Value\n';
    csv += `Average SOC,${(statsData.kpis.avg_soc || 0).toFixed(1)}%\n`;
    csv += `Energy Charged,${(statsData.kpis.energy_charged || 0).toFixed(1)} kWh\n`;
    csv += `Average Temperature,${(statsData.kpis.avg_temp || 0).toFixed(1)}°C\n`;
    csv += `Total Cycles,${statsData.kpis.total_cycles || 0}\n\n`;
    
    // History
    csv += 'Historical Data\n';
    csv += 'Timestamp,SOC (%),Voltage (V),Current (A),Power (W),Temperature (°C)\n';
    const len = statsData.history.timestamps?.length || 0;
    for (let i = 0; i < len; i++) {
        csv += `${statsData.history.timestamps[i]},`;
        csv += `${statsData.history.soc[i]},`;
        csv += `${statsData.history.voltage[i]},`;
        csv += `${statsData.history.current[i]},`;
        csv += `${statsData.history.power[i]},`;
        csv += `${statsData.history.temperature[i]}\n`;
    }
    
    const blob = new Blob([csv], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `statistics_${statsData.period}_${new Date().toISOString().split('T')[0]}.csv`;
    a.click();
    URL.revokeObjectURL(url);
    
    showToast('Statistics exported as CSV', 'success');
}

function exportStatisticsJSON() {
    const report = {
        version: '3.0',
        period: statsData.period,
        generated: new Date().toISOString(),
        data: statsData
    };
    
    const json = JSON.stringify(report, null, 2);
    const blob = new Blob([json], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `statistics_${statsData.period}_${new Date().toISOString().split('T')[0]}.json`;
    a.click();
    URL.revokeObjectURL(url);
    
    showToast('Statistics exported as JSON', 'success');
}

// ============================================
// Generate Demo Data
// ============================================

function generateDemoStatistics() {
    console.log('[Statistics] Generating demo data...');
    
    // KPIs
    statsData.kpis = {
        avg_soc: 75.3,
        soc_trend: 2.5,
        energy_charged: 125.4,
        energy_discharged: 116.2,
        avg_temp: 23.5,
        temp_trend: -1.2,
        total_cycles: 145,
        cycles_delta: 3
    };
    
    // Generate history (last 24 points for demo)
    const points = 24;
    statsData.history = {
        timestamps: [],
        soc: [],
        voltage: [],
        current: [],
        power: [],
        temperature: []
    };
    
    for (let i = 0; i < points; i++) {
        const time = new Date(Date.now() - (points - i) * 3600000);
        statsData.history.timestamps.push(time.toLocaleTimeString());
        
        // Generate sinusoidal data with noise
        const phase = (i / points) * Math.PI * 2;
        statsData.history.soc.push(60 + 30 * Math.sin(phase) + Math.random() * 5);
        statsData.history.voltage.push(51 + 4 * Math.sin(phase) + Math.random() * 0.5);
        statsData.history.current.push(20 * Math.sin(phase + Math.PI/2) + Math.random() * 2);
        statsData.history.power.push((51 + 4 * Math.sin(phase)) * (20 * Math.sin(phase + Math.PI/2)));
        statsData.history.temperature.push(22 + 3 * Math.sin(phase) + Math.random() * 1);
    }
    
    // Peak values
    statsData.peaks = {
        max_soc: 98.5,
        min_soc: 15.2,
        max_voltage: 58.4,
        min_voltage: 48.2,
        max_temp: 32.1,
        min_temp: 18.5
    };
    
    // Energy stats
    statsData.energy_stats = {
        total_charged: 1245,
        total_discharged: 1156,
        efficiency: 92.8,
        charge_hours: 156,
        discharge_hours: 178,
        idle_hours: 242
    };
    
    // Cell stats
    statsData.cell_stats = {
        avg_balance: 25,
        max_imbalance: 145,
        highest_cell: 8,
        lowest_cell: 3,
        hottest_cell: 12,
        coldest_cell: 1
    };
    
    // Daily summary
    statsData.daily_summary = {
        dates: ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'],
        charged: [45, 52, 38, 61, 48, 55, 42],
        discharged: [42, 48, 35, 58, 45, 52, 39]
    };
    
    // Events
    statsData.events = [
        { timestamp: '2025-10-24 10:30:15', type: 'info', message: 'System started', value: '' },
        { timestamp: '2025-10-24 12:15:42', type: 'success', message: 'Charge complete', value: '98.5%' },
        { timestamp: '2025-10-24 14:20:18', type: 'warning', message: 'High temperature detected', value: '32.1°C' },
        { timestamp: '2025-10-24 16:45:33', type: 'info', message: 'Discharge started', value: '-25A' },
        { timestamp: '2025-10-24 18:30:27', type: 'warning', message: 'Cell imbalance detected', value: '145mV' },
        { timestamp: '2025-10-24 20:10:05', type: 'success', message: 'Balancing complete', value: '25mV' },
        { timestamp: '2025-10-24 22:00:00', type: 'info', message: 'Daily cycle completed', value: '#145' }
    ];
    
    updateAllStatistics();
}

// ============================================
// Initialize
// ============================================

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initStatistics);
} else {
    initStatistics();
}
