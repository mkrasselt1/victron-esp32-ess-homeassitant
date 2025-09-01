let ws;
let lastUpdateTime = 0;

function connectWS() {
    ws = new WebSocket('ws://' + location.hostname + '/ws');
    ws.onmessage = function(event) {
        const data = JSON.parse(event.data);
        updateAllValues(data);
        lastUpdateTime = Date.now();
    };
    ws.onclose = function() {
        setTimeout(connectWS, 2000);
    };
    ws.onerror = function() {
        console.log('WebSocket error');
    };
}

function updateAllValues(data) {
    // Battery Status
    const soc = data.battery_soc || 0;
    document.getElementById('battery_soc').textContent = soc + '%';
    document.getElementById('battery_voltage').textContent = (data.battery_voltage || '?') + ' V';
    document.getElementById('battery_current').textContent = (data.battery_current || '?') + ' A';
    document.getElementById('battery_temperature').textContent = (data.battery_temperature || '?') + ' °C';
    document.getElementById('battery_soh').textContent = (data.battery_soh || '?') + '%';
    document.getElementById('battery_charge_voltage').textContent = (data.battery_chargeVoltage || '?') + ' V';
    document.getElementById('battery_charge_current_limit').textContent = (data.battery_chargeCurrentLimit || '?') + ' A';
    document.getElementById('battery_discharge_current_limit').textContent = (data.battery_dischargeCurrentLimit || '?') + ' A';
    document.getElementById('battery_manufacturer').textContent = data.battery_manufacturer || '---';
    
    // Update battery SOC progress bar
    const progressBar = document.getElementById('battery-progress');
    progressBar.style.width = Math.max(0, Math.min(100, soc)) + '%';
    
    // Change color based on SOC
    if (soc > 70) {
        progressBar.style.background = 'linear-gradient(90deg, var(--victron-green), #20C997)';
    } else if (soc > 30) {
        progressBar.style.background = 'linear-gradient(90deg, var(--victron-orange), #FFB347)';
    } else {
        progressBar.style.background = 'linear-gradient(90deg, var(--victron-red), #FF6B6B)';
    }
    
    // MultiPlus Data
    document.getElementById('multiplus_dc_voltage').textContent = (data.multiplusDcVoltage || '?') + ' V';
    document.getElementById('multiplus_dc_current').textContent = (data.multiplusDcCurrent || '?') + ' A';
    document.getElementById('multiplus_ac_voltage').textContent = (data.multiplusUMainsRMS || '?') + ' V';
    document.getElementById('multiplus_ac_frequency').textContent = (data.multiplusAcFrequency || '?') + ' Hz';
    document.getElementById('multiplus_inverter_power').textContent = (data.multiplusPinverterFiltered || '?') + ' W';
    document.getElementById('multiplus_acin_power').textContent = (data.multiplusPmainsFiltered || '?') + ' W';
    document.getElementById('multiplus_power_factor').textContent = data.multiplusPowerFactor || '---';
    document.getElementById('multiplus_temperature').textContent = (data.multiplusTemp || '?') + ' °C';
    document.getElementById('multiplus_status').textContent = data.multiplusStatus80 || '---';
    document.getElementById('multiplus_current_limit').textContent = (data.masterMultiLED_ActualInputCurrentLimit || '?') + ' A';
    
    // ESS Power Flow
    const essPower = data.multiplusESSpower || 0;
    document.getElementById('ess_power').textContent = Math.abs(essPower) + ' W';
    const powerDirection = document.getElementById('power_direction');
    if (essPower > 100) {
        powerDirection.textContent = 'Charging';
        document.getElementById('ess_power').style.color = 'var(--victron-green)';
    } else if (essPower < -100) {
        powerDirection.textContent = 'Discharging';
        document.getElementById('ess_power').style.color = 'var(--victron-orange)';
    } else {
        powerDirection.textContent = 'Standby';
        document.getElementById('ess_power').style.color = 'var(--victron-blue)';
    }
    
    document.getElementById('switch_mode').textContent = data.switchMode || '---';
    document.getElementById('strategy').textContent = data.essPowerStrategy || '---';
    document.getElementById('min_strategy_time').textContent = formatTime(data.secondsInMinStrategy || 0);
    document.getElementById('max_strategy_time').textContent = formatTime(data.secondsInMaxStrategy || 0);
    document.getElementById('avg_bms_power').textContent = Math.round(data.bmsPowerAverage || 0) + ' W';
    
    // System Status
    const currentTime = new Date();
    document.getElementById('current_time').textContent = currentTime.toLocaleTimeString();
    
    // VE.Bus Status
    const vebusIndicator = document.getElementById('vebus-indicator');
    const vebusOnline = data.veBus_isOnline;
    document.getElementById('vebus_online').textContent = vebusOnline ? 'Online' : 'Offline';
    vebusIndicator.className = 'status-indicator ' + (vebusOnline ? 'status-online' : 'status-offline');
    document.getElementById('vebus_quality').textContent = Math.round((data.veBus_communicationQuality || 0) * 100) + '%';
    document.getElementById('vebus_frames_sent').textContent = data.veBus_framesSent || '0';
    document.getElementById('vebus_frames_received').textContent = data.veBus_framesReceived || '0';
    document.getElementById('vebus_errors').textContent = ((data.veBus_checksumErrors || 0) + (data.veBus_timeoutErrors || 0));
    
    // Status LED
    const ledModes = ['Boot', 'WiFi Connecting', 'WiFi Connected', 'Normal Operation', 'Error'];
    document.getElementById('statusLED_mode').textContent = ledModes[data.statusLED_mode] || 'Unknown';
    
    // Feed-in Power Control
    const feedInEnabled = data.feedInControl_enabled || false;
    document.getElementById('feedin_status').textContent = feedInEnabled ? 'Aktiv' : 'Inaktiv';
    document.getElementById('feedin_status').style.color = feedInEnabled ? 'var(--victron-green)' : 'var(--victron-red)';
    document.getElementById('feedin_current').textContent = Math.round(data.feedInControl_current || 0) + ' W';
    document.getElementById('feedin_target').textContent = Math.round(data.feedInControl_target || 0) + ' W';
    document.getElementById('feedin_max').textContent = Math.round(data.feedInControl_max || 0) + ' W';
    
    // Update form inputs if not currently being edited
    if (!document.getElementById('feedin_enabled').matches(':focus')) {
        document.getElementById('feedin_enabled').checked = feedInEnabled;
    }
    if (!document.getElementById('feedin_target_input').matches(':focus')) {
        document.getElementById('feedin_target_input').value = Math.round(data.feedInControl_target || 0);
    }
    if (!document.getElementById('feedin_max_input').matches(':focus')) {
        document.getElementById('feedin_max_input').value = Math.round(data.feedInControl_max || 5000);
    }
    
    // MQTT Status (if available in data)
    if (data.mqtt) {
        const mqttConnected = data.mqtt.connected || false;
        document.getElementById('mqtt_status').textContent = mqttConnected ? 'Verbunden' : 'Getrennt';
        document.getElementById('mqtt_status').style.color = mqttConnected ? 'var(--victron-green)' : 'var(--victron-red)';
        document.getElementById('mqtt_server').textContent = data.mqtt.server || '---';
        document.getElementById('mqtt_port').textContent = data.mqtt.port || '---';
        document.getElementById('mqtt_last_message').textContent = new Date().toLocaleTimeString();
    }
    
    // Protection & Warnings
    document.getElementById('battery_protection1').textContent = '0x' + (data.battery_protectionFlags1 ? data.battery_protectionFlags1.toString(16).toUpperCase() : '?');
    document.getElementById('battery_protection2').textContent = '0x' + (data.battery_protectionFlags2 ? data.battery_protectionFlags2.toString(16).toUpperCase() : '?');
    document.getElementById('battery_warning1').textContent = '0x' + (data.battery_warningFlags1 ? data.battery_warningFlags1.toString(16).toUpperCase() : '?');
    document.getElementById('battery_warning2').textContent = '0x' + (data.battery_warningFlags2 ? data.battery_warningFlags2.toString(16).toUpperCase() : '?');
    document.getElementById('battery_request').textContent = '0x' + (data.battery_requestFlags ? data.battery_requestFlags.toString(16).toUpperCase() : '?');
    
    // Control & Diagnostics
    document.getElementById('minimum_feedin').textContent = (data.minimumFeedIn || '?') + ' W';
    document.getElementById('avg_ctrl_deviation').textContent = (data.averageControlDeviationFeedIn || '?') + ' W';
    document.getElementById('avg_charging_power').textContent = (data.averageChargingPower || '?') + ' W';
    document.getElementById('power_trend_cons').textContent = (data.powerTrendConsumption || '?') + ' Wh';
    document.getElementById('power_trend_feed').textContent = (data.powerTrendFeedIn || '?') + ' Wh';
}

