/*

MHZ19 MODULE

Copyright (C) 2016-2017 by Xose Pérez <xose dot perez at gmail dot com>

*/

#if ENABLE_MHZ19

#include <SoftwareSerial.h>
#include "mhz19.h"

SoftwareSerial *SwSerial;

unsigned int _mhz19Ppm = 0;
unsigned int _mhz19Temperature = 0;

// -----------------------------------------------------------------------------
// Values
// -----------------------------------------------------------------------------

static bool exchange_command(uint8_t cmd, uint8_t data[], int timeout)
{
    // create command buffer
    uint8_t buf[9];
    int len = prepare_tx(cmd, data, buf, sizeof(buf));

    // send the command
    SwSerial->write(buf, len);

    // wait for response
    long start = millis();
    while ((millis() - start) < timeout) {
        if (SwSerial->available() > 0) {
            uint8_t b = SwSerial->read();
            if (process_rx(b, cmd, data)) {
                return true;
            }
        }
    }

    return false;
}

static bool read_temp_co2(int *co2, int *temp)
{
    uint8_t data[] = {0, 0, 0, 0, 0, 0};
    bool result = exchange_command(0x86, data, 3000);
    if (result) {
        *co2 = (data[0] << 8) + data[1];
        *temp = data[2] - 40;
    }
    return result;
}

unsigned int getMZH19Temperature() {
    return _mhz19Temperature;
}

unsigned int getMZH19Ppm() {
    return _mhz19Ppm;
}

void mhz19Setup() {
    SwSerial = new SoftwareSerial(MHZ19_RX_PIN, MHZ19_TX_PIN);
    SwSerial->begin(9600);
    
    apiRegister("/api/temperature", "temperature", [](char * buffer, size_t len) {
        snprintf(buffer, len, "%d", _mhz19Temperature);
    });
    apiRegister("/api/ppm", "ppm", [](char * buffer, size_t len) {
        snprintf(buffer, len, "%d", _mhz19Ppm);
    });
}

void mhz19Loop() {

    // Check if we should read new data
    static unsigned long last_update = 0;
    if ((millis() - last_update > MHZ19_UPDATE_INTERVAL) || (last_update == 0)) {
        last_update = millis();

        unsigned char tmpUnits = getSetting("tmpUnits", TMP_UNITS).toInt();

        // Read sensor data
        int co2, temp;
        if (!(read_temp_co2(&co2, &temp))) {
            DEBUG_MSG_P(PSTR("[MHZ19] Error reading sensor\n"));
            return;
        }
        
        // Check if readings are valid
        if (isnan(co2) || isnan(temp)) {
            DEBUG_MSG_P(PSTR("[MHZ19] Error reading sensor\n"));
        } else {

            _mhz19Temperature = temp;
            _mhz19Ppm = co2;

            char temperature[6];
            char ppm[6];
            itoa((unsigned int) temp, temperature, 10);
            itoa((unsigned int) co2, ppm, 10);

            DEBUG_MSG_P(PSTR("[MHZ19] Temperature: %s%s\n"), temperature, (tmpUnits == TMP_CELSIUS) ? "ºC" : "ºF");
            DEBUG_MSG_P(PSTR("[MHZ19] PPM: %s\n"), ppm);

            // Send MQTT messages
            mqttSend(getSetting("mhz19TmpTopic", MHZ19_TEMPERATURE_TOPIC).c_str(), temperature);
            mqttSend(getSetting("mhz19PpmTopic", MHZ19_PPM_TOPIC).c_str(), ppm);

            // Update websocket clients
            char buffer[100];
            sprintf_P(buffer, PSTR("{\"mhz19Visible\": 1, \"mhz19Tmp\": %s, \"mhz19Ppm\": %s, \"tmpUnits\": %d}"), temperature, ppm, tmpUnits);
            wsSend(buffer);
        }
    }
}

#endif
