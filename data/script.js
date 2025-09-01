let ws;
let lastUpdateTime = 0;

// Safe DOM element access helper
function getElement(id) {
    return document.getElementById(id);
}

// Safe element update helper
function updateElement(id, value, suffix = '') {
    const element = getElement(id);
    if (element) {
        element.textContent = value + suffix;
    }
}

// Safe element style update helper
function updateElementStyle(id, property, value) {
    const element = getElement(id);
    if (element) {
        element.style[property] = value;
    }
}

// Safe element class update helper
function updateElementClass(id, className) {
    const element = getElement(id);
    if (element) {
        element.className = className;
    }
}

function connectWS() {
    ws = new WebSocket('ws://' + location.hostname + '/ws');
    ws.onmessage = function(event) {
        try {
            const data = JSON.parse(event.data);
            updateData(data); // Neue spezifische Update-Funktion
            lastUpdateTime = Date.now();
        } catch (e) {
            console.error('Error parsing WebSocket data:', e);
        }
    };
    ws.onclose = function() {
        setTimeout(connectWS, 2000);
    };
    ws.onerror = function() {
        console.log('WebSocket error');
    };
}

function updateAllValues(data) {
    if (!data) return;

    // Battery Status - only update if data is available
    if (typeof data.battery_soc !== 'undefined') {
        const soc = data.battery_soc || 0;
        updateElement('battery_soc', soc, '%');
        updateElement('battery_voltage', (data.battery_voltage || '?'), ' V');
        updateElement('battery_current', (data.battery_current || '?'), ' A');
        updateElement('battery_temperature', (data.battery_temperature || '?'), ' °C');
        updateElement('battery_soh', (data.battery_soh || '?'), '%');
        updateElement('battery_charge_voltage', (data.battery_chargeVoltage || '?'), ' V');
        updateElement('battery_charge_current_limit', (data.battery_chargeCurrentLimit || '?'), ' A');
        updateElement('battery_discharge_current_limit', (data.battery_dischargeCurrentLimit || '?'), ' A');
        updateElement('battery_manufacturer', data.battery_manufacturer || '---');

        // Update battery SOC progress bar
        const progressBar = getElement('battery-progress');
        if (progressBar) {
            progressBar.style.width = Math.max(0, Math.min(100, soc)) + '%';

            // Change color based on SOC
            if (soc > 70) {
                progressBar.style.background = 'linear-gradient(90deg, var(--victron-green), #20C997)';
            } else if (soc > 30) {
                progressBar.style.background = 'linear-gradient(90deg, var(--victron-orange), #FFB347)';
            } else {
                progressBar.style.background = 'linear-gradient(90deg, var(--victron-red), #FF6B6B)';
            }
        }
    }

    // MultiPlus Data - only update if data is available
    if (typeof data.multiplusDcVoltage !== 'undefined' ||
        typeof data.multiplusDcCurrent !== 'undefined' ||
        typeof data.multiplusUMainsRMS !== 'undefined') {

        updateElement('multiplus_dc_voltage', (data.multiplusDcVoltage || '?'), ' V');
        updateElement('multiplus_dc_current', (data.multiplusDcCurrent || '?'), ' A');
        updateElement('multiplus_ac_voltage', (data.multiplusUMainsRMS || '?'), ' V');
        updateElement('multiplus_ac_frequency', (data.multiplusAcFrequency || '?'), ' Hz');
        updateElement('multiplus_inverter_power', (data.multiplusPinverterFiltered || '?'), ' W');
        updateElement('multiplus_acin_power', (data.multiplusPmainsFiltered || '?'), ' W');
        updateElement('multiplus_power_factor', data.multiplusPowerFactor || '---');
        updateElement('multiplus_temperature', (data.multiplusTemp || '?'), ' °C');
        updateElement('multiplus_status', data.multiplusStatus80 || '---');
        updateElement('multiplus_current_limit', (data.masterMultiLED_ActualInputCurrentLimit || '?'), ' A');
    }

    // ESS Power Flow - only update if data is available
    if (typeof data.multiplusESSpower !== 'undefined') {
        const essPower = data.multiplusESSpower || 0;
        updateElement('ess_power', Math.abs(essPower), ' W');

        const powerDirection = getElement('power_direction');
        const essPowerElement = getElement('ess_power');
        if (powerDirection && essPowerElement) {
            if (essPower > 100) {
                powerDirection.textContent = 'Charging';
                essPowerElement.style.color = 'var(--victron-green)';
            } else if (essPower < -100) {
                powerDirection.textContent = 'Discharging';
                essPowerElement.style.color = 'var(--victron-orange)';
            } else {
                powerDirection.textContent = 'Standby';
                essPowerElement.style.color = 'var(--victron-blue)';
            }
        }
    }

    // ESS Control data - only update if data is available
    if (typeof data.switchMode !== 'undefined' ||
        typeof data.essPowerStrategy !== 'undefined') {

        updateElement('switch_mode', data.switchMode || '---');
        updateElement('strategy', data.essPowerStrategy || '---');
        updateElement('min_strategy_time', formatTime(data.secondsInMinStrategy || 0));
        updateElement('max_strategy_time', formatTime(data.secondsInMaxStrategy || 0));
        updateElement('avg_bms_power', Math.round(data.bmsPowerAverage || 0), ' W');
    }

    // System Status - always update time
    const currentTime = new Date();
    updateElement('current_time', currentTime.toLocaleTimeString());

    // VE.Bus Status - only update if data is available
    if (typeof data.veBus_isOnline !== 'undefined') {
        const vebusIndicator = getElement('vebus-indicator');
        const vebusOnline = data.veBus_isOnline;
        updateElement('vebus_online', vebusOnline ? 'Online' : 'Offline');

        if (vebusIndicator) {
            vebusIndicator.className = 'status-indicator ' + (vebusOnline ? 'status-online' : 'status-offline');
        }

        updateElement('vebus_quality', Math.round((data.veBus_communicationQuality || 0) * 100), '%');
        updateElement('vebus_frames_sent', data.veBus_framesSent || '0');
        updateElement('vebus_frames_received', data.veBus_framesReceived || '0');
        updateElement('vebus_errors', ((data.veBus_checksumErrors || 0) + (data.veBus_timeoutErrors || 0)));
    }

    // Status LED - only update if data is available
    if (typeof data.statusLED_mode !== 'undefined') {
        const ledModes = ['Boot', 'WiFi Connecting', 'WiFi Connected', 'Normal Operation', 'Error'];
        updateElement('statusLED_mode', ledModes[data.statusLED_mode] || 'Unknown');
    }

    // Feed-in Power Control - only update if data is available
    if (typeof data.feedInControl_enabled !== 'undefined') {
        const feedInEnabled = data.feedInControl_enabled || false;
        updateElement('feedin_status', feedInEnabled ? 'Aktiv' : 'Inaktiv');
        updateElementStyle('feedin_status', 'color', feedInEnabled ? 'var(--victron-green)' : 'var(--victron-red)');
        updateElement('feedin_current', Math.round(data.feedInControl_current || 0), ' W');
        updateElement('feedin_target', Math.round(data.feedInControl_target || 0), ' W');
        updateElement('feedin_max', Math.round(data.feedInControl_max || 0), ' W');

        // Update form inputs if not currently being edited
        const feedinEnabledInput = getElement('feedin_enabled');
        if (feedinEnabledInput && !feedinEnabledInput.matches(':focus')) {
            feedinEnabledInput.checked = feedInEnabled;
        }

        const feedinTargetInput = getElement('feedin_target_input');
        if (feedinTargetInput && !feedinTargetInput.matches(':focus')) {
            feedinTargetInput.value = Math.round(data.feedInControl_target || 0);
        }

        const feedinMaxInput = getElement('feedin_max_input');
        if (feedinMaxInput && !feedinMaxInput.matches(':focus')) {
            feedinMaxInput.value = Math.round(data.feedInControl_max || 5000);
        }
    }

    // MQTT Status - only update if data is available
    if (data.mqtt) {
        const mqttConnected = data.mqtt.connected || false;
        updateElement('mqtt_status', mqttConnected ? 'Verbunden' : 'Getrennt');
        updateElementStyle('mqtt_status', 'color', mqttConnected ? 'var(--victron-green)' : 'var(--victron-red)');
        updateElement('mqtt_server', data.mqtt.server || '---');
        updateElement('mqtt_port', data.mqtt.port || '---');
        updateElement('mqtt_last_message', new Date().toLocaleTimeString());
    }

    // Protection & Warnings - only update if data is available
    if (typeof data.battery_protectionFlags1 !== 'undefined') {
        updateElement('battery_protection1', '0x' + (data.battery_protectionFlags1 ? data.battery_protectionFlags1.toString(16).toUpperCase() : '?'));
        updateElement('battery_protection2', '0x' + (data.battery_protectionFlags2 ? data.battery_protectionFlags2.toString(16).toUpperCase() : '?'));
        updateElement('battery_warning1', '0x' + (data.battery_warningFlags1 ? data.battery_warningFlags1.toString(16).toUpperCase() : '?'));
        updateElement('battery_warning2', '0x' + (data.battery_warningFlags2 ? data.battery_warningFlags2.toString(16).toUpperCase() : '?'));
        updateElement('battery_request', '0x' + (data.battery_requestFlags ? data.battery_requestFlags.toString(16).toUpperCase() : '?'));
    }

    // Control & Diagnostics - only update if data is available
    if (typeof data.minimumFeedIn !== 'undefined') {
        updateElement('minimum_feedin', (data.minimumFeedIn || '?'), ' W');
        updateElement('avg_ctrl_deviation', (data.averageControlDeviationFeedIn || '?'), ' W');
        updateElement('avg_charging_power', (data.averageChargingPower || '?'), ' W');
        updateElement('power_trend_cons', (data.powerTrendConsumption || '?'), ' Wh');
        updateElement('power_trend_feed', (data.powerTrendFeedIn || '?'), ' Wh');
    }
}

