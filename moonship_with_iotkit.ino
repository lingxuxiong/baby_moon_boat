// Source code of Baby Moon Boat(BMB).
// For cloud capability, following prequisites have to be met:
// 1. Device must be activated;
//    --> iotkit-admin activiate activation_code
// 2. Iotkit agent must be started and configured to use MQTT protocol;
//    --> iotkit-admin protocol mqtt
// 3. A component named "motorcontroller" of type "motorctrl.v1.1" must be
//    registered on the device. The motorctrl.v1.1 component type has
//    a command named "motor". When executed from Control section on
//    dashboard with value 1, motor will be started, whereas value 0
//    will stop the motor.
//     --> iotkit-admin register motorcontroller motorctrl.v1.1
// 4. To report sensor values to cloud, corresponding components
//    must be registered also.
//      --> iotkit-admin register temp temperature.v1.0
//      --> iotkit-admin register water water.v1.0
//      --> iotkit-admin register zpos position.v1.0
//      --> iotkit-admin register sound sound.v1.0
//   "iotkit-admin components" to show all registered components. 
//   "tail -f /tmp/agent.log" to check working status of mqtt protocol.

#include <IoTkit.h>     // include IoTkit.h to use the Intel IoT Kit
#include <Ethernet.h>  // must be included to use IoTkit
#include <aJSON.h>
#include <stdio.h>

#include <Grove_LED_Bar.h>
#include <Wire.h>
#include "MMA7660.h"

#define MotorSpeedSet             0x82
#define PWMFrequenceSet           0x84
#define DirectionSet              0xaa
#define MotorSetA                 0xa1
#define MotorSetB                 0xa5
#define Nothing                   0x01
#define EnableStepper             0x1a
#define UnenableStepper           0x1b
#define Stepernu                  0x1c
#define I2CMotorDriverAdd         0x0f   // Set the address of the I2CMotorDriver

#define READ_DATA_DELAY_TIME_MS_PER_LOOP  100
#define REPORT_DATA_TO_CLOUD_EVERY_N_LOOP  30
unsigned long loopCounter = 0;

IoTkit iotkit;
MMA7660 accelemeter;
Grove_LED_Bar bar(9, 8, 1);

// Digital pins definition.
#define WATER_LED   3
#define Z_POS_LED   4
#define WATER_PIN   5
#define RELAY_PIN   6
#define HEAT_LED    7

// Analog pins definition.
#define SOUND_PIN   A2
#define TEMP_PIN    A3

int zPos = 0;
int8_t x, y, z;
int shakeCounter = 0;

// the setup routine runs once when you press reset:
void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(Z_POS_LED, OUTPUT);
  pinMode(WATER_LED, OUTPUT);  
  pinMode(HEAT_LED, OUTPUT);  
  pinMode(WATER_PIN, INPUT);
  pinMode(SOUND_PIN, INPUT);
  pinMode(TEMP_PIN, INPUT);

 // initialize serial communication at 9600 bits per second:
  Serial.begin(9600);
  
  bar.begin();
  iotkit.begin();
  Wire.begin();  
  accelemeter.init();

  // wait some time till initialzation finish.
  delay(3000);

  startMotor();
}

// the loop routine runs over and over again forever:
void loop() { 
  processTemperature(analogRead(TEMP_PIN));
  processWater(digitalRead(WATER_PIN));
  processSound(analogRead(SOUND_PIN));

  accelemeter.getXYZ(&x, &y, &z);
  processPosition(x, y, z);
  zPos = z;

  // aync received command from cloud.
  iotkit.receive(callback);

  delay(READ_DATA_DELAY_TIME_MS_PER_LOOP);  // delay in between reads for stability

  loopCounter++;
}

