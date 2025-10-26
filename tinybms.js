// ============================================================================
// TinyBMS-Victron Bridge - TinyBMS Specific Decoders
// ============================================================================

// TinyBMS Register Map
const TINYBMS_REGISTERS = {
    36: { name: 'Voltage', unit: 'V', type: 'FLOAT' },
    38: { name: 'Current', unit: 'A', type: 'FLOAT' },
    40: { name: 'Min Cell', unit: 'mV', type: 'UINT16' },
    41: { name: 'Max Cell', unit: 'mV', type: 'UINT16' },
    45: { name: 'SOH', unit: '%', type: 'UINT32', scale: 0.000001 },
    46: { name: 'SOC', unit: '%', type: 'UINT32', scale: 0.000001 },
    48: { name: 'Temperature', unit: '0.1°C', type: 'INT16' },
    50: { name: 'Online Status', unit: '', type: 'UINT16' },
    52: { name: 'Balancing Bits', unit: '', type: 'UINT16' },
    102: { name: 'Max Discharge', unit: '0.1A', type: 'UINT16' },
    103: { name: 'Max Charge', unit: '0.1A', type: 'UINT16' },
    300: { name: 'Fully Charged', unit: 'mV', type: 'UINT16' },
    315: { name: 'Overvoltage Cutoff', unit: 'mV', type: 'UINT16' }
};

// Online Status Codes
const ONLINE_STATUS = {
    0: 'Offline',
    1: 'Discharge',
    2: 'Charge',
    3: 'Idle',
    151: 'Idle (Normal)',
    152: 'Charge Active',
    153: 'Discharge Active'
};

// ============================================================================
// Register Decoder
// ============================================================================

function decodeTinyBMSRegister(address, rawValue) {
    const reg = TINYBMS_REGISTERS[address];
    if (!reg) return { error: 'Unknown register' };
    
    let value = rawValue;
    
    // Apply scaling if defined
    if (reg.scale) {
        value = rawValue * reg.scale;
    }
    
    // Convert temperature (0.1°C to °C)
    if (address === 48) {
        value = rawValue / 10;
    }
    
    return {
        address: address,
        name: reg.name,
        value: value,
        unit: reg.unit,
        type: reg.type,
        raw: rawValue
    };
}

// ============================================================================
// Online Status Decoder
// ============================================================================

function decodeOnlineStatus(statusCode) {
    return ONLINE_STATUS[statusCode] || `Unknown (${statusCode})`;
}

// ============================================================================
// Balancing Bits Decoder
// ============================================================================

function decodeBalancingBits(bits) {
    const bitsArray = [];
    for (let i = 0; i < 16; i++) {
        bitsArray.push((bits >> i) & 1);
    }
    return {
        raw: bits,
        binary: bits.toString(2).padStart(16, '0'),
        cells: bitsArray,
        active_count: bitsArray.filter(b => b === 1).length
    };
}

// ============================================================================
// Cell Voltage Analysis
// ============================================================================

function analyzeCellVoltages(minMv, maxMv) {
    const delta = maxMv - minMv;
    
    let status = 'good';
    let message = 'Cellules équilibrées';
    
    if (delta > 100) {
        status = 'critical';
        message = 'Déséquilibre critique (>100mV)';
    } else if (delta > 50) {
        status = 'warning';
        message = 'Déséquilibre détecté (>50mV)';
    } else if (delta > 30) {
        status = 'info';
        message = 'Léger déséquilibre (>30mV)';
    }
    
    return {
        min: minMv,
        max: maxMv,
        delta: delta,
        status: status,
        message: message
    };
}

// ============================================================================
// Current Direction & Power
// ============================================================================

function analyzeCurrentFlow(voltage, current) {
    const power = voltage * current;
    
    let direction = 'idle';
    let message = 'Inactif';
    
    if (current > 0.1) {
        direction = 'charging';
        message = `Charge: ${power.toFixed(1)}W`;
    } else if (current < -0.1) {
        direction = 'discharging';
        message = `Décharge: ${Math.abs(power).toFixed(1)}W`;
    }
    
    return {
        voltage: voltage,
        current: current,
        power: power,
        direction: direction,
        message: message
    };
}

// ============================================================================
// SOC Health Analysis
// ============================================================================

function analyzeSOCHealth(soc, soh) {
    const analysis = {
        soc: {
            value: soc,
            status: 'good',
            message: 'Niveau correct'
        },
        soh: {
            value: soh,
            status: 'good',
            message: 'État optimal'
        }
    };
    
    // SOC Analysis
    if (soc < 10) {
        analysis.soc.status = 'critical';
        analysis.soc.message = 'Batterie très faible';
    } else if (soc < 20) {
        analysis.soc.status = 'warning';
        analysis.soc.message = 'Batterie faible';
    } else if (soc > 95) {
        analysis.soc.status = 'info';
        analysis.soc.message = 'Batterie pleine';
    }
    
    // SOH Analysis
    if (soh < 70) {
        analysis.soh.status = 'critical';
        analysis.soh.message = 'État dégradé';
    } else if (soh < 85) {
        analysis.soh.status = 'warning';
        analysis.soh.message = 'État moyen';
    }
    
    return analysis;
}

// ============================================================================
// Temperature Analysis
// ============================================================================

function analyzeTemperature(tempC) {
    let status = 'good';
    let message = 'Température normale';
    
    if (tempC > 45) {
        status = 'critical';
        message = 'Surchauffe critique';
    } else if (tempC > 35) {
        status = 'warning';
        message = 'Température élevée';
    } else if (tempC < 0) {
        status = 'warning';
        message = 'Température basse';
    } else if (tempC < -10) {
        status = 'critical';
        message = 'Température très basse';
    }
    
    return {
        temperature: tempC,
        status: status,
        message: message
    };
}

// ============================================================================
// Export Functions for UI
// ============================================================================

window.TinyBMSDecoder = {
    decodeRegister: decodeTinyBMSRegister,
    decodeOnlineStatus: decodeOnlineStatus,
    decodeBalancingBits: decodeBalancingBits,
    analyzeCells: analyzeCellVoltages,
    analyzeCurrent: analyzeCurrentFlow,
    analyzeSOCHealth: analyzeSOCHealth,
    analyzeTemperature: analyzeTemperature
};