// Smart update function that only processes available data sections
function updateData(data) {
    if (!data) return;

    // Battery data section
    if (hasBatteryData(data)) {
        updateBatteryData(data);
    }

    // MultiPlus data section
    if (hasMultiPlusData(data)) {
        updateMultiPlusData(data);
    }

    // ESS data section
    if (hasESSData(data)) {
        updateESSData(data);
    }

    // VE.Bus data section
    if (hasVEbusData(data)) {
        updateVEbusData(data);
    }

    // System data section
    if (hasSystemData(data)) {
        updateSystemData(data);
    }

    // MQTT data section
    if (hasMQTTData(data)) {
        updateMQTTData(data);
    }

    // Protection/Warnings data section
    if (hasProtectionData(data)) {
        updateProtectionData(data);
    }

    // Control/Diagnostics data section
    if (hasControlData(data)) {
        updateControlData(data);
    }
}

// Helper functions to check if specific data sections are available
function hasBatteryData(data) {
    return typeof data.battery_soc !== 'undefined' ||
           typeof data.battery_voltage !== 'undefined' ||
           typeof data.battery_current !== 'undefined' ||
           typeof data.battery_temperature !== 'undefined' ||
           typeof data.battery_soh !== 'undefined' ||
           typeof data.battery_chargeVoltage !== 'undefined' ||
           typeof data.battery_chargeCurrentLimit !== 'undefined' ||
           typeof data.battery_dischargeCurrentLimit !== 'undefined' ||
           typeof data.battery_manufacturer !== 'undefined';
}

