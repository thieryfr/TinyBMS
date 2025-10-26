/**
 * WebSocket Handler
 * Manages WebSocket connection to ESP32
 */

class WebSocketHandler {
    constructor() {
        this.ws = null;
        this.reconnectInterval = 3000; // 3 seconds
        this.reconnectTimer = null;
        this.isConnecting = false;
        this.callbacks = {
            onOpen: [],
            onClose: [],
            onError: [],
            onMessage: []
        };
        
        this.connect();
    }
    
    /**
     * Connect to WebSocket server
     */
    connect() {
        if (this.isConnecting || (this.ws && this.ws.readyState === WebSocket.OPEN)) {
            return;
        }
        
        this.isConnecting = true;
        this.updateStatus('connecting');
        
        // Determine WebSocket URL
        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        const host = window.location.hostname || 'localhost';
        const port = window.location.port || '80';
        const wsUrl = `${protocol}//${host}:${port}/ws`;
        
        console.log('[WS] Connecting to:', wsUrl);
        
        try {
            this.ws = new WebSocket(wsUrl);
            
            this.ws.onopen = (event) => {
                console.log('[WS] Connected');
                this.isConnecting = false;
                this.updateStatus('online');
                this.clearReconnectTimer();
                this.triggerCallbacks('onOpen', event);
            };
            
            this.ws.onclose = (event) => {
                console.log('[WS] Disconnected');
                this.isConnecting = false;
                this.updateStatus('offline');
                this.triggerCallbacks('onClose', event);
                this.scheduleReconnect();
            };
            
            this.ws.onerror = (error) => {
                console.error('[WS] Error:', error);
                this.updateStatus('error');
                this.triggerCallbacks('onError', error);
            };
            
            this.ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    this.triggerCallbacks('onMessage', data);
                } catch (error) {
                    console.error('[WS] Failed to parse message:', error);
                }
            };
            
        } catch (error) {
            console.error('[WS] Connection failed:', error);
            this.isConnecting = false;
            this.updateStatus('error');
            this.scheduleReconnect();
        }
    }
    
    /**
     * Disconnect WebSocket
     */
    disconnect() {
        this.clearReconnectTimer();
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
    }
    
    /**
     * Send data through WebSocket
     */
    send(data) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify(data));
            return true;
        }
        console.warn('[WS] Cannot send - not connected');
        return false;
    }
    
    /**
     * Schedule reconnection
     */
    scheduleReconnect() {
        this.clearReconnectTimer();
        console.log(`[WS] Reconnecting in ${this.reconnectInterval/1000}s...`);
        this.reconnectTimer = setTimeout(() => {
            this.connect();
        }, this.reconnectInterval);
    }
    
    /**
     * Clear reconnect timer
     */
    clearReconnectTimer() {
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }
    }
    
    /**
     * Update connection status in UI
     */
    updateStatus(status) {
        const statusLed = document.getElementById('statusLed');
        const statusText = document.getElementById('statusText');
        
        if (statusLed) {
            statusLed.className = 'status-led';
            statusLed.classList.add(`status-${status}`);
        }
        
        if (statusText) {
            const statusTexts = {
                'connecting': 'Connecting...',
                'online': 'Connected',
                'offline': 'Disconnected',
                'error': 'Error'
            };
            statusText.textContent = statusTexts[status] || 'Unknown';
        }
    }
    
    /**
     * Register callback
     */
    on(event, callback) {
        if (this.callbacks[event]) {
            this.callbacks[event].push(callback);
        }
    }
    
    /**
     * Trigger callbacks
     */
    triggerCallbacks(event, data) {
        if (this.callbacks[event]) {
            this.callbacks[event].forEach(callback => {
                try {
                    callback(data);
                } catch (error) {
                    console.error(`[WS] Callback error (${event}):`, error);
                }
            });
        }
    }
    
    /**
     * Get connection state
     */
    isConnected() {
        return this.ws && this.ws.readyState === WebSocket.OPEN;
    }
}

// Create global WebSocket instance
const wsHandler = new WebSocketHandler();
