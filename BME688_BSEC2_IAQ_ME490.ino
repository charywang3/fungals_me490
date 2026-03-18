#define BLYNK_TEMPLATE_ID   "TMPL2YmFQAaD6"            // your template id (from Blynk Console)
#define BLYNK_TEMPLATE_NAME "BME688"     // your template name
#define BLYNK_AUTH_TOKEN    "i7RwJMdXuWfASvXzbX2Yt23jX1B4FHEu"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

const char* WIFI_SSID = "DukeVisitor";
const char* WIFI_PASS = "";

// Virtual pin mapping
const uint8_t VP_TEMP = V0;   // temperature -> V0
const uint8_t VP_HUM  = V1;   // humidity    -> V1
const uint8_t VP_IAQ  = V2;   // IAQ index   -> V2
const uint8_t VP_BVOC = V3;   // breath VOC equivalent -> V3
const uint8_t VP_C1P  = V10;  // probability of class 1 -> V10
const uint8_t VP_C2P  = V11;  // probability of class 2 -> V11
const uint8_t VP_C3P  = V12;  // probability of class 3 -> V12
const uint8_t VP_C4P  = V13;  // probability of class 4 -> V13
const uint8_t VP_PRED = V14;  // predicted class -> V14

int currentLabel = 1;  
// 1 = clean
// 2 = moldy
// 3 = cleaning product
// 4 = cooking
// change label number depending on what data we are collecting

#include <bsec2.h>

/* Macros used */
#define PANIC_LED   LED_BUILTIN
#define ERROR_DUR   1000

#define SAMPLE_RATE		BSEC_SAMPLE_RATE_LP

/* Helper functions declarations */
/**
 * @brief : This function toggles the led when a fault was detected
 */
void errLeds(void);

/**
 * @brief : This function checks the BSEC status, prints the respective error code. Halts in case of error
 * @param[in] bsec  : Bsec2 class object
 */
void checkBsecStatus(Bsec2 bsec);

/**
 * @brief : This function is called by the BSEC library when a new output is available
 * @param[in] input     : BME68X sensor data before processing
 * @param[in] outputs   : Processed BSEC BSEC output data
 * @param[in] bsec      : Instance of BSEC2 calling the callback
 */
void newDataCallback(const bme68xData data, const bsecOutputs outputs, Bsec2 bsec);

/* Create an object of the class Bsec2 */
Bsec2 envSensor;

/* Entry point for the example */
void setup(void)
{
    /* Desired subscription list of BSEC2 outputs */
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

    /* Initialize the communication interfaces */
    
    Serial.begin(115200);
    Wire.begin(); 
    pinMode(PANIC_LED, OUTPUT);


    /* Initialize the library and interfaces */
    if (!envSensor.begin(BME68X_I2C_ADDR_HIGH, Wire))
    {
        checkBsecStatus(envSensor);
    }
	
	/*
	 *	The default offset provided has been determined by testing the sensor in LP and ULP mode on application board 3.0
	 *	Please update the offset value after testing this on your product 
	 */
	if (SAMPLE_RATE == BSEC_SAMPLE_RATE_ULP)
	{
		envSensor.setTemperatureOffset(TEMP_OFFSET_ULP);
	}
	else if (SAMPLE_RATE == BSEC_SAMPLE_RATE_LP)
	{
		envSensor.setTemperatureOffset(TEMP_OFFSET_LP);
	}

    /* Subsribe to the desired BSEC2 outputs */
    if (!envSensor.updateSubscription(sensorList, ARRAY_LEN(sensorList), SAMPLE_RATE))
    {
        checkBsecStatus(envSensor);
    }

    /* Whenever new data is available call the newDataCallback function */
    envSensor.attachCallback(newDataCallback);

    Serial.println("BSEC library version " + \
            String(envSensor.version.major) + "." \
            + String(envSensor.version.minor) + "." \
            + String(envSensor.version.major_bugfix) + "." \
            + String(envSensor.version.minor_bugfix));

    
  Serial.print("Connecting to WiFi...");
	WiFi.begin(WIFI_SSID, WIFI_PASS);
	unsigned long start = millis();
	while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
		Serial.print(".");
		delay(250);
	}
	Serial.println();
	if (WiFi.status() == WL_CONNECTED) {
		Serial.println("WiFi connected.");
    Serial.println("time_ms,temp_c,humidity,gas_raw,bvoc,label"); //csv header
	} else {
		Serial.println("WiFi connect failed (maybe captive portal).");
	}
	// Start Blynk (works well for simple home networks)
	//Blynk.begin(BLYNK_AUTH_TOKEN, WIFI_SSID, WIFI_PASS);
}