function hasMultiPlusData(data) {
    return typeof data.multiplusDcVoltage !== 'undefined' ||
           typeof data.multiplusDcCurrent !== 'undefined' ||
           typeof data.multiplusUMainsRMS !== 'undefined' ||
           typeof data.multiplusAcFrequency !== 'undefined' ||
           typeof data.multiplusPinverterFiltered !== 'undefined' ||
           typeof data.multiplusPmainsFiltered !== 'undefined' ||
           typeof data.multiplusPowerFactor !== 'undefined' ||
           typeof data.multiplusTemp !== 'undefined' ||
           typeof data.multiplusStatus80 !== 'undefined' ||
           typeof data.masterMultiLED_ActualInputCurrentLimit !== 'undefined';
}

function hasESSData(data) {
    return typeof data.multiplusESSpower !== 'undefined' ||
           typeof data.switchMode !== 'undefined' ||
           typeof data.essPowerStrategy !== 'undefined' ||
           typeof data.secondsInMinStrategy !== 'undefined' ||
           typeof data.secondsInMaxStrategy !== 'undefined' ||
           typeof data.bmsPowerAverage !== 'undefined';
}

function hasVEbusData(data) {
    return typeof data.veBus_isOnline !== 'undefined' ||
           typeof data.veBus_communicationQuality !== 'undefined' ||
           typeof data.veBus_framesSent !== 'undefined' ||
           typeof data.veBus_framesReceived !== 'undefined' ||
           typeof data.veBus_checksumErrors !== 'undefined' ||
           typeof data.veBus_timeoutErrors !== 'undefined';
}

