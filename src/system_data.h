/*
 * System Data Structures for ESP32 ESS Controller
 * 
 * This header file contains structured data types for organizing
 * all system variables in a clean and maintainable way.
 * 
 * SPDX-FileCopyrightText: © 2023 PV Baxi <pv-baxi@gmx.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SYSTEM_DATA_H
#define SYSTEM_DATA_H

#include <Arduino.h>
#include <stdint.h>

// Maximum sizes and constants - DRASTICALLY REDUCED FOR ESP32
#define MAX_METER_STATUS_BYTES 6
#define SML_LENGTH_MAX 400
// LOGFILE_ALL_SIZE and LOGFILE_FEW_SIZE removed - no more logging buffers!
#define CPS 10000                               // Cycles per second for timer
#define DEFAULT_SHELLY_SWITCHING_INTERVAL 450  // Default Shelly switching interval

// Battery Management System (BMS) Data Structure
struct BatteryData {
    int16_t soc = -1;                           // Battery level 0-100% (negative = invalid)
    int16_t socMin = 32767;                    // Used to log minimum battery level
    int16_t socMax = -32768;                   // Used to log maximum battery level
    uint32_t socMinTime = 0;                    // Time when minimum SOC occurred
    uint32_t socMaxTime = 0;                    // Time when maximum SOC occurred
    int16_t soh = -1;                           // Battery health 0-100% (negative = invalid)
    float chargeVoltage = -1;                   // Maximum charge voltage
    float chargeCurrentLimit = -1;              // Maximum charge current limit
    float dischargeCurrentLimit = -1;           // Maximum discharge current limit
    float dischargeVoltage = -1;                // Minimum discharge voltage
    float voltage = -1;                         // Current battery voltage
    float current = -1;                         // Current battery current
    int power = -1;                             // Current battery power
    float temperature = -1;                     // Battery temperature
    char manufacturer[9] = {0};                 // Battery manufacturer string
    int8_t nrPacksInParallel = -1;             // Number of battery packs in parallel
    uint8_t protectionFlags1 = 0;              // Protection flags byte 1
    uint8_t protectionFlags2 = 0;              // Protection flags byte 2
    uint8_t warningFlags1 = 0;                 // Warning flags byte 1
    uint8_t warningFlags2 = 0;                 // Warning flags byte 2
    uint8_t requestFlags = 0;                  // Request flags
};

// Electric Meter Data Structure
struct ElectricMeterData {
    char deviceID[10];                          // Electric meter device ID
    char status180[MAX_METER_STATUS_BYTES];     // Status sent with 1.8.0
    double consumption = 0;                     // kWh value 1.8.0 (consumption)
    double feedIn = 0;                          // kWh value 2.8.0 (feed-in)
    uint32_t runtime = 0;                       // Meter runtime in seconds
    double power = 0;                           // Current total power
    double powerL1 = 0;                         // Power phase L1
    double powerL2 = 0;                         // Power phase L2
    double powerL3 = 0;                         // Power phase L3
    uint16_t crcWrong = 0;                      // CRC failure counter
    
    // Hourly consumption and feed-in arrays
    double hourlyConsumption[24] = {0};         // Consumption per hour 0-23
    double hourlyFeedIn[24] = {0};              // Feed-in per hour 0-23
    double consumption24h = 0;                  // 24h total consumption
    double feedIn24h = 0;                       // 24h total feed-in
};

// Multiplus Inverter Data Structure
struct MultiplusData {
    int16_t esspower = -1;                      // ESS power value currently applied
    float temp = 11.1;                          // Multiplus temperature
    float dcCurrent = -22.2;                    // DC current
    float dcVoltage = 48.0;                     // DC voltage
    int16_t batteryAh = -12345;                 // Battery capacity
    float acFrequency = 50.0;                   // AC frequency
    float uMainsRMS = 230.0;                    // AC input voltage RMS
    float powerFactor = 1.0;                    // Power factor
    int pinverterFiltered = 0;                  // Filtered inverter power
    int pmainsFiltered = 0;                     // Filtered mains power
    
    // Status and control variables
    uint8_t status80 = 23;                      // Charger/Inverter Status (00=ok, 02=battery low)
    uint8_t voltageStatus = 0;                  // Voltage status
    uint8_t emergencyPowerStatus = 0;           // Emergency power status (00=mains, 02=emergency)
    uint8_t battery_byte07 = 0;                 // Battery frame byte 07
    uint8_t battery_byte06 = 0;                 // Battery frame byte 06
    uint8_t battery_byte05 = 0;                 // Battery frame byte 05
    uint8_t e4_byte18 = 0;                      // E4 frame byte 18
    uint8_t e4_byte17 = 0;                      // E4 frame byte 17
    uint8_t e4_byte12 = 0;                      // E4 frame byte 12
    uint8_t e4_byte11 = 0;                      // E4 frame byte 11
    
    // LED status
    uint8_t masterMultiLED_Status = 12;         // LED status (0=ok, 2=battery low)
    uint8_t masterMultiLED_SwitchRegister = 0;  // Switch register
    uint8_t masterMultiLED_LEDon = 123;         // LED on status (bits 0..7)
    uint8_t masterMultiLED_LEDblink = 234;      // LED blink status
    float masterMultiLED_ActualInputCurrentLimit = 0; // AC input current limit
    uint8_t masterMultiLED_AcInputConfiguration = 0;   // AC input configuration
    float masterMultiLED_MinimumInputCurrentLimit = 0; // Minimum input current limit
    float masterMultiLED_MaximumInputCurrentLimit = 0; // Maximum input current limit
    
    // Additional timing and phase data
    uint32_t e4_Timestamp = 0;                  // E4 frame timestamp (unit = 400ns)
    int8_t acPhase = 0;                         // AC phase angle
    
    // Calibration data
    float dcVoltageCalibration = 0;             // DC voltage calibration
    int dcVoltageCalibrationCnt = 0;            // Calibration counter
};

// VE.Bus Communication Data Structure
struct VeBusData {
    char frbuf0[256];                           // Complete frame received by Multiplus
    char frbuf1[256];                           // Frame without replacement
    char txbuf1[64];                            // Command buffer (bare)
    char txbuf2[64];                            // Command buffer (final with checksum)
    uint16_t frp = 0;                           // Frame buffer pointer
    uint8_t frameNr = 0;                        // Last frame number received
    int synccnt = 0;                            // Sync frame counter
    int cmdCounter = 0;                         // Total command counter
    int txCmdFailCnt = 0;                       // Failed TX commands
    int rxCmdFailCnt = 0;                       // Failed RX commands
    uint8_t cmdSendState = 0;                   // Command send state
    int cmdAckCnt = 0;                          // Command acknowledgment counter
};

// ESS Control Data Structure
struct ESSControlData {
    int16_t powerTmp = 0;                       // ESS power calculation temp
    int16_t powerTmp2 = 0;                      // ESS power after limitations
    int16_t powerDesired = 0;                   // ESS power to be written to Multiplus
    int16_t socInverterOnOff = 50;              // Adaptive SOC threshold
    bool chargeOnly = false;                    // Charge only mode flag
    bool chargeFromACin = true;                 // Allow ACin charging
    int gridSetpoint = 0;                       // Power compensation offset
    char switchMode = 'A';                      // Current switch mode
    uint32_t socLastBalanced = 0;               // Time when battery was last balanced
    uint32_t secondsInMinStrategy = 0;          // Time in minimum strategy
    uint32_t secondsInMaxStrategy = 0;          // Time in maximum strategy
    int essTarget = 0;                          // ESS target power
    int essIgnored = 0;                         // ESS commands ignored
    char essStrategy[32] = "normal";            // Current ESS strategy
};

// Power Meter Processing Data Structure
struct PowerMeterData {
    int impulseMeterPower = 0;                  // Power from impulse meter
    int decisiveMeterPower = 0;                 // Power value used in calculations
    bool newImpulseMeterPower = false;          // New impulse meter data flag
    bool newDigitalMeterPower = false;          // New digital meter data flag
    bool newMeterValue = false;                 // New meter value available flag
    int infoDssCntSinceLastMeterPower = 0;      // Info-DSS message counter
    
    // SML processing buffers
    char sBuf[1024];                            // Circular SML buffer
    char sBuf2[SML_LENGTH_MAX];                 // Linear SML buffer
    uint16_t smlp = 0;                          // SML buffer pointer
    uint16_t smlCnt = 0;                        // SML stream length counter
    uint16_t smlLength = 0;                     // Detected SML stream length
    
    // Power trend analysis
    int powerTrendRingbuf[5*60];                // 5 minute power trend buffer (reduced from 15*60 to 5*60)
    int powerTrendPtr = 0;                      // Power trend buffer pointer
    float powerTrendConsumption = 0;            // Power trend consumption
    float powerTrendFeedIn = 0;                 // Power trend feed-in
};

// System Status Data Structure
struct SystemStatusData {
    bool timeIsValid = false;                   // Valid time flag
    bool batteryStartedCharging = true;         // Battery charging status
    int minimumFeedIn = 0;                      // Minimum feed-in power
    int averageControlDeviationFeedIn = 0;      // Average control deviation
    int averageChargingPower = 0;               // Average charging power
    double bmsPowerAverage = 0;                 // BMS power average
    float alpha = 0.03;                         // Averaging factor
    
    // Min/Max tracking
    float batteryTempMin = 999;                 // Minimum battery temperature
    float batteryTempMax = -999;                // Maximum battery temperature
    float batteryCurrentMin = 999;              // Minimum battery current
    float batteryCurrentMax = -999;             // Maximum battery current
    int batteryPowerMin = 99999;                // Minimum battery power
    int batteryPowerMax = -99999;               // Maximum battery power
    float dcCurrentMin = 0.0;                   // Minimum DC current
    float dcCurrentMax = 0.0;                   // Maximum DC current
    float dcVoltageMin = 999;                   // Minimum DC voltage
    float dcVoltageMax = 0;                     // Maximum DC voltage
    float acVoltageMin = 999;                   // Minimum AC voltage
    float acVoltageMax = 0;                     // Maximum AC voltage
    uint32_t timeAcVoltageMin = 0;              // Time of minimum AC voltage
    uint32_t timeAcVoltageMax = 0;              // Time of maximum AC voltage
    float acFrequencyMin = 999;                 // Minimum AC frequency
    float acFrequencyMax = 0;                   // Maximum AC frequency
    uint32_t timeAcFrequencyMin = 0;            // Time of minimum AC frequency
    uint32_t timeAcFrequencyMax = 0;            // Time of maximum AC frequency
    float multiplusTempMin = 999;               // Minimum Multiplus temperature
    float multiplusTempMax = -999;              // Maximum Multiplus temperature
    
    // Cable resistance measurements
    float chargeCableResistance = 0;            // Cable resistance during charging
    int chargeCableResistanceCnt = 0;           // Cable resistance measurement count
    float dischargeCableResistance = 0;         // Cable resistance during discharge
    int dischargeCableResistanceCnt = 0;        // Cable resistance measurement count
};

// Shelly Consumer Control Data Structure
struct ShellyControlData {
    int shellyState = 0;                        // Current Shelly state
    int shellyActuations = 0;                   // Total Shelly actuations
    int shellyFails = 0;                        // Failed Shelly commands
    int nrShellysEnabledByRule = 0;             // Shellys enabled by rules
    uint32_t shellyWaitCnt = 0;                 // Shelly switching wait counter
    double electricMeterConsumptionOneHourAgo = -1; // Consumption one hour ago
    double electricMeterFeedInOneHourAgo = -1;  // Feed-in one hour ago
    int timeNewHourDone = -1;                   // Last hour processed
};

// Timer and ISR Data Structure
struct TimerData {
    volatile int essTimeoutCounter = 0;         // ESS timeout counter
    volatile int socWatchdog = 20000;           // SOC watchdog timer
    volatile int automaticMinMaxModeCnt = 0;    // Automatic mode counter
    volatile int buttonPressCnt = -1;           // Button press counter
    volatile int cylTime = 0;                   // Cycle time counter
    volatile bool isrOneSecondOver = false;     // One second ISR flag
    volatile bool isrOneMinuteOver = false;     // One minute ISR flag
    volatile int minuteTimer = 0;               // Minute timer
    bool oneSecondOver = false;                 // Main loop one second flag
    bool oneMinuteOver = false;                 // Main loop one minute flag
};

// Power Calculation and Ring Buffer Data Structure
struct PowerCalculationData {
    int electricMeterStatusDifferent = 0;       // Status difference counter
    int electricMeterSignPositive = 0;          // Positive sign counter
    int electricMeterSignNegative = 0;          // Negative sign counter
    int electricMeterCurrentSign = 0;           // Current sign decision
    int16_t estTargetPower = 0;                 // ESS target power
    int essPowerStrategy = 5;                   // Power strategy
    int16_t estTargetPowerRingBuf[10] = {0};    // Target power ring buffer (reduced from 45 to 10)
    int ptrPowerRingBuf = 0;                    // Power ring buffer pointer
    int16_t pACinRingBuf[16] = {0};             // AC input ring buffer (reduced from 32 to 16)
    int ptr_pACinRingBuf = 0;                   // AC input buffer pointer
    int16_t pInverterRingBuf[16] = {0};         // Inverter ring buffer (reduced from 32 to 16)
    int ptr_pInverterRingBuf = 0;               // Inverter buffer pointer
    int16_t powerACin = 0;                      // Power on AC input
    int16_t powerChargerRingBuf[50] = {0};      // Charger power buffer (reduced from 400 to 50)
    int ptr_powerChargerRingBuf = 0;            // Charger buffer pointer
    int16_t powerControlDeviationRingBuf[50] = {0}; // Control deviation buffer (reduced from 400 to 50)
    int ptr_powerControlDeviationRingBuf = 0;   // Control deviation pointer
    int16_t powerMeterRingBuf[50] = {0};        // Meter power buffer (reduced from 400 to 50)
    int ptr_powerMeterRingBuf = 0;              // Meter buffer pointer
    float beta = 0.001;                         // Cable resistance averaging factor
};

// Optical Meter Measurement Data
struct OpticalMeterData {
    volatile bool IRpinDisplay = LOW;           // IR pin display state
    volatile int highCnt = 1000;                // High pulse counter
    volatile int totalCnt = 4000000;            // Total measurement counter
    volatile int highCycles = 0;                // High pulse cycles
    volatile int highCyclesPrevious = 0;        // Previous high pulse cycles
    volatile int totalCycles = 0;               // Total measurement cycles
    volatile bool meterHighPulse = false;       // Current pulse state
    volatile bool isrNewMeterPulse = false;     // New pulse flag
    volatile int shelly1PMcnt = 0;              // Shelly 1PM counter
    volatile int shelly1PMpulsewidth = 100;     // Shelly 1PM pulse width
};

// Main System Data Structure for efficient memory usage
struct SystemData {
    BatteryData battery;
    ElectricMeterData electricMeter;
    MultiplusData multiplus;
    VeBusData veBus;
    ESSControlData essControl;
    PowerMeterData powerMeter;
    SystemStatusData systemStatus;
    ShellyControlData shellyControl;
    TimerData timer;
    PowerCalculationData powerCalc;
    OpticalMeterData opticalMeter;
};

// Global system data instance declaration
extern SystemData systemData;

#endif // SYSTEM_DATA_H