void callback(char* json) {
  Serial.println("callback...");
  Serial.println(json);
  aJsonObject* parsed = aJson.parse(json);
  if (&parsed == NULL) {
    // invalid or empty JSON
    Serial.println("recieved invalid JSON");
    return;
  }

  aJsonObject* component = aJson.getObjectItem(parsed, "component");
  aJsonObject* command = aJson.getObjectItem(parsed, "command");
  aJsonObject* argv = aJson.getObjectItem(parsed, "argv");
  aJsonObject* argvArray = argv->child;
  aJsonObject* name = argvArray->child;
  aJsonObject* value = name->next;

  if ((component != NULL)) {
    if (strcmp(component->valuestring, "motorcontroller") == 0) {
      if ((command != NULL)) {
        if (strcmp(command->valuestring, "motor") == 0 && strcmp(value->valuestring, "0") == 0) {
          stopMotor();
        }
        if (strcmp(command->valuestring, "motor") == 0 && strcmp(value->valuestring, "1") == 0) {
          startMotor();
        }
      }
    }
  }
}

/**
 * Process raw temperature value from temperature sensor.
 * @param raw temperature value from temp sensor. 
 */
void processTemperature(int value) {  
  Serial.print("raw temp:");
  Serial.println(value);
  int temp = 1 / (log((float)(1023 - value) * 10000 / value / 10000) / 3975 + 1 / 298.15) - 273.15;

  Serial.print("normalized temp:");
  Serial.println(temp);
  
  int barLevel = int((temp - 20) / 2);
  bar.setLevel(barLevel > 0 ? barLevel : 1);

  if (temp < 20) {
    digitalWrite(HEAT_LED, HIGH);
    Serial.println("Heating...");
  } else {
    digitalWrite(HEAT_LED, LOW);
  }

  if (shouldReportDataToCloud()) {
      iotkit.send("temp", temp); // send normalized temperature value to cloud.
  }
  
}

/**
 * Process raw water value from water sensor.
 * @value water value from water sensor. 
 */
void processWater(int value) {
  Serial.print("water:");
  Serial.println(value);
  if (value) {
    digitalWrite(WATER_LED, LOW);
  } else {
    digitalWrite(WATER_LED, HIGH);
  }

  if (shouldReportDataToCloud()) {
      iotkit.send("water", value); // send water value to cloud.
  }
  
}

/**
 * Process raw sound value from sound sensor.
 * @param value, sound value from sound sensor. 
 */
void processSound(int value) {
  Serial.print("sound:");
  Serial.println(value);

  //shake when detect sounds
  if (value > 250) {
    startMotor();
    Serial.println("Crying...");
    digitalWrite(RELAY_PIN, HIGH);
    //MotorSpeedSetAB(100, 100); //defines the speed of motor 1 and motor 2;
    //delay(10); //this delay needed
    //MotorDirectionSet(0b1010);  //"0b1010" defines the output polarity, "10" means the M+ is "positive" while the M- is "negtive"
    shakeCounter = 0;
  } else {
    shakeCounter++;
    if (shakeCounter > 50) {
      shakeCounter = 0;
      digitalWrite(RELAY_PIN, LOW);
      // MotorSpeedSetAB(0, 0);
      stopMotor();
    }
  }

  if (shouldReportDataToCloud()) {
      iotkit.send("sound", value); // send sound value to cloud.
  }
  
}

/**
 * Process raw position value from 3-axis digital accelemeter sensor.
 * @param x position from 3-axis digital accelemeter sensor. 
 * @param y position from 3-axis digital accelemeter sensor.
 * @param z position from 3-axis digital accelemeter sensor.
 */
void processPosition(int x, int y, int z) {
  Serial.print("x:");
  Serial.print(x);
  Serial.print(", y:");
  Serial.print(y);
  Serial.print(", z:");
  Serial.println(z);
    
  if (z < -12) {
    digitalWrite(Z_POS_LED, HIGH);
  } else {
    digitalWrite(Z_POS_LED, LOW);
  }
  
  //motion alarm
  int zDelta = abs(zPos - z);
  if (zDelta > 10) {
    Serial.print("Motion:");
    Serial.println(zPos - z);
  }

  if (shouldReportDataToCloud()) {
      iotkit.send("zpos", z); // send z position value to cloud.
  }
  
}


boolean shouldReportDataToCloud() {
  return (0 == loopCounter % REPORT_DATA_TO_CLOUD_EVERY_N_LOOP);
}

void startMotor() {
  Serial.println("start motor:");
  //digitalWrite(RELAY_PIN, HIGH);
  
  MotorSpeedSetAB(100, 100); //defines the speed of motor 1 and motor 2;
}