function hasSystemData(data) {
    return typeof data.statusLED_mode !== 'undefined' ||
           typeof data.feedInControl_enabled !== 'undefined' ||
           typeof data.feedInControl_current !== 'undefined' ||
           typeof data.feedInControl_target !== 'undefined' ||
           typeof data.feedInControl_max !== 'undefined';
}

function hasMQTTData(data) {
    return data.mqtt && typeof data.mqtt.connected !== 'undefined';
}

function hasProtectionData(data) {
    return typeof data.battery_protectionFlags1 !== 'undefined' ||
           typeof data.battery_protectionFlags2 !== 'undefined' ||
           typeof data.battery_warningFlags1 !== 'undefined' ||
           typeof data.battery_warningFlags2 !== 'undefined' ||
           typeof data.battery_requestFlags !== 'undefined';
}

function hasControlData(data) {
    return typeof data.minimumFeedIn !== 'undefined' ||
           typeof data.averageControlDeviationFeedIn !== 'undefined' ||
           typeof data.averageChargingPower !== 'undefined' ||
           typeof data.powerTrendConsumption !== 'undefined' ||
           typeof data.powerTrendFeedIn !== 'undefined';
}

function formatTime(seconds) {
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    return h.toString().padStart(2, '0') + ':' + m.toString().padStart(2, '0') + ':' + s.toString().padStart(2, '0');
}

// Specific update functions for each data section
function updateBatteryData(data) {
    const soc = data.battery_soc || 0;
    updateElement('battery_soc', soc, '%');
    updateElement('battery_voltage', (data.battery_voltage || '?'), ' V');
    updateElement('battery_current', (data.battery_current || '?'), ' A');
    updateElement('battery_temperature', (data.battery_temperature || '?'), ' °C');
    updateElement('battery_soh', (data.battery_soh || '?'), '%');
    updateElement('battery_charge_voltage', (data.battery_chargeVoltage || '?'), ' V');
    updateElement('battery_charge_current_limit', (data.battery_chargeCurrentLimit || '?'), ' A');
    updateElement('battery_discharge_current_limit', (data.battery_dischargeCurrentLimit || '?'), ' A');
    updateElement('battery_manufacturer', data.battery_manufacturer || '---');

    // Update battery SOC progress bar
    const progressBar = getElement('battery-progress');
    if (progressBar) {
        progressBar.style.width = Math.max(0, Math.min(100, soc)) + '%';
        if (soc > 70) {
            progressBar.style.background = 'linear-gradient(90deg, var(--victron-green), #20C997)';
        } else if (soc > 30) {
            progressBar.style.background = 'linear-gradient(90deg, var(--victron-orange), #FFB347)';
        } else {
            progressBar.style.background = 'linear-gradient(90deg, var(--victron-red), #FF6B6B)';
        }
    }
}

