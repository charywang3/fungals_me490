/**
 * BME688 + BSEC2 IAQ logger with state save/restore
 * Logs CSV for ML training:
 * time_ms,temp_c,humidity,gas_raw,bvoc,iaq,label
 *
 * Labels:
 * 1 = clean
 * 2 = cleaning
 * 3 = moldy
 */

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
#include <EEPROM.h>
#define USE_EEPROM
#endif

#include <bsec2.h>

// ---------------- USER SETTINGS ----------------
int currentLabel = 3;   // 1=clean, 2=cleaning, 3=moldy
#define SAMPLE_RATE BSEC_SAMPLE_RATE_LP
#define PANIC_LED LED_BUILTIN
#define ERROR_DUR 1000
#define STATE_SAVE_PERIOD UINT32_C(360 * 60 * 1000)   // every 6 hours

// IMPORTANT:
// Replace this include path with the IAQ config file path from the Bosch
// IAQ example that compiles on your machine.
// Example folder names vary by library version.
const uint8_t bsec_config[] = {
  #include "config\bme680\bme680_iaq_18v_3s_4d\bsec_iaq.txt"
};
// ------------------------------------------------

Bsec2 envSensor;

#ifdef USE_EEPROM
static uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE];
#endif

float gTemp = NAN;
float gHum  = NAN;
float gGas  = NAN;
float gBvoc = NAN;
float gIaq  = NAN;
int   gIaqAcc = -1;
int gStab = 0;
int gRunIn = 0;

// ---------- function declarations ----------
void errLeds(void);
void checkBsecStatus(Bsec2 bsec);
void updateBsecState(Bsec2 bsec);
void newDataCallback(const bme68xData data, const bsecOutputs outputs, Bsec2 bsec);
bool loadState(Bsec2 bsec);
bool saveState(Bsec2 bsec);
// ------------------------------------------

void setup(void)
{
    bsecSensor sensorList[] = {
        BSEC_OUTPUT_IAQ,
        BSEC_OUTPUT_RAW_GAS,
        BSEC_OUTPUT_STABILIZATION_STATUS,
        BSEC_OUTPUT_RUN_IN_STATUS,
        BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
        BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
        BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
        BSEC_OUTPUT_COMPENSATED_GAS
    };

    Serial.begin(115200);
#ifdef USE_EEPROM
    EEPROM.begin(BSEC_MAX_STATE_BLOB_SIZE + 1);
#endif
    Wire.begin();
    pinMode(PANIC_LED, OUTPUT);

    while (!Serial) delay(10);

    if (!envSensor.begin(BME68X_I2C_ADDR_HIGH, Wire))
    {
        checkBsecStatus(envSensor);
    }

    if (!envSensor.setConfig(bsec_config))
    {
        checkBsecStatus(envSensor);
    }

    if (SAMPLE_RATE == BSEC_SAMPLE_RATE_ULP)
    {
        envSensor.setTemperatureOffset(TEMP_OFFSET_ULP);
    }
    else if (SAMPLE_RATE == BSEC_SAMPLE_RATE_LP)
    {
        envSensor.setTemperatureOffset(TEMP_OFFSET_LP);
    }

    if (!loadState(envSensor))
    {
        checkBsecStatus(envSensor);
    }

    if (!envSensor.updateSubscription(sensorList, ARRAY_LEN(sensorList), SAMPLE_RATE))
    {
        checkBsecStatus(envSensor);
    }

    envSensor.attachCallback(newDataCallback);

    Serial.println();
    Serial.println("BSEC library version " +
                   String(envSensor.version.major) + "." +
                   String(envSensor.version.minor) + "." +
                   String(envSensor.version.major_bugfix) + "." +
                   String(envSensor.version.minor_bugfix));

    Serial.println("time_ms,temp_c,humidity,gas_raw,bvoc,iaq,label");
}

void loop(void)
{
    if (!envSensor.run())
    {
        checkBsecStatus(envSensor);
    }

    static uint32_t lastLog = 0;
    uint32_t now = millis();

    // Log every 20 seconds
    if (now - lastLog >= 20000)
    {
        lastLog = now;

        if (gStab == 1 && !isnan(gTemp) && !isnan(gHum) && !isnan(gGas))
        {
            Serial.print(now);
            Serial.print(",");
            Serial.print(gTemp);
            Serial.print(",");
            Serial.print(gHum);
            Serial.print(",");
            Serial.print(log(gGas));   // already log-transformed for CSV
            Serial.print(",");
            Serial.print(gBvoc);
            Serial.print(",");
            Serial.print(gIaq);
            Serial.print(",");
            Serial.println(currentLabel);
        }
    }
}

