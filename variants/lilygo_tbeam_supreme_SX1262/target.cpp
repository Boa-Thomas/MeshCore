#include <Arduino.h>
#include "target.h"
#include <helpers/sensors/MicroNMEALocationProvider.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/SH1106Display.h>
#endif

TBeamS3SupremeBoard board;

bool pmuIntFlag;

#ifndef LORA_CR
  #define LORA_CR      5
#endif

#if defined(P_LORA_SCLK)
  static SPIClass spi;
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1);
TbeamSupSensorManager sensors = TbeamSupSensorManager(nmea);

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
#endif

static void setPMUIntFlag(){
  pmuIntFlag = true;
}

#ifdef MESH_DEBUG
uint32_t deviceOnline = 0x00;
void scanDevices(TwoWire *w)
{
    uint8_t err, addr;
    int nDevices = 0;
    uint32_t start = 0;

    Serial.println("Scanning I2C for Devices");
    for (addr = 1; addr < 127; addr++) {
        start = millis();
        w->beginTransmission(addr); delay(2);
        err = w->endTransmission();
        if (err == 0) {
            nDevices++;
            switch (addr) {
            case 0x77:
            case 0x76:
                Serial.println("\tFound BMX280 Sensor");
                deviceOnline |= BME280_ONLINE;
                break;
            case 0x34:
                Serial.println("\tFound AXP192/AXP2101 PMU");
                deviceOnline |= POWERMANAGE_ONLINE;
                break;
            case 0x3C:
                Serial.println("\tFound SSD1306/SH1106 dispaly");
                deviceOnline |= DISPLAY_ONLINE;
                break;
            case 0x51:
                Serial.println("\tFound PCF8563 RTC");
                deviceOnline |= PCF8563_ONLINE;
                break;
            case 0x1C:
                Serial.println("\tFound QMC6310 MAG Sensor");
                deviceOnline |= QMC6310_ONLINE;
                break;
            default:
                Serial.print("\tI2C device found at address 0x");
                if (addr < 16) {
                    Serial.print("0");
                }
                Serial.print(addr, HEX);
                Serial.println(" !");
                break;
            }

        } else if (err == 4) {
            Serial.print("Unknow error at address 0x");
            if (addr < 16) {
                Serial.print("0");
            }
            Serial.println(addr, HEX);
        }
    }
    if (nDevices == 0)
        Serial.println("No I2C devices found\n");

    Serial.println("Scan for devices is complete.");
    Serial.println("\n");
}
void TBeamS3SupremeBoard::printPMU()
{
    Serial.print("isCharging:"); Serial.println(PMU.isCharging() ? "YES" : "NO");
    Serial.print("isDischarge:"); Serial.println(PMU.isDischarge() ? "YES" : "NO");
    Serial.print("isVbusIn:"); Serial.println(PMU.isVbusIn() ? "YES" : "NO");
    Serial.print("getBattVoltage:"); Serial.print(PMU.getBattVoltage()); Serial.println("mV");
    Serial.print("getVbusVoltage:"); Serial.print(PMU.getVbusVoltage()); Serial.println("mV");
    Serial.print("getSystemVoltage:"); Serial.print(PMU.getSystemVoltage()); Serial.println("mV");

    // The battery percentage may be inaccurate at first use, the PMU will automatically
    // learn the battery curve and will automatically calibrate the battery percentage
    // after a charge and discharge cycle
    if (PMU.isBatteryConnect()) {
        Serial.print("getBatteryPercent:"); Serial.print(PMU.getBatteryPercent()); Serial.println("%");
    }

    Serial.println();
}
#endif

