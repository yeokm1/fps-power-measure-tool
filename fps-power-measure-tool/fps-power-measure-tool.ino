#include <SPI.h>
#include <Adb.h>
#include <StandardCplusplus.h>
#include <Stack>

#define FPS_COMMAND "dumpsys SurfaceFlinger --latency SurfaceView && echo $\n"
#define FPS_RESULT_LINE_LENGTH 100
#define TIME_INTERVAL_FPS_SECOND 1000
#define TIME_SHELL_COMMAND_ISSUE 1000

#define MAX_FPS 60
#define NO_FPS_CALCULATED -1

using namespace std;



Connection * shell;

long shellLastCallTime = millis();


char resultLine[FPS_RESULT_LINE_LENGTH];
int currentLetter = 0;

stack<long> lastValues;

bool didPreviousFPSCommandSucceed = false;

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
            Serial.println(NO_FPS_CALCULATED);
          }

          didPreviousFPSCommandSucceed = false;
          return;
        }

        int frameCount = framesCounted();
        didPreviousFPSCommandSucceed = true;

        Serial.println(frameCount);


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

  if (!lastValues.empty()) {
    //Ignore the last value is it can be invalid on some systems, notably the Nexus 5
    lastValues.pop();
  }

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
void setup()
{

  // Initialise serial port
  Serial.begin(115200);

  // Initialise the ADB subsystem.
  ADB::init();
  // Open an ADB stream to the phone's shell. Auto-reconnect
  shell = ADB::addConnection("shell:", true, adbEventHandler);

}

void loop()
{
  // Poll the ADB subsystem.

  if (shell->status == ADB_OPEN) {

    long currentTime = millis();
    if ((currentTime - shellLastCallTime) > TIME_SHELL_COMMAND_ISSUE) {
      shellLastCallTime = currentTime;
      ADB::writeString(shell, FPS_COMMAND);
    }
  }

  ADB::poll();
}