/* Function that is looped forever */
void loop(void)
{
    Blynk.run();
    /* Call the run function often so that the library can 
     * check if it is time to read new data from the sensor  
     * and process it.
     */
    if (!envSensor.run())
    {
        checkBsecStatus(envSensor);
    }
}

void errLeds(void)
{
    while(1)
    {
        digitalWrite(PANIC_LED, HIGH);
        delay(ERROR_DUR);
        digitalWrite(PANIC_LED, LOW);
        delay(ERROR_DUR);
    }
}

void newDataCallback(const bme68xData data, const bsecOutputs outputs, Bsec2 bsec)
{
  if (!outputs.nOutputs) return;

  // Capture values from this callback batch
  float tempC = NAN, rh = NAN, iaq = NAN, bvoc = NAN;
  float gasRaw = NAN, gasComp = NAN;
  int iaqAcc = -1;
  int runIn = -1, stab = -1;

  for (uint8_t i = 0; i < outputs.nOutputs; i++)
  {
    const bsecData out = outputs.output[i];

    switch (out.sensor_id)
    {
      case BSEC_OUTPUT_IAQ:
        iaq = out.signal;
        iaqAcc = (int)out.accuracy;
        break;

      case BSEC_OUTPUT_RAW_GAS:
        gasRaw = out.signal;
        break;

      case BSEC_OUTPUT_COMPENSATED_GAS:
        gasComp = out.signal;
        break;

      case BSEC_OUTPUT_STABILIZATION_STATUS:
        stab = (int)out.signal;        // typically 0/1
        break;

      case BSEC_OUTPUT_RUN_IN_STATUS:
        runIn = (int)out.signal;       // typically 0/1
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

  static uint32_t lastLog = 0;
  uint32_t now = millis();

  if (now - lastLog >= 3000) {   // log every 3 seconds
  lastLog = now;

  //outputs into terminal in csv file format for better data logging
  if (!isnan(tempC) && !isnan(rh) && !isnan(gasRaw)) {
    Serial.print(now);
    Serial.print(",");
    Serial.print(tempC);
    Serial.print(",");
    Serial.print(rh);
    Serial.print(",");
    Serial.print(log(gasRaw));
    Serial.print(",");
    Serial.print(bvoc);
    Serial.print(",");
    Serial.println(currentLabel);
  }
  }
  // Publish to Blynk at most 1 Hz
  static uint32_t lastPub = 0;
  if (now - lastPub >= 1000) {
    lastPub = now;

    // Allow sending temperature/humidity immediately.
    // Optionally gate IAQ/BVOC on accuracy instead.
    if (!isnan(tempC)) Blynk.virtualWrite(VP_TEMP, tempC);
    if (!isnan(rh))    Blynk.virtualWrite(VP_HUM, rh);

    // Only send IAQ/BVOC once the algorithm has some confidence
    if (iaqAcc > 0 && !isnan(iaq))  Blynk.virtualWrite(VP_IAQ, iaq);
    if (iaqAcc > 0 && !isnan(bvoc)) Blynk.virtualWrite(VP_BVOC, bvoc);
  }
}

void checkBsecStatus(Bsec2 bsec)
{
    if (bsec.status < BSEC_OK)
    {
        Serial.println("BSEC error code : " + String(bsec.status));
        errLeds(); /* Halt in case of failure */
    }
    else if (bsec.status > BSEC_OK)
    {
        Serial.println("BSEC warning code : " + String(bsec.status));
    }

    if (bsec.sensor.status < BME68X_OK)
    {
        Serial.println("BME68X error code : " + String(bsec.sensor.status));
        errLeds(); /* Halt in case of failure */
    }
    else if (bsec.sensor.status > BME68X_OK)
    {
        Serial.println("BME68X warning code : " + String(bsec.sensor.status));
    }
}
