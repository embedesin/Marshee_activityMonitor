#include <Wire.h>
#include <QMI8658.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <esp_sleep.h>
#include <LittleFS.h>
#include <TimeLib.h> // Include TimeLib for time functions

#define MOTION_PIN 34  // Adjust this pin number based on your wiring

QMI8658 qmi8658;

// TimeManager class for handling NTP synchronization
class TimeManager {
public:
    TimeManager() : ntpClient(new NTPClient(udp, "pool.ntp.org", 3600, 60000)) {
        WiFi.begin("SSID", "PASSWORD");
        ntpClient->begin();
    }

    void synchronizeTime() {
        if (WiFi.status() == WL_CONNECTED) {
            ntpClient->update();
            setTime(ntpClient->getEpochTime()); // Set the current time
            Serial.print("Time synchronized: ");
            printCurrentTime();
        } else {
            Serial.println("WiFi not connected. Using RTC time.");
        }
    }

    unsigned long getEpochTime() {
        return ntpClient->getEpochTime();
    }

    void printCurrentTime() {
        Serial.printf("Current time: %02d:%02d:%02d\n", hour(), minute(), second());
    }

private:
    WiFiUDP udp;
    NTPClient* ntpClient;
};

// Structure to hold activity data
struct ActivityData {
    uint32_t restingDuration = 0;
    uint32_t walkingDuration = 0;
    uint32_t runningDuration = 0;
    uint32_t playingDuration = 0;
};

TimeManager timeManager;
ActivityData activityData;

String classifyActivity(float ax, float ay, float az);
void logActivityData();
void saveToFile();
void IRAM_ATTR onMotionDetected(); // Interrupt handler

// Flag to indicate motion detection
volatile bool motionDetected = false;

void setup() {
    Serial.begin(115200);
    Wire.begin();

    // Initialize LittleFS
    if (!LittleFS.begin()) {
        Serial.println("Failed to mount LittleFS");
        return;
    }

    // Initialize QMI8658 sensor
    if (!qmi8658.begin()) {
        Serial.println("QMI8658 not found!");
        while (1);
    }
    Serial.println("QMI8658 initialized successfully.");

    // Set up motion detection pin
    pinMode(MOTION_PIN, INPUT_PULLUP); // Assuming active low motion detection
    attachInterrupt(digitalPinToInterrupt(MOTION_PIN), onMotionDetected, FALLING);

    // Synchronize time if Wi-Fi is available
    timeManager.synchronizeTime();
    timeManager.printCurrentTime();

    // Log activity data before going to sleep
    logActivityData();
    
    // Set deep sleep timer for 10 minutes
    esp_sleep_enable_timer_wakeup(10 * 60 * 1000000); // Sleep for 10 minutes
    esp_deep_sleep_start();
}

void loop() {
    // This will never be reached due to deep sleep
}

// Activity classification function
String classifyActivity(float ax, float ay, float az) {
    float magnitude = sqrt(ax * ax + ay * ay + az * az);

    if (magnitude < 1.0) {
        return "Resting";
    } else if (magnitude < 2.5) {
        return "Walking";
    } else if (magnitude < 4.0) {
        return "Running";
    } else {
        return "Playing";
    }
}

// Log activity data to file
void logActivityData() {
    if (motionDetected) {
        // Reset the motion detected flag
        motionDetected = false;

        // Read sensor data and classify activity
        float accel[3], gyro[3];
        qmi8658.read_sensor_data(accel, gyro); // Pass the arrays to read_sensor_data

        // Classify activity based on accelerometer data
        String activity = classifyActivity(accel[0], accel[1], accel[2]);
        Serial.print("Detected activity: ");
        Serial.println(activity);

        // Increment activity duration based on detected activity
        if (activity == "Resting") {
            activityData.restingDuration++;
        } else if (activity == "Walking") {
            activityData.walkingDuration++;
        } else if (activity == "Running") {
            activityData.runningDuration++;
        } else if (activity == "Playing") {
            activityData.playingDuration++;
        }
    }

    // Save activity data to file
    saveToFile();
}

// Save the activity data to a file
void saveToFile() {
    String filename = "/activity_log.txt";
    File file = LittleFS.open(filename, "a");
    
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }

    // Get the epoch time from the NTP client
    unsigned long epochTime = timeManager.getEpochTime();

    // Create a log entry
    String logEntry = String(epochTime) + "," + // Use the epoch time
                      String(activityData.restingDuration) + "," +
                      String(activityData.walkingDuration) + "," +
                      String(activityData.runningDuration) + "," +
                      String(activityData.playingDuration) + "\n";

    file.print(logEntry);
    file.close();
    Serial.println("Activity data logged: " + logEntry);
}

// Interrupt handler for motion detection
void IRAM_ATTR onMotionDetected() {
    motionDetected = true; // Set the flag indicating motion has been detected
}