void newDataCallback(const bme68xData data, const bsecOutputs outputs, Bsec2 bsec)
{
    if (!outputs.nOutputs) return;

    float tempC = NAN, rh = NAN, iaq = NAN, bvoc = NAN;
    float gasRaw = NAN;
    int iaqAcc = -1;
    int stab = -1;
    int runIn = -1;

    for (uint8_t i = 0; i < outputs.nOutputs; i++)
    {
        const bsecData out = outputs.output[i];

        switch (out.sensor_id)
        {
            case BSEC_OUTPUT_STABILIZATION_STATUS:
                stab = (int)out.signal;
                break;

            case BSEC_OUTPUT_RUN_IN_STATUS:
                runIn = (int)out.signal;
                break;

            case BSEC_OUTPUT_IAQ:
                iaq = out.signal;
                iaqAcc = (int)out.accuracy;
                break;

            case BSEC_OUTPUT_RAW_GAS:
                gasRaw = out.signal;
                break;

            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
                tempC = out.signal;
                break;

            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
                rh = out.signal;
                break;

            case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
                bvoc = out.signal;
                break;

            default:
                break;
        }
    }

    if (!isnan(tempC))  gTemp = tempC;
    if (!isnan(rh))     gHum = rh;
    if (!isnan(gasRaw)) gGas = gasRaw;
    if (!isnan(bvoc))   gBvoc = bvoc;
    if (!isnan(iaq))    gIaq = iaq;
    gIaqAcc = iaqAcc;

    if (stab != -1)  gStab = stab;
    if (runIn != -1) gRunIn = runIn;

    updateBsecState(envSensor);
}

void updateBsecState(Bsec2 bsec)
{
    static uint16_t stateUpdateCounter = 0;
    bool update = false;

    if (!stateUpdateCounter || (stateUpdateCounter * STATE_SAVE_PERIOD) < millis())
    {
        update = true;
        stateUpdateCounter++;
    }

    if (update && !saveState(bsec))
    {
        checkBsecStatus(bsec);
    }
}

bool loadState(Bsec2 bsec)
{
#ifdef USE_EEPROM
    if (EEPROM.read(0) == BSEC_MAX_STATE_BLOB_SIZE)
    {
        for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++)
        {
            bsecState[i] = EEPROM.read(i + 1);
        }

        if (!bsec.setState(bsecState))
            return false;
    }
    else
    {
        for (uint8_t i = 0; i <= BSEC_MAX_STATE_BLOB_SIZE; i++)
        {
            EEPROM.write(i, 0);
        }
        EEPROM.commit();
    }
#endif
    return true;
}

bool saveState(Bsec2 bsec)
{
#ifdef USE_EEPROM
    if (!bsec.getState(bsecState))
        return false;

    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++)
    {
        EEPROM.write(i + 1, bsecState[i]);
    }

    EEPROM.write(0, BSEC_MAX_STATE_BLOB_SIZE);
    EEPROM.commit();
#endif
    return true;
}

void checkBsecStatus(Bsec2 bsec)
{
    if (bsec.status < BSEC_OK)
    {
        Serial.println("BSEC error code : " + String(bsec.status));
        errLeds();
    }
    else if (bsec.status > BSEC_OK)
    {
        Serial.println("BSEC warning code : " + String(bsec.status));
    }

    if (bsec.sensor.status < BME68X_OK)
    {
        Serial.println("BME68X error code : " + String(bsec.sensor.status));
        errLeds();
    }
    else if (bsec.sensor.status > BME68X_OK)
    {
        Serial.println("BME68X warning code : " + String(bsec.sensor.status));
    }
}

void errLeds(void)
{
    while (1)
    {
        digitalWrite(PANIC_LED, HIGH);
        delay(ERROR_DUR);
        digitalWrite(PANIC_LED, LOW);
        delay(ERROR_DUR);
    }
}