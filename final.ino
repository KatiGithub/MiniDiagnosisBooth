#include <SPI.h>
#include <Ethernet.h>
#include <ArduinoJson.h>
#include <ArduinoHttpClient.h>

#include <LiquidCrystal_I2C.h>

#include <Wire.h>

#include <Key.h>
#include <Keypad.h>

#include <Adafruit_MLX90614.h>

#include "MAX30100_PulseOximeter.h"

#define REPORTING_PERIOD_MS 1000
#define rows 4
#define cols 4
#define id_number_size 4


byte mac[] = {0x47, 0x09, 0xA0, 0x18, 0xF1, 0x4C};
IPAddress server(192,168,43,12);
int port = 3000;

char keys[rows][cols] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

byte rowPins[rows] = {12, 11, 9, 8};
byte colPins[cols] = {7, 6, 5, 3};

String symptoms[8] = {"fatigue", "breathing-problem", "chest-pain", "rashes", "diarrhea", "fever", "headache", "stomach-ache"};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, rows, cols);

Adafruit_MLX90614 tempsensor = Adafruit_MLX90614();
//LiquidCrystal_I2C lcd = LiquidCrystal_I2C(0x27, 20, 4);
PulseOximeter pox;
EthernetClient ether;
HttpClient client = HttpClient(ether, server, port);

const int capacity = JSON_OBJECT_SIZE(25);

void setup() {
    Serial.begin(115200);
    
    pinMode(4, OUTPUT);
    digitalWrite(4, HIGH); // IMPORTANT, DISABLE THE SD CARD READER

    while(!Serial) {
        Serial.println("Serial is not connected");
        delay(1000);
    }

    Serial.println("DHCP Initialization Started...");
    if(Ethernet.begin(mac) == 1) { // ONLY CHECK ETHERNET.BEGIN == 1 IF ONLY MAC ADDRESS IS GIVEN.
        Serial.println("DHCP Init Success");
        Serial.println("IP Addresss is: ");
        Serial.print(Ethernet.localIP());
        Serial.println();
      
        delay(1000);
    } else {
        Serial.println("Initialization Failed");
        while(true);
    }

    Serial.println("Initializing Pulse Oximeter");
    poxInit();

    Wire.begin();
    tempsensor.begin();

    Serial.println("Temp Sensor initialized, testing now....");
    testifr();

//    lcd.begin(20, 4);
//    lcd.init();
//    lcd.backlight();
//
//    lcd.setCursor(7,1);
//    lcd.print("THAMMATORN *PIMVALAN* 3000");
//
//    delay(3000);
    
}

void loop() {
    StaticJsonDocument<capacity> doc;

    int id_number = getInputNum().toInt();
    doc["student_id"].set(id_number);
    doc["temp"].set(readAvgTemp());
    doc["heartrate"].set(getHeartRate());
    doc["percent"].set(getOxygen());

    String output;
    serializeJson(doc, output);

    Serial.println("\nSending Http Request");
    Serial.println("Post body: " + output);
    String response = sendHttpRequest(output, "/");

    if(!(response == "preliminary-questions")) {
      Serial.println("Error occured. Preliminary-questions");
      while(true);
    }

    doc.clear();

    String preliminary_results = askquestions();
    response = sendHttpRequest(preliminary_results, "/preliminary-question");

    deserializeJson(doc, response);

    while(response != "interview-over") {
      deserializeJson(doc, response);
      String question = doc["question"];
      Serial.println(question + " (1 = present, 2 = absent, 3 = don't know)");

      doc.clear();
      char answer_key;
      boolean selectedAnswer = false;

      while(!selectedAnswer) {
        answer_key = keypad.waitForKey();

        if(answer_key == '1') {
          doc["choice"].set("present");
          Serial.println("Answer: present");
          selectedAnswer = true;
        } else if(answer_key == '2') {
          doc["choice"].set("absent");
          Serial.println("Answer: absent");
          selectedAnswer = true;
        } else if(answer_key == '3') {
          doc["choice"].set("unknown");
          Serial.println("Answer: unknown");
          selectedAnswer = true;
        }
      }

      String json_answer;

      serializeJson(doc, json_answer);
      
      response = sendHttpRequest(json_answer, "/api-middleman");
      doc.clear();
    }
    Serial.println("Interview over");
    delay(2000);
    Serial.println("Conditions: (press any key to exit) \n" + response);

    char exit_key = keypad.waitForKey();
}

String getInputNum() {
  Serial.println("Enter id number: ");
  
  char id_number[id_number_size];

    while(!isDigit(id_number[id_number_size])) {
        char key = keypad.getKey();

        if(isDigit(key)) {
            for(int i = 0; i <= id_number_size; i++) {
                if(!isDigit(id_number[i])) {
                    if(i == id_number_size) {
                      break;
                    }
                    id_number[i] = key;
                    Serial.print(key);
                    break;
                }
            }
        }
    }

  id_number[id_number_size] = '\0';
  Serial.println((String)id_number);
  return (String)id_number;
}

void testifr() {
  int objectTemp = tempsensor.readObjectTempC();
  int ambientTemp = tempsensor.readAmbientTempC();

  if(ambientTemp > 100 || objectTemp > 100) {
    Serial.println("Ifr sensor not properly connected");
    while(true);
  }

  Serial.println("Current ambient temperature (in Celcius): " + (String) tempsensor.readAmbientTempC());
  Serial.println("Current object temperature (in Celcius): " + (String) tempsensor.readObjectTempC());
}