function updateMultiPlusData(data) {
    updateElement('multiplus_dc_voltage', (data.multiplusDcVoltage || '?'), ' V');
    updateElement('multiplus_dc_current', (data.multiplusDcCurrent || '?'), ' A');
    updateElement('multiplus_ac_voltage', (data.multiplusUMainsRMS || '?'), ' V');
    updateElement('multiplus_ac_frequency', (data.multiplusAcFrequency || '?'), ' Hz');
    updateElement('multiplus_inverter_power', (data.multiplusPinverterFiltered || '?'), ' W');
    updateElement('multiplus_acin_power', (data.multiplusPmainsFiltered || '?'), ' W');
    updateElement('multiplus_power_factor', data.multiplusPowerFactor || '---');
    updateElement('multiplus_temperature', (data.multiplusTemp || '?'), ' °C');
    updateElement('multiplus_status', data.multiplusStatus80 || '---');
    updateElement('multiplus_current_limit', (data.masterMultiLED_ActualInputCurrentLimit || '?'), ' A');
}

function updateESSData(data) {
    const essPower = data.multiplusESSpower || 0;
    updateElement('ess_power', Math.abs(essPower), ' W');

    const powerDirection = getElement('power_direction');
    const essPowerElement = getElement('ess_power');
    if (powerDirection && essPowerElement) {
        if (essPower > 100) {
            powerDirection.textContent = 'Charging';
            essPowerElement.style.color = 'var(--victron-green)';
        } else if (essPower < -100) {
            powerDirection.textContent = 'Discharging';
            essPowerElement.style.color = 'var(--victron-orange)';
        } else {
            powerDirection.textContent = 'Standby';
            essPowerElement.style.color = 'var(--victron-blue)';
        }
    }

    updateElement('switch_mode', data.switchMode || '---');
    updateElement('strategy', data.essPowerStrategy || '---');
    updateElement('min_strategy_time', formatTime(data.secondsInMinStrategy || 0));
    updateElement('max_strategy_time', formatTime(data.secondsInMaxStrategy || 0));
    updateElement('avg_bms_power', Math.round(data.bmsPowerAverage || 0), ' W');
}

function updateVEbusData(data) {
    const vebusIndicator = getElement('vebus-indicator');
    const vebusOnline = data.veBus_isOnline;
    updateElement('vebus_online', vebusOnline ? 'Online' : 'Offline');

    if (vebusIndicator) {
        vebusIndicator.className = 'status-indicator ' + (vebusOnline ? 'status-online' : 'status-offline');
    }

    updateElement('vebus_quality', Math.round((data.veBus_communicationQuality || 0) * 100), '%');
    updateElement('vebus_frames_sent', data.veBus_framesSent || '0');
    updateElement('vebus_frames_received', data.veBus_framesReceived || '0');
    updateElement('vebus_errors', ((data.veBus_checksumErrors || 0) + (data.veBus_timeoutErrors || 0)));
}

function updateSystemData(data) {
    const ledModes = ['Boot', 'WiFi Connecting', 'WiFi Connected', 'Normal Operation', 'Error'];
    updateElement('statusLED_mode', ledModes[data.statusLED_mode] || 'Unknown');

    const feedInEnabled = data.feedInControl_enabled || false;
    updateElement('feedin_status', feedInEnabled ? 'Aktiv' : 'Inaktiv');
    updateElementStyle('feedin_status', 'color', feedInEnabled ? 'var(--victron-green)' : 'var(--victron-red)');
    updateElement('feedin_current', Math.round(data.feedInControl_current || 0), ' W');
    updateElement('feedin_target', Math.round(data.feedInControl_target || 0), ' W');
    updateElement('feedin_max', Math.round(data.feedInControl_max || 0), ' W');

    // Update form inputs if not currently being edited
    const feedinEnabledInput = getElement('feedin_enabled');
    if (feedinEnabledInput && !feedinEnabledInput.matches(':focus')) {
        feedinEnabledInput.checked = feedInEnabled;
    }

    const feedinTargetInput = getElement('feedin_target_input');
    if (feedinTargetInput && !feedinTargetInput.matches(':focus')) {
        feedinTargetInput.value = Math.round(data.feedInControl_target || 0);
    }

    const feedinMaxInput = getElement('feedin_max_input');
    if (feedinMaxInput && !feedinMaxInput.matches(':focus')) {
        feedinMaxInput.value = Math.round(data.feedInControl_max || 5000);
    }
}