bool TBeamS3SupremeBoard::power_init()
{
  bool result = PMU.begin(PMU_WIRE_PORT, I2C_PMU_ADD, PIN_BOARD_SDA1, PIN_BOARD_SCL1);
  if (result == false) {
    MESH_DEBUG_PRINTLN("power is not online..."); while (1)delay(50);
  }
  MESH_DEBUG_PRINTLN("Setting charge led");
  PMU.setChargingLedMode(XPOWERS_CHG_LED_CTRL_CHG);

  // Set up PMU interrupts
  // MESH_DEBUG_PRINTLN("Setting up PMU interrupts");
  // pinMode(PIN_PMU_IRQ, INPUT_PULLUP);
  // attachInterrupt(PIN_PMU_IRQ, setPMUIntFlag, FALLING);

  // GPS
  MESH_DEBUG_PRINTLN("Setting and enabling a-ldo4 for GPS");
  PMU.setALDO4Voltage(3300);
  PMU.enableALDO4(); // disable to save power

  // Lora
  MESH_DEBUG_PRINTLN("Setting and enabling a-ldo3 for LoRa");
  PMU.setALDO3Voltage(3300);
  PMU.enableALDO3();

  // To avoid SPI bus issues during power up, reset OLED, sensor, and SD card supplies
  // MESH_DEBUG_PRINTLN("Reset a-ldo1&2 and b-ldo1");
  // if (ESP_SLEEP_WAKEUP_UNDEFINED == esp_sleep_get_wakeup_cause())
  // {
  //   PMU.disableALDO1();
  //   PMU.disableALDO2();
  //   PMU.disableBLDO1();
  //   delay(250);
  // }

  // m.2 interface
  MESH_DEBUG_PRINTLN("Setting and enabling dcdc3 for m.2 interface");
  PMU.setDC3Voltage(3300); // doesn't go anywhere in the schematic??
  PMU.enableDC3();

  // QMC6310U
  MESH_DEBUG_PRINTLN("Setting and enabling a-ldo2 for QMC");
  PMU.setALDO2Voltage(3300);
  PMU.enableALDO2(); // disable to save power

  // BME280 and OLED
  MESH_DEBUG_PRINTLN("Setting and enabling a-ldo1 for oled");
  PMU.setALDO1Voltage(3300);
  PMU.enableALDO1();

  // SD card
  MESH_DEBUG_PRINTLN("Setting and enabling b-ldo2 for SD card");
  PMU.setBLDO1Voltage(3300);
  PMU.enableBLDO1();

  // Out to header pins
  // MESH_DEBUG_PRINTLN("Setting and enabling b-ldo2 for output to header");
  // PMU.setBLDO2Voltage(3300);
  // PMU.enableBLDO2();

  // MESH_DEBUG_PRINTLN("Setting and enabling dcdc4 for output to header");
  // PMU.setDC4Voltage(XPOWERS_AXP2101_DCDC4_VOL2_MAX); // 1.8V
  // PMU.enableDC4();

  // MESH_DEBUG_PRINTLN("Setting and enabling dcdc5 for output to header");
  // PMU.setDC5Voltage(3300);
  // PMU.enableDC5();

  // Unused power rails
  MESH_DEBUG_PRINTLN("Disabling unused supplies dcdc2, dldo1 and dldo2");
  PMU.disableDC2();
  PMU.disableDC5();
  PMU.disableDLDO1();
  PMU.disableDLDO2();

  PMU.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);

  // Set charge current to 500mA
  MESH_DEBUG_PRINTLN("Setting battery charge current limit and voltage");
  PMU.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);
  PMU.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);

  PMU.clearIrqStatus();
  PMU.disableTSPinMeasure();

  // enable battery voltage measurement
  MESH_DEBUG_PRINTLN("Enabling battery measurement");
  PMU.enableBattVoltageMeasure();
  PMU.enableVbusVoltageMeasure();

  // Reset and re-enable PMU interrupts
  // MESH_DEBUG_PRINTLN("Re-enable interrupts");
  // PMU.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  // PMU.clearIrqStatus();
  // PMU.enableIRQ(
  //     XPOWERS_AXP2101_BAT_INSERT_IRQ | XPOWERS_AXP2101_BAT_REMOVE_IRQ |    // Battery interrupts
  //     XPOWERS_AXP2101_VBUS_INSERT_IRQ | XPOWERS_AXP2101_VBUS_REMOVE_IRQ |  // VBUS interrupts
  //     XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ |     // Power Key interrupts
  //     XPOWERS_AXP2101_BAT_CHG_DONE_IRQ | XPOWERS_AXP2101_BAT_CHG_START_IRQ // Charging interrupts
  // );
#ifdef MESH_DEBUG
  // scanDevices(&Wire);
  // scanDevices(&Wire1);
  printPMU();
#endif

  // Set the power key off press time
  PMU.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
  return true;
}

static bool readStringUntil(Stream& s, char dest[], size_t max_len, char term, unsigned int timeout_millis) {
  unsigned long timeout = millis() + timeout_millis;
  char *dp = dest;
  while (millis() < timeout && dp - dest < max_len - 1) {
    if (s.available()) {
      char c = s.read();
      if (c == term) break;
      *dp++ = c;  // append to dest[]
    } else {
      delay(1);
    }
  }
  *dp = 0;  // null terminator
  return millis() < timeout;   // false, if timed out
}

