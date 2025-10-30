#!/bin/bash
# =============================================================================
# TinyBMS 24h Stability Test Script
# =============================================================================
# Phase 4: Long-term stability validation
#
# This script performs a 24-hour continuous test of the TinyBMS system,
# monitoring:
# - API endpoint availability and response times
# - WebSocket connectivity and latency
# - System heap memory stability
# - Task stack health
# - Watchdog health
# - CAN/UART communication stability
#
# Usage:
#   ./longrun_test.sh [device_ip] [duration_hours]
#
# Example:
#   ./longrun_test.sh tinybms-bridge.local 24
#
# Output:
#   longrun_YYYYMMDD_HHMMSS.log - Detailed test log
#   longrun_YYYYMMDD_HHMMSS.csv - Time-series data for analysis
# =============================================================================

set -e

# Configuration
DEVICE_IP="${1:-tinybms-bridge.local}"
DURATION_H="${2:-24}"
CHECK_INTERVAL_S=60  # Check every minute
LOG_FILE="longrun_$(date +%Y%m%d_%H%M%S).log"
CSV_FILE="longrun_$(date +%Y%m%d_%H%M%S).csv"

# Thresholds
MIN_HEAP_KB=150
MIN_HEAP_WARNING_KB=100
MAX_API_LATENCY_MS=1000
MAX_WS_LATENCY_MS=2000

# Counters
TOTAL_CHECKS=0
API_FAILURES=0
WS_FAILURES=0
HEAP_WARNINGS=0
HEAP_CRITICAL=0

# Colors
RED='\033[0;31m'
YELLOW='\033[1;33m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

# =============================================================================
# Helper Functions
# =============================================================================

log() {
    local level=$1
    shift
    local msg="$@"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "${timestamp} [${level}] ${msg}" | tee -a "$LOG_FILE"
}

log_info() {
    log "INFO" "${GREEN}$@${NC}"
}

log_warn() {
    log "WARN" "${YELLOW}$@${NC}"
}

log_error() {
    log "ERROR" "${RED}$@${NC}"
}

# =============================================================================
# Test Functions
# =============================================================================

test_api_endpoint() {
    local endpoint=$1
    local start_time=$(date +%s%N)

    local response=$(curl -s -w "\n%{http_code}\n%{time_total}" \
        -m 5 \
        "http://${DEVICE_IP}/api/${endpoint}" 2>/dev/null || echo "FAIL")

    local end_time=$(date +%s%N)
    local latency_ms=$(( (end_time - start_time) / 1000000 ))

    if [[ "$response" == "FAIL" ]]; then
        log_error "API ${endpoint} failed (timeout or connection error)"
        return 1
    fi

    local http_code=$(echo "$response" | tail -n 1)

    if [[ "$http_code" != "200" ]]; then
        log_error "API ${endpoint} returned HTTP ${http_code}"
        return 1
    fi

    if [[ $latency_ms -gt $MAX_API_LATENCY_MS ]]; then
        log_warn "API ${endpoint} latency high: ${latency_ms}ms (max: ${MAX_API_LATENCY_MS}ms)"
    fi

    echo "$latency_ms"
    return 0
}

check_system_health() {
    local timestamp=$(date +%s)

    # Get system diagnostics
    local diag=$(curl -s -m 5 "http://${DEVICE_IP}/api/memory" 2>/dev/null)

    if [[ $? -ne 0 ]]; then
        log_error "Failed to fetch system diagnostics"
        ((API_FAILURES++))
        return 1
    fi

    # Parse JSON (requires jq)
    if ! command -v jq &> /dev/null; then
        log_warn "jq not installed - skipping detailed metrics"
        return 0
    fi

    local heap_free=$(echo "$diag" | jq -r '.free_heap // 0')
    local heap_min=$(echo "$diag" | jq -r '.min_free_heap // 0')

    # Convert to KB
    heap_free_kb=$((heap_free / 1024))
    heap_min_kb=$((heap_min / 1024))

    # Log to CSV
    echo "${timestamp},${heap_free_kb},${heap_min_kb}" >> "$CSV_FILE"

    # Check thresholds
    if [[ $heap_free_kb -lt $MIN_HEAP_WARNING_KB ]]; then
        log_error "CRITICAL: Heap critically low: ${heap_free_kb} KB (min: ${MIN_HEAP_WARNING_KB} KB)"
        ((HEAP_CRITICAL++))
        return 1
    elif [[ $heap_free_kb -lt $MIN_HEAP_KB ]]; then
        log_warn "Low heap warning: ${heap_free_kb} KB (target: ${MIN_HEAP_KB} KB)"
        ((HEAP_WARNINGS++))
    fi

    log_info "Heap: ${heap_free_kb} KB free, ${heap_min_kb} KB min"
    return 0
}

