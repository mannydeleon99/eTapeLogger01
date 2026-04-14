//Project Name: eTape Code
//Author: Luca Cecere
//Credit to CSU Agricultural Water Quality Program Team

#include "Particle.h"

#ifndef SYSTEM_VERSION_v620
SYSTEM_THREAD(ENABLED); 
#endif

SerialLogHandler logHandler;

// Sleep time between cycles (measurement period)
const std::chrono::minutes publishPeriod = 5min;  // not sure why this was orginally 1min. Will cause battery issues  

// Time to turn sleep OFF off before taking a measurement
const std::chrono::seconds sensorWarmup = 10s;

// Time until sleep is turned ON after taking a measurement
const std::chrono::seconds postDelay = 10s;

// The event name to publish with
const char *eventName = "eTape Log 1";

const pin_t ETAPE_PIN = A0;     // eTape on A0
const float VREF      = 3.3f;   // Boron ADC full-scale
const int   NUMSAMPLES = 20;    // Amount of samples taken for smoothing


const int   batchSize = 6;      // Amount of readings in a batch  (6 * 5min = 30 mins), will publish ever 30 min 
const int   maxBufferSize = 18; // Maximum offline storage capacity (18 * 5min = 1.5 hours)

// Particle variable (open you particle app)
double v_depth = 0.0;
double v_sensorV = 0.0;
double v_battSoc = 0.0;
double v_battV = 0.0;
int v_cellSig = 0;


//Declaration of variables
FuelGauge fuel;

int   v = 0;

struct Reading
{                 
    float depth;
    float volts;
    float batterySoc;
    float batteryVolts;
    float cellStrength;
    unsigned long timestamp;
};

Reading currentReading;
retained Reading readings[batchSize]; 
retained int readingCount = 0;

//declaration of functions
void takeMeasurement();         // Take a reading
void publishBatch();            // Publish readings to Google SHEETS
void goToSleep();               // Go into sleep mode
void battSettings();            // Configure PMIC
void storeReading();            // Batch load readings

void setup() {
    
//begin serial connection
    Serial.begin(9600);
    waitFor(Serial.isConnected, 3000);
    Log.info("Starting eTape logger");
    battSettings();

// Register variables to the Particle Cloud (Max 12 chars per name)
    Particle.variable("Depth", v_depth);
    Particle.variable("SensorVolts", v_sensorV);
    Particle.variable("BattSoC", v_battSoc);
    Particle.variable("BattVolts", v_battV);
    Particle.variable("CellSignal", v_cellSig);
    Particle.variable("QCount", readingCount); // Exposing the retained count
}

void loop() {
    
    // 1) Warmup period
    delay((uint32_t)(sensorWarmup / 1ms));              //Turn sleep OFF

    // 2) Take a measurement
    takeMeasurement();    
    
    // 3) Store reading internally
    storeReading();

    // 4) Post-measurement delay
    delay((uint32_t)(postDelay / 1ms));

    // 5) Publish batch measurment
    if (readingCount >= batchSize){
        publishBatch();
    }
    // 6) Go to sleep
    goToSleep();
}

void takeMeasurement() {
    
    long sum = 0;

    for (int i = 0; i < NUMSAMPLES; i++) {
        v = analogRead(ETAPE_PIN);
        sum += v;
        delay(50);
    }

    float smoothed = (float)sum / (float)NUMSAMPLES;

    currentReading.volts = (smoothed / 4095.0f) * VREF;

    if (currentReading.volts < 0.05f) {
        currentReading.depth = 0.0f;
    }
    else if (currentReading.volts >= 0.05f && currentReading.volts < 0.1f) {
        currentReading.depth = 0.762f;   // small depth placeholder
    }
    else {
        currentReading.depth = (currentReading.volts * 19.8f - 0.2271f) - 2.54f;
    }

    // Update battery State of Charge(SoC)
    currentReading.batterySoc = System.batteryCharge();
    currentReading.batteryVolts = fuel.getVCell();

    CellularSignal sig = Cellular.RSSI();
    currentReading.cellStrength = sig.getStrength();

    currentReading.timestamp = Time.now();

    // Particle varibles
    v_depth = (double)currentReading.depth;
    v_sensorV = (double)currentReading.volts;
    v_battSoc = (double)currentReading.batterySoc;
    v_battV = (double)currentReading.batteryVolts;
    v_cellSig = currentReading.cellStrength;
    
}

void storeReading() {

    if (readingCount < maxBufferSize) {
        readings[readingCount] = currentReading;
        readingCount++;

    }
}
// prepares array and publishes it
void publishBatch() {
   
    if (!Particle.connected()) {
        Particle.connect();
        // Wait up to 3 minutes for connection
        waitFor(Particle.connected, 3 * 60 * 1000);
    }

    if (Particle.connected()) {
        
        char payload[1024];   // Increased payload size to handle up to 18 readings safely
        payload[0] = '\0';

        strcat(payload, "[");
        
        for (int i = 0; i < readingCount; i++){
            char temp[64];

            snprintf(temp,sizeof(temp),
            "[%lu,%.2f,%.2f,%.2f,%.2f,%.2f]",
            readings[i].timestamp,
            readings[i].depth,
            readings[i].batteryVolts,
            readings[i].cellStrength,
            readings[i].volts,
            readings[i].batterySoc
        );

        strcat(payload, temp);

        if (i < (readingCount - 1)){
            strcat(payload, ",");
        }
        }

        strcat(payload, "]");

        Particle.publish(eventName, payload, PRIVATE);

        // Only clear the retained array if the publish actually succeeds
        bool success = Particle.publish(eventName, payload, PRIVATE);
        
    if(success){
         readingCount = 0;
      }
    }
}

void goToSleep() {

    SystemSleepConfiguration config;
    config.mode(SystemSleepMode::ULTRA_LOW_POWER)
      .duration(publishPeriod)
      .network(NETWORK_INTERFACE_CELLULAR, SystemSleepNetworkFlag::INACTIVE_STANDBY);

    System.sleep(config);
    // On wake cycle again
}

void battSettings() {
  
  SystemPowerConfiguration conf; 
  conf.powerSourceMaxCurrent(900)    // 5W / 5V = 1000mA. 900mA is the closest PMIC register setting.
      .powerSourceMinVoltage(3880)  
      .batteryChargeCurrent(500)
      .batteryChargeVoltage(4110);    // you can increase to 4208 which is the max, for 4.2v 
      
  System.setPowerConfiguration(conf);
}