static bool l76kProbe()
{
    bool result = false;
    uint32_t startTimeout ;
    Serial1.write("$PCAS03,0,0,0,0,0,0,0,0,0,0,,,0,0*02\r\n");
    delay(5);
    // Get version information
    startTimeout = millis() + 3000;
    MESH_DEBUG_PRINTLN("Trying to init L76K GPS");
    // Serial1.flush();
    while (Serial1.available()) {
        int c = Serial1.read();
        // Serial.write(c);
        // Serial.print(".");
        // Serial.flush();
        // Serial1.flush();
        if (millis() > startTimeout) {
            MESH_DEBUG_PRINTLN("L76K NMEA timeout!");
            return false;
        }
    };

    Serial1.flush();
    delay(200);

    Serial1.write("$PCAS06,0*1B\r\n");

    char ver[100];
    if (!readStringUntil(Serial1, ver, sizeof(ver), '\n', 500)) {
      MESH_DEBUG_PRINTLN("Get L76K timeout!");
      return false;
    }

    if (memcmp(ver, "$GPTXT,01,01,02", 15) == 0) {
        MESH_DEBUG_PRINTLN("L76K GNSS init succeeded, using L76K GNSS Module\n");
        result = true;
    }
    delay(500);

    // Initialize the L76K Chip, use GPS + GLONASS
    Serial1.write("$PCAS04,5*1C\r\n");
    delay(250);
    // only ask for RMC and GGA
    Serial1.write("$PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0*02\r\n");
    delay(250);
    // Switch to Vehicle Mode, since SoftRF enables Aviation < 2g
    Serial1.write("$PCAS11,3*1E\r\n");
    return result;
}

bool radio_init() {
  fallback_clock.begin();

  rtc_clock.begin(Wire1);
  
#ifdef SX126X_DIO3_TCXO_VOLTAGE
  float tcxo = SX126X_DIO3_TCXO_VOLTAGE;
#else
  float tcxo = 1.6f;
#endif

#if defined(P_LORA_SCLK)
  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
#endif
  int status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, LORA_TX_POWER, 8, tcxo);
  if (status != RADIOLIB_ERR_NONE) {
    Serial.print("ERROR: radio init failed: ");
    Serial.println(status);
    return false;  // fail
  }

  radio.setCRC(1);
  
  return true;  // success
}

uint32_t radio_get_rng_seed() {
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);
}

void radio_set_tx_power(uint8_t dbm) {
  radio.setOutputPower(dbm);
}

void TbeamSupSensorManager::start_gps()
{
  gps_active = true;
  pinMode(P_GPS_WAKE, OUTPUT);
  digitalWrite(P_GPS_WAKE, HIGH);
}

void TbeamSupSensorManager::sleep_gps() {
  gps_active = false;
  pinMode(P_GPS_WAKE, OUTPUT);
  digitalWrite(P_GPS_WAKE, LOW);
}

bool TbeamSupSensorManager::begin() {
  // init GPS port

  Serial1.begin(GPS_BAUD_RATE, SERIAL_8N1, P_GPS_RX, P_GPS_TX);

  bool result = false;
    for ( int i = 0; i < 3; ++i) {
      result = l76kProbe();
      if (result) {
        gps_active = true;
        return result;
      }
    }
  return result;
}

bool TbeamSupSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if (requester_permissions & TELEM_PERM_LOCATION) {   // does requester have permission?
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }
  return true;
}

void TbeamSupSensorManager::loop() {
  static long next_gps_update = 0;

  _nmea->loop();

  if (millis() > next_gps_update) {
    if (_nmea->isValid()) {
      node_lat = ((double)_nmea->getLatitude())/1000000.;
      node_lon = ((double)_nmea->getLongitude())/1000000.;
      node_altitude = ((double)_nmea->getAltitude()) / 1000.0;
      //Serial.printf("lat %f lon %f\r\n", _lat, _lon);
    }
    next_gps_update = millis() + 1000;
  }
}

int TbeamSupSensorManager::getNumSettings() const { return 1; }  // just one supported: "gps" (power switch)

const char* TbeamSupSensorManager::getSettingName(int i) const {
  return i == 0 ? "gps" : NULL;
}

const char* TbeamSupSensorManager::getSettingValue(int i) const {
  if (i == 0) {
    return gps_active ? "1" : "0";
  }
  return NULL;
}

bool TbeamSupSensorManager::setSettingValue(const char* name, const char* value) {
  if (strcmp(name, "gps") == 0) {
    if (strcmp(value, "0") == 0) {
      sleep_gps();
    } else {
      start_gps();
    }
    return true;
  }
  return false;  // not supported
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