function updateMQTTData(data) {
    const mqttConnected = data.mqtt.connected || false;
    updateElement('mqtt_status', mqttConnected ? 'Verbunden' : 'Getrennt');
    updateElementStyle('mqtt_status', 'color', mqttConnected ? 'var(--victron-green)' : 'var(--victron-red)');
    updateElement('mqtt_server', data.mqtt.server || '---');
    updateElement('mqtt_port', data.mqtt.port || '---');
    updateElement('mqtt_last_message', new Date().toLocaleTimeString());
}

function updateProtectionData(data) {
    updateElement('battery_protection1', '0x' + (data.battery_protectionFlags1 ? data.battery_protectionFlags1.toString(16).toUpperCase() : '?'));
    updateElement('battery_protection2', '0x' + (data.battery_protectionFlags2 ? data.battery_protectionFlags2.toString(16).toUpperCase() : '?'));
    updateElement('battery_warning1', '0x' + (data.battery_warningFlags1 ? data.battery_warningFlags1.toString(16).toUpperCase() : '?'));
    updateElement('battery_warning2', '0x' + (data.battery_warningFlags2 ? data.battery_warningFlags2.toString(16).toUpperCase() : '?'));
    updateElement('battery_request', '0x' + (data.battery_requestFlags ? data.battery_requestFlags.toString(16).toUpperCase() : '?'));
}

function updateControlData(data) {
    updateElement('minimum_feedin', (data.minimumFeedIn || '?'), ' W');
    updateElement('avg_ctrl_deviation', (data.averageControlDeviationFeedIn || '?'), ' W');
    updateElement('avg_charging_power', (data.averageChargingPower || '?'), ' W');
    updateElement('power_trend_cons', (data.powerTrendConsumption || '?'), ' Wh');
    updateElement('power_trend_feed', (data.powerTrendFeedIn || '?'), ' Wh');
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
    const btn = getElement('.refresh-btn');
    if (btn) {
        btn.style.transform = 'scale(0.9) rotate(180deg)';
        setTimeout(() => {
            btn.style.transform = 'scale(1) rotate(0deg)';
        }, 200);
    }

    // Reconnect WebSocket if needed
    if (ws.readyState !== WebSocket.OPEN) {
        connectWS();
    }
}

// MQTT Configuration Modal Functions
function openMqttModal() {
    console.log('Opening MQTT modal...');
    const modal = getElement('mqttModal');
    if (!modal) {
        console.error('MQTT modal not found!');
        return;
    }
    modal.style.display = 'block';
    loadMqttConfig();

    // Add event listener when modal opens
    const form = getElement('mqtt_modal_form');
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
    const modal = getElement('mqttModal');
    if (modal) {
        modal.style.display = 'none';
    }
}

// Close modal when clicking outside
window.onclick = function(event) {
    const modal = getElement('mqttModal');
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
        const serverInput = getElement('mqtt_server_input');
        if (serverInput) serverInput.value = data.server || '';

        const portInput = getElement('mqtt_port_input');
        if (portInput) portInput.value = data.port || 1883;

        const usernameInput = getElement('mqtt_username_input');
        if (usernameInput) usernameInput.value = data.username || '';

        // Don't populate password field for security reasons
        const passwordInput = getElement('mqtt_password_input');
        if (passwordInput) {
            passwordInput.value = '';
            passwordInput.placeholder = data.password ? 'Password set (hidden)' : 'Enter password';
        }
    })
    .catch(error => {
        console.error('Error loading MQTT config:', error);
    });
}

