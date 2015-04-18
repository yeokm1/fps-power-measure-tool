#include <Wire.h>
#include <SPI.h>
#include <Adb.h>
#include <StandardCplusplus.h>
#include <Stack>
#include <Adafruit_INA219.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

using namespace std;

#define FPS_COMMAND "dumpsys SurfaceFlinger --latency SurfaceView && echo $\n"
#define FPS_RESULT_LINE_LENGTH 100
#define TIME_INTERVAL_FPS_SECOND 1000
#define TIME_SHELL_COMMAND_ISSUE 1000

#define MAX_FPS 60
#define NO_FPS_CALCULATED -1

#define POLL_RATE 500
#define SCREEN_RESET_PIN 4 //Digital 4
#define DISPLAY_CONTRAST true
#define PAST_POWER_TO_AVERAGE 10

#define MEASUREMENT_RATE 1000

#define PIN_BUTTON 7
#define DEBOUNCE_TIME 200

Adafruit_INA219 ina219;
Adafruit_SSD1306 display(SCREEN_RESET_PIN);

//Smoothing algorithm http://arduino.cc/en/Tutorial/Smoothing
float powerValues[PAST_POWER_TO_AVERAGE];      // the readings from the analog input
int powerValuesIndex = 0;                  // the index of the current reading
float powerValuesTotal = 0;                  // the running total
float powerValuesAverage = 0;                // the average

bool isMeasuring = false;

long lastPressedTime = millis();

double totalEnergyUsed = 0;
long totalFPSMeasured = 0;
int measurementsTaken = 0;
long timeLastMeasurementTaken = millis();

Connection * shell;

long shellLastCallTime = millis();

char resultLine[FPS_RESULT_LINE_LENGTH];
int currentLetter = 0;
stack<long> lastValues;
bool didPreviousFPSCommandSucceed = false;

int currentFPS = 0;

void setup() {
  Serial.begin(115200);
  ina219.begin();

  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // Initialise the ADB subsystem.
  ADB::init();
  // Open an ADB stream to the phone's shell. Auto-reconnect
  shell = ADB::addConnection("shell:", true, adbEventHandler);

  // initialize with the OLED with I2C addr 0x3D (for the 128x64)
  display.begin(SSD1306_SWITCHCAPVCC, 0x3D);

  display.dim(DISPLAY_CONTRAST);

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.display();

}

void loop() {
  float outputVolt = ina219.getBusVoltage_V();
  float outputAmp = ina219.getCurrent_mA() / 1000;

  float outputWattRaw = outputVolt * outputAmp;


  powerValuesTotal = powerValuesTotal - powerValues[powerValuesIndex];
  powerValues[powerValuesIndex] = outputWattRaw;
  powerValuesTotal = powerValuesTotal + powerValues[powerValuesIndex];
  powerValuesIndex++;

  // if we're at the end of the array...
  if (powerValuesIndex >= PAST_POWER_TO_AVERAGE) {
    powerValuesIndex = 0;
  }

  powerValuesAverage = powerValuesTotal / PAST_POWER_TO_AVERAGE;


  long currentTime = millis();
  // Poll the ADB subsystem.
  if (shell->status == ADB_OPEN) {


    if ((currentTime - shellLastCallTime) > TIME_SHELL_COMMAND_ISSUE) {
      shellLastCallTime = currentTime;
      ADB::writeString(shell, FPS_COMMAND);
    }
  }

  ADB::poll();


  int buttonStatus = digitalRead(PIN_BUTTON);

  if (buttonStatus == LOW && ((currentTime - lastPressedTime) > DEBOUNCE_TIME)) {
    lastPressedTime = currentTime;

    if (isMeasuring) {
      isMeasuring = false;
    } else {
      isMeasuring = true;

      measurementsTaken = 0;;
      totalEnergyUsed = 0;
      totalFPSMeasured = 0;
    }



  }



  if (isMeasuring) {
    if ((currentTime - timeLastMeasurementTaken) > MEASUREMENT_RATE) {

      if (currentFPS != NO_FPS_CALCULATED) {
        timeLastMeasurementTaken = currentTime;

        measurementsTaken++;
        totalEnergyUsed += powerValuesAverage;
        totalFPSMeasured += currentFPS;
      }

    }

  }



  display.clearDisplay();
  display.setCursor(0, 0);
  
  display.print("FPS:      ");
  display.println(currentFPS);

  display.print("Voltage:  ");
  display.print(outputVolt, 2);
  display.println("V");

  display.print("Current:  ");
  display.print(outputAmp, 2);
  display.println("A");

  display.print("Power:    ");
  display.print(powerValuesAverage, 2);
  display.println("W");


  display.println();

  int averageFPS = 0;

  if (measurementsTaken != 0) {
    averageFPS = totalFPSMeasured / measurementsTaken;
  }

  if (isMeasuring) {

    int mod750 = currentTime % 1500;

    if (mod750 < 750) {
      display.println("Measuring");
    } else {
      display.println("Measuring...");
    }

  } else {
    display.println("Idle");
  }

  display.print("Avg FPS: ");
  display.println(averageFPS);

  display.print("Energy:  ");
  display.print(totalEnergyUsed);
  display.println("J");




  display.display();

}

void adbEventHandler(Connection * connection, adb_eventType event, uint16_t length, uint8_t * data)
{
  if (event == ADB_CONNECTION_RECEIVE) {
    for (int i = 0; i < length; i++) {

      char recvData = data[i];

      if (recvData == '\n') {

        //End this line in the array
        resultLine[currentLetter] = '\0';
        currentLetter = 0;

        //Get to third number
        strtok(resultLine, "\t");
        strtok(NULL, "\t");
        char * finishFrameTime = strtok(NULL, "\t");

        if (finishFrameTime != NULL) {
          int frameStrLen = strlen(finishFrameTime);

          //Chop last 6 characters away to keep within 32bits so atol can work
          finishFrameTime[frameStrLen - 7] = '\0';
          long finishFrameTimeLn = atol(finishFrameTime);
          lastValues.push(finishFrameTimeLn);

        }


      } else if (recvData == '$') {
        //Reached end of command



        currentLetter = 0;

        if (lastValues.size() == 0) {

          if (didPreviousFPSCommandSucceed == false) {
            currentFPS = NO_FPS_CALCULATED;
          }

          didPreviousFPSCommandSucceed = false;
          return;
        }

        currentFPS = framesCounted();
        didPreviousFPSCommandSucceed = true;

      } else {
        resultLine[currentLetter] = recvData;
        currentLetter++;
      }


    }

  }


}

int framesCounted() {
  int frameCount = 0;
  long lastFrameFinishedTime = lastValues.top();


  while (!lastValues.empty()) {
    long currentValue = lastValues.top();
    lastValues.pop();

    if ((lastFrameFinishedTime - currentValue) <= TIME_INTERVAL_FPS_SECOND) {
      frameCount++;
    }
  }


  if (frameCount > MAX_FPS) {
    frameCount = MAX_FPS;
  } else if (frameCount < 2) {
    frameCount = NO_FPS_CALCULATED;
  }

  return frameCount;

}