float getOxygen() {

  Serial.println("Please place your finger on the pulse oximeter (Oxygen)");
  delay(3000);

  Serial.println("pulse oximeter initialized");
  poxInit();
  
  uint32_t tsLastReport = 0;
  float avgOxygenPercent = 0;

  int i = 0;
  while(i < 10) {
    pox.update();
    if(millis() - tsLastReport > REPORTING_PERIOD_MS) {
      float oxygenPercentage = pox.getSpO2();
      Serial.println((String) oxygenPercentage + " %");

      if(oxygenPercentage > 20 && oxygenPercentage <= 100) {
        avgOxygenPercent = avgOxygenPercent + oxygenPercentage;
        i++;
      }
      tsLastReport = millis();
    }
  }

  avgOxygenPercent = (avgOxygenPercent/10);
  Serial.println("Avg Oxygen &: " + (String) avgOxygenPercent);
  return avgOxygenPercent;
}

float getHeartRate() {

  Serial.println("Please place your finger on the pulse oximeter (heart rate)");
  delay(3000);

  Serial.println("initializing oximeter");
  poxInit();

  uint32_t tsLastReport = 0;
  float avgheartRate = 0;

  int i = 0;
  while(i <= 10) {
    pox.update();
    
    if(millis() - tsLastReport > REPORTING_PERIOD_MS) {
      float heartRate = pox.getHeartRate();
      Serial.println((String) heartRate + " BPM");
      if(heartRate > 20 && heartRate < 170) {
        avgheartRate = avgheartRate + heartRate;
        i++;
      }
      tsLastReport = millis();
    }
  }

  avgheartRate = (avgheartRate/10);
  Serial.println("Average Heart rate: " + (String) avgheartRate);
  return avgheartRate;
}

String askquestions() {
  StaticJsonDocument<capacity> doc;

  Serial.println("Age: ");

  char key;

  char age[2];
  
  while(!isDigit(age[1])) {
        char key = keypad.waitForKey();

        if(isDigit(key)) {
            for(int i = 0; i < 2; i++) {
                if(!isDigit(age[i])) {
                    if(i == 2) {
                      break;
                    } else {
                      age[i] = key;
                      Serial.print(key);
                      break;
                    }
                }
            }
        }
    }

    age[2] = '\0';

    doc["age"].set(((String)age).toInt());

    Serial.println("Gender: (1 == Male, 2 == Female)");

    char gender_choice;

  boolean selectedGender = false;

  while(!selectedGender) {
    gender_choice = keypad.waitForKey();
    
    if(gender_choice == '1') {
      doc["gender"].set("M");
      selectedGender = true;
      Serial.println("Selected: Male");
    } else if(gender_choice == '2') {
      doc["gender"].set("F");
      selectedGender = true;
      Serial.println("Selected: Female");
    }  
  }

    Serial.println("Do you smoke? (1 = true, 2 = false)");

    char smoke_choice;
    boolean selectedSmoke = false;

    while(!selectedSmoke) {
      smoke_choice = keypad.waitForKey();

      if(smoke_choice == '1') {
        doc["smoke"].set(true);
        selectedSmoke = true;
        Serial.println("Selected: true");
      } else if(smoke_choice == '2') {
        doc["smoke"].set(false);
        selectedSmoke = true;
        Serial.println("Selected: false");
      }
    }
    JsonArray array = doc["symptoms"].to<JsonArray>();

    Serial.println("Symptoms: ");
    for(int thisSymptom; thisSymptom < 8; thisSymptom++) {
      Serial.println(symptoms[thisSymptom] + "(1 = present, (any key) = absent)");
      boolean selectedSymptoms = false;
      char symptom_choice;
      
      while(!selectedSymptoms) {
         symptom_choice = keypad.waitForKey();

         if(symptom_choice == '1') {
            array.add(symptoms[thisSymptom]);
            Serial.println("Added Symptom: " + symptoms[thisSymptom]);
            selectedSymptoms = true;
         } else {
            selectedSymptoms = true;
         }
      }
    };

    Serial.println("Cancer (1 = true, 2 = false)");
    char cancer;
    boolean selectedCancer = false;

    while(!selectedCancer) {
      cancer = keypad.waitForKey();

      if(cancer == '1') {
        doc["cancer"].set(true);
        selectedCancer = true;
        Serial.println("Cancer: True");
      } else if(cancer == '2') {
        doc["cancer"].set(false);
        selectedCancer = true;
        Serial.println("Cancer: False");
      }
    }
    

    Serial.println("Medications: (1 = true, 2 = false)");
    char medications;
    boolean selectedMedications = false;

    while(!selectedMedications) {
      medications = keypad.waitForKey();

      if(medications == '1') {
        doc["medications"].set(true);
        selectedMedications = true;
        Serial.println("Medications: True");
      } else if(medications == '2') {
        doc["medications"].set(false);
        selectedMedications = true;
        Serial.println("Medications: False");
      }
    }

    String output;
    serializeJson(doc, output);

    return output;
}

double readAvgTemp() {

  testifr();

  Serial.println("Measuring your temperature");
  delay(2000);
  
  double objtemp;

  for (int i = 0; i <= 10; i++){
    objtemp = objtemp + tempsensor.readObjectTempC();
    delay(200);
  }

  Serial.println("Avg temp: " + (String)(objtemp/10));
  return objtemp/10;
}

void poxInit() {
  if(!pox.begin()) {
    Serial.println("FAILED");
    while(true);
  } else {
    Serial.println("SUCCESS");
  }
}

String sendHttpRequest(String postBody, String endpoint) {
    client.beginRequest();
    client.post(endpoint);
    client.sendHeader("Content-Type", "application/json");
    client.sendHeader("Content-Length", postBody.length());
    client.beginBody();
    client.print(postBody);
    client.endRequest();
    Serial.println(client.responseStatusCode());
    return client.responseBody();
}