// Handle MQTT modal form submission
function handleMqttFormSubmit(e) {
    e.preventDefault();

    const serverInput = getElement('mqtt_server_input');
    const portInput = getElement('mqtt_port_input');
    const usernameInput = getElement('mqtt_username_input');
    const passwordInput = getElement('mqtt_password_input');

    const mqttConfig = {
        server: serverInput ? serverInput.value.trim() : '',
        port: portInput ? parseInt(portInput.value) : 1883,
        username: usernameInput ? usernameInput.value.trim() : '',
        password: passwordInput ? passwordInput.value.trim() : ''
    };

    if (!mqttConfig.server) {
        alert('Bitte geben Sie einen MQTT-Server an!');
        return;
    }

    // Disable form during submission
    const submitBtn = e.target.querySelector('button[type="submit"]');
    const originalText = submitBtn ? submitBtn.textContent : 'Speichern';
    if (submitBtn) {
        submitBtn.textContent = '💾 Speichere...';
        submitBtn.disabled = true;
    }

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
        if (submitBtn) {
            submitBtn.textContent = originalText;
            submitBtn.disabled = false;
        }
    });
}

// Load current MQTT status for display
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
        updateElement('mqtt_status', mqttConnected ? 'Verbunden ✅' : 'Getrennt ❌');
        updateElementStyle('mqtt_status', 'color', mqttConnected ? 'var(--victron-green)' : 'var(--victron-red)');
        updateElement('mqtt_server_display', data.server || 'Nicht konfiguriert');
        updateElement('mqtt_port_display', data.port || '---');
        updateElement('mqtt_last_message', data.lastMessage || 'Keine');
    })
    .catch(error => {
        console.error('Error loading MQTT status:', error);
        updateElement('mqtt_status', 'Fehler beim Laden ❌');
        updateElement('mqtt_server_display', 'API-Fehler');
        updateElement('mqtt_port_display', '---');
        updateElement('mqtt_last_message', error.message);
    });
}

// Event listeners that need to be added after DOM loads
document.addEventListener('DOMContentLoaded', function() {
    // MQTT configuration form handler (only if modal form doesn't exist)
    const mqttModalForm = getElement('mqtt_modal_form');
    const mqttForm = getElement('mqtt_form');

    if (mqttModalForm && !mqttModalForm.hasAttribute('data-listener-added')) {
        // Use the modal form handler
        console.log('MQTT modal form found, listener should already be added');
    } else if (mqttForm && !mqttForm.hasAttribute('data-listener-added')) {
        console.log('Adding MQTT form submit event listener...');
        mqttForm.addEventListener('submit', function(e) {
            e.preventDefault();

            const serverInput = getElement('mqtt_server_input');
            const portInput = getElement('mqtt_port_input');
            const usernameInput = getElement('mqtt_username_input');
            const passwordInput = getElement('mqtt_password_input');

            const server = serverInput ? serverInput.value.trim() : '';
            const port = portInput ? (parseInt(portInput.value) || 1883) : 1883;
            const username = usernameInput ? usernameInput.value.trim() : '';
            const password = passwordInput ? passwordInput.value : '';

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
        mqttForm.setAttribute('data-listener-added', 'true');
    }

    // Feed-in control form handler
    const feedinForm = getElement('feedin_form');
    if (feedinForm) {
        feedinForm.addEventListener('submit', function(e) {
            e.preventDefault();

            const enabledInput = getElement('feedin_enabled');
            const targetInput = getElement('feedin_target_input');
            const maxInput = getElement('feedin_max_input');

            const enabled = enabledInput ? enabledInput.checked : false;
            const target = targetInput ? (parseFloat(targetInput.value) || 0) : 0;
            const max = maxInput ? (parseFloat(maxInput.value) || 5000) : 5000;
            
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