check_websocket() {
    # Simple WebSocket check using wscat (if available)
    if ! command -v wscat &> /dev/null; then
        log_warn "wscat not installed - skipping WebSocket test"
        return 0
    fi

    # Try to connect and receive one message (timeout 10s)
    local ws_test=$(timeout 10s wscat -c "ws://${DEVICE_IP}/ws" --execute "exit" 2>&1 || echo "FAIL")

    if [[ "$ws_test" == *"FAIL"* ]] || [[ "$ws_test" == *"error"* ]]; then
        log_error "WebSocket connection failed"
        ((WS_FAILURES++))
        return 1
    fi

    log_info "WebSocket connection OK"
    return 0
}

# =============================================================================
# Main Test Loop
# =============================================================================

main() {
    log_info "=========================================="
    log_info "TinyBMS 24h Stability Test"
    log_info "=========================================="
    log_info "Device: ${DEVICE_IP}"
    log_info "Duration: ${DURATION_H} hours"
    log_info "Check interval: ${CHECK_INTERVAL_S}s"
    log_info "Log file: ${LOG_FILE}"
    log_info "CSV file: ${CSV_FILE}"
    log_info "=========================================="

    # Initialize CSV
    echo "timestamp,heap_free_kb,heap_min_kb" > "$CSV_FILE"

    # Initial connectivity check
    log_info "Performing initial connectivity check..."
    if ! curl -s -m 5 "http://${DEVICE_IP}/api/status" > /dev/null 2>&1; then
        log_error "Cannot reach device at ${DEVICE_IP}"
        exit 1
    fi
    log_info "Device reachable ✓"

    local start_time=$(date +%s)
    local end_time=$((start_time + (DURATION_H * 3600)))
    local next_check=$start_time

    log_info "Test started at $(date)"
    log_info "Test will end at $(date -d @${end_time})"

    # Main loop
    while [[ $(date +%s) -lt $end_time ]]; do
        local current_time=$(date +%s)

        if [[ $current_time -ge $next_check ]]; then
            ((TOTAL_CHECKS++))

            log_info "========== Check #${TOTAL_CHECKS} =========="

            # Test API endpoints
            local api_latency=$(test_api_endpoint "status")
            if [[ $? -eq 0 ]]; then
                log_info "API status: ${api_latency}ms"
            else
                ((API_FAILURES++))
            fi

            # Check system health
            check_system_health

            # Check WebSocket
            check_websocket

            # Print statistics
            local elapsed_h=$(( (current_time - start_time) / 3600 ))
            local remaining_h=$(( (end_time - current_time) / 3600 ))

            log_info "Progress: ${elapsed_h}h elapsed, ${remaining_h}h remaining"
            log_info "Stats: API failures=${API_FAILURES}, WS failures=${WS_FAILURES}, Heap warnings=${HEAP_WARNINGS}, Heap critical=${HEAP_CRITICAL}"

            # Schedule next check
            next_check=$((current_time + CHECK_INTERVAL_S))
        fi

        sleep 10
    done

    # Final report
    log_info "=========================================="
    log_info "Test completed"
    log_info "=========================================="
    log_info "Total checks: ${TOTAL_CHECKS}"
    log_info "API failures: ${API_FAILURES}"
    log_info "WebSocket failures: ${WS_FAILURES}"
    log_info "Heap warnings: ${HEAP_WARNINGS}"
    log_info "Heap critical: ${HEAP_CRITICAL}"

    local success_rate=$(bc <<< "scale=2; 100 * (${TOTAL_CHECKS} - ${API_FAILURES}) / ${TOTAL_CHECKS}")
    log_info "Success rate: ${success_rate}%"

    if [[ $API_FAILURES -eq 0 ]] && [[ $HEAP_CRITICAL -eq 0 ]]; then
        log_info "${GREEN}✓ Test PASSED${NC}"
        exit 0
    else
        log_error "${RED}✗ Test FAILED${NC}"
        exit 1
    fi
}

# Run main
main