function formatTime(seconds) {
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    return h.toString().padStart(2, '0') + ':' + m.toString().padStart(2, '0') + ':' + s.toString().padStart(2, '0');
}

function formatTimeFromSeconds(totalSeconds) {
    const timeOffset = 9*60*60 + 57*60 + 7; // ELECTRIC_METER_TIME_OFFSET
    const adjustedSeconds = totalSeconds + timeOffset;
    const h = Math.floor((adjustedSeconds % (24*60*60)) / 3600);
    const m = Math.floor((adjustedSeconds % 3600) / 60);
    const s = adjustedSeconds % 60;
    return h.toString().padStart(2, '0') + ':' + m.toString().padStart(2, '0') + ':' + s.toString().padStart(2, '0');
}

function shortPress() {
    fetch('/shortpress').catch(e => console.log('Short press failed:', e));
}

function longPress() {
    fetch('/longpress').catch(e => console.log('Long press failed:', e));
}

function manualRefresh() {
    // Visual feedback
    const btn = document.querySelector('.refresh-btn');
    btn.style.transform = 'scale(0.9) rotate(180deg)';
    setTimeout(() => {
        btn.style.transform = 'scale(1) rotate(0deg)';
    }, 200);
    
    // Reconnect WebSocket if needed
    if (ws.readyState !== WebSocket.OPEN) {
        connectWS();
    }
}