void stopMotor() {
  Serial.println("stop motor:");
  //digitalWrite(RELAY_PIN, LOW);

  MotorSpeedSetAB(0, 0); 
}

// set the steps you want, if 255, the stepper will rotate continuely;
void SteperStepset(unsigned char stepnu)
{
  Wire.beginTransmission(I2CMotorDriverAdd); // transmit to device I2CMotorDriverAdd
  Wire.write(Stepernu);          // Send the stepernu command
  Wire.write(stepnu);            // send the steps
  Wire.write(Nothing);           // send nothing
  Wire.endTransmission();        // stop transmitting
}
///////////////////////////////////////////////////////////////////////////////
// Enanble the i2c motor driver to drive a 4-wire stepper. the i2c motor driver will
//driver a 4-wire with 8 polarity  .
//Direction: stepper direction ; 1/0
//motor speed: defines the time interval the i2C motor driver change it output to drive the stepper
//the actul interval time is : motorspeed * 4ms. that is , when motor speed is 10, the interval time
//would be 40 ms
//////////////////////////////////////////////////////////////////////////////////
void StepperMotorEnable(unsigned char Direction, unsigned char motorspeed)
{
  Wire.beginTransmission(I2CMotorDriverAdd); // transmit to device I2CMotorDriverAdd
  Wire.write(EnableStepper);        // set pwm header
  Wire.write(Direction);              // send pwma
  Wire.write(motorspeed);              // send pwmb
  Wire.endTransmission();    // stop transmitting
}
//function to uneanble i2C motor drive to drive the stepper.
void StepperMotorUnenable()
{
  Wire.beginTransmission(I2CMotorDriverAdd); // transmit to device I2CMotorDriverAdd
  Wire.write(UnenableStepper);        // set unenable commmand
  Wire.write(Nothing);
  Wire.write(Nothing);
  Wire.endTransmission();    // stop transmitting
}
//////////////////////////////////////////////////////////////////////
//Function to set the 2 DC motor speed
//motorSpeedA : the DC motor A speed; should be 0~100;
//motorSpeedB: the DC motor B speed; should be 0~100;

void MotorSpeedSetAB(unsigned char MotorSpeedA , unsigned char MotorSpeedB)  {
  MotorSpeedA = map(MotorSpeedA, 0, 100, 0, 255);
  MotorSpeedB = map(MotorSpeedB, 0, 100, 0, 255);
  Wire.beginTransmission(I2CMotorDriverAdd); // transmit to device I2CMotorDriverAdd
  Wire.write(MotorSpeedSet);        // set pwm header
  Wire.write(MotorSpeedA);              // send pwma
  Wire.write(MotorSpeedB);              // send pwmb
  Wire.endTransmission();    // stop transmitting
}
//set the prescale frequency of PWM, 0x03 default;
void MotorPWMFrequenceSet(unsigned char Frequence)  {
  Wire.beginTransmission(I2CMotorDriverAdd); // transmit to device I2CMotorDriverAdd
  Wire.write(PWMFrequenceSet);        // set frequence header
  Wire.write(Frequence);              //  send frequence
  Wire.write(Nothing);              //  need to send this byte as the third byte(no meaning)
  Wire.endTransmission();    // stop transmitting
}
//set the direction of DC motor.
void MotorDirectionSet(unsigned char Direction)  {     //  Adjust the direction of the motors 0b0000 I4 I3 I2 I1
  Wire.beginTransmission(I2CMotorDriverAdd); // transmit to device I2CMotorDriverAdd
  Wire.write(DirectionSet);        // Direction control header
  Wire.write(Direction);              // send direction control information
  Wire.write(Nothing);              // need to send this byte as the third byte(no meaning)
  Wire.endTransmission();    // stop transmitting
}

void MotorDriectionAndSpeedSet(unsigned char Direction, unsigned char MotorSpeedA, unsigned char MotorSpeedB)  { //you can adjust the driection and speed together
  MotorDirectionSet(Direction);
  MotorSpeedSetAB(MotorSpeedA, MotorSpeedB);
}