// MQTT Configuration Modal Functions
function openMqttModal() {
    console.log('Opening MQTT modal...');
    const modal = document.getElementById('mqttModal');
    if (!modal) {
        console.error('MQTT modal not found!');
        return;
    }
    modal.style.display = 'block';
    loadMqttConfig();
    
    // Add event listener when modal opens
    const form = document.getElementById('mqtt_modal_form');
    if (form && !form.hasAttribute('data-listener-added')) {
        console.log('Adding form submit event listener...');
        form.addEventListener('submit', handleMqttFormSubmit);
        form.setAttribute('data-listener-added', 'true');
    } else if (!form) {
        console.error('MQTT modal form not found!');
    } else {
        console.log('Form event listener already added');
    }
}

function closeMqttModal() {
    document.getElementById('mqttModal').style.display = 'none';
}

// Close modal when clicking outside
window.onclick = function(event) {
    const modal = document.getElementById('mqttModal');
    if (event.target === modal) {
        closeMqttModal();
    }
}

// Load current MQTT configuration into modal
function loadMqttConfig() {
    console.log('Loading MQTT config for modal...');
    fetch('/api/mqtt')
    .then(response => {
        console.log('MQTT config response:', response.status);
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        return response.json();
    })
    .then(data => {
        console.log('MQTT config data:', data);
        document.getElementById('mqtt_server_input').value = data.server || '';
        document.getElementById('mqtt_port_input').value = data.port || 1883;
        document.getElementById('mqtt_username_input').value = data.username || '';
        // Don't populate password field for security reasons
        document.getElementById('mqtt_password_input').value = '';
        document.getElementById('mqtt_password_input').placeholder = data.password ? 'Password set (hidden)' : 'Enter password';
    })
    .catch(error => {
        console.error('Error loading MQTT config:', error);
    });
}

// Handle MQTT modal form submission
function handleMqttFormSubmit(e) {
    e.preventDefault();
    
    const mqttConfig = {
        server: document.getElementById('mqtt_server_input').value.trim(),
        port: parseInt(document.getElementById('mqtt_port_input').value),
        username: document.getElementById('mqtt_username_input').value.trim(),
        password: document.getElementById('mqtt_password_input').value.trim()
    };

    if (!mqttConfig.server) {
        alert('Bitte geben Sie einen MQTT-Server an!');
        return;
    }

    // Disable form during submission
    const submitBtn = e.target.querySelector('button[type="submit"]');
    const originalText = submitBtn.textContent;
    submitBtn.textContent = '💾 Speichere...';
    submitBtn.disabled = true;

    console.log('Sending MQTT config:', mqttConfig);

    fetch('/api/mqtt', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(mqttConfig)
    })
    .then(response => {
        console.log('MQTT config save response:', response.status);
        return response.json();
    })
    .then(data => {
        console.log('MQTT config save result:', data);
        if (data.success) {
            alert('MQTT-Konfiguration erfolgreich gespeichert!');
            closeMqttModal();
            loadMqttStatus(); // Refresh status display
        } else {
            alert('Fehler beim Speichern der MQTT-Konfiguration: ' + (data.error || 'Unbekannter Fehler'));
        }
    })
    .catch(error => {
        console.error('Error saving MQTT config:', error);
        alert('Fehler beim Speichern der MQTT-Konfiguration: ' + error.message);
    })
    .finally(() => {
        submitBtn.textContent = originalText;
        submitBtn.disabled = false;
    });
}

// Load current MQTT status for display
function loadMqttStatus() {
    console.log('Loading MQTT status...');
    fetch('/api/mqtt')
    .then(response => {
        console.log('MQTT status response:', response.status);
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        return response.json();
    })
    .then(data => {
        console.log('MQTT status data:', data);
        const mqttConnected = data.connected || false;
        document.getElementById('mqtt_status').textContent = mqttConnected ? 'Verbunden ✅' : 'Getrennt ❌';
        document.getElementById('mqtt_server_display').textContent = data.server || 'Nicht konfiguriert';
        document.getElementById('mqtt_port_display').textContent = data.port || '---';
        document.getElementById('mqtt_last_message').textContent = data.lastMessage || 'Keine';
    })
    .catch(error => {
        console.error('Error loading MQTT status:', error);
        document.getElementById('mqtt_status').textContent = 'Fehler beim Laden ❌';
        document.getElementById('mqtt_server_display').textContent = 'API-Fehler';
        document.getElementById('mqtt_port_display').textContent = '---';
        document.getElementById('mqtt_last_message').textContent = error.message;
    });
}

// Event listeners that need to be added after DOM loads
document.addEventListener('DOMContentLoaded', function() {
    // MQTT configuration form handler
    const mqttForm = document.getElementById('mqtt_form');
    if (mqttForm) {
        mqttForm.addEventListener('submit', function(e) {
            e.preventDefault();
            
            const server = document.getElementById('mqtt_server_input').value.trim();
            const port = parseInt(document.getElementById('mqtt_port_input').value) || 1883;
            const username = document.getElementById('mqtt_username_input').value.trim();
            const password = document.getElementById('mqtt_password_input').value;
            
            if (!server) {
                alert('Bitte geben Sie einen MQTT-Server ein.');
                return;
            }
            
            const formData = new FormData();
            formData.append('server', server);
            formData.append('port', port);
            if (username) formData.append('username', username);
            if (password) formData.append('password', password);
            
            fetch('/api/mqtt', {
                method: 'POST',
                body: formData
            })
            .then(response => response.json())
            .then(data => {
                console.log('MQTT configuration updated:', data);
                alert('MQTT-Konfiguration erfolgreich gespeichert. Verbindung wird hergestellt...');
                // Reload MQTT status
                loadMqttStatus();
            })
            .catch(error => {
                console.error('Error updating MQTT configuration:', error);
                alert('Fehler beim Aktualisieren der MQTT-Konfiguration: ' + error.message);
            });
        });
    }

    // Feed-in control form handler
    const feedinForm = document.getElementById('feedin_form');
    if (feedinForm) {
        feedinForm.addEventListener('submit', function(e) {
            e.preventDefault();
            
            const enabled = document.getElementById('feedin_enabled').checked;
            const target = parseFloat(document.getElementById('feedin_target_input').value) || 0;
            const max = parseFloat(document.getElementById('feedin_max_input').value) || 5000;
            
            const formData = new FormData();
            formData.append('enabled', enabled ? 'true' : 'false');
            formData.append('target', target);
            formData.append('max', max);
            
            fetch('/api/feedin', {
                method: 'POST',
                body: formData
            })
            .then(response => response.json())
            .then(data => {
                console.log('Feed-in control updated:', data);
                // Update will be reflected through WebSocket
            })
            .catch(error => {
                console.error('Error updating feed-in control:', error);
                alert('Fehler beim Aktualisieren der Einstellungen: ' + error.message);
            });
        });
    }
});

// Auto-connect on page load
window.onload = function() {
    connectWS();
    loadMqttStatus();
};
