#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <DHT.h>

const int relayPin = 14;
const int soilMoisturePin = 34;
const int buzzerPin = 32;

const int LedRedPin = 4;
const int LedYellowPin = 0;
const int LedGreenPin = 2;

float soilMoisturePercentage = 0;

String mode;
String pumpStatus;

#define DHT_PIN 15
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

LiquidCrystal_I2C lcd(0x27, 20, 4);

#define WIFI_SSID "(SSID_WIFI)"
#define WIFI_PASSWORD "(PASSWORD_WIFI)"
#define FIREBASE_HOST "(url_firebase_realtime-database)"
#define FIREBASE_API_KEY "(authentication_token_firebase)"

FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200, 60000);

unsigned long lastDisplaySwitch = 0;
int currentDisplay = 1;

void setup() {
  Serial.begin(115200);
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);

  pinMode(buzzerPin, OUTPUT);

  dht.begin();

  pinMode(LedRedPin, OUTPUT);
  pinMode(LedYellowPin, OUTPUT);
  pinMode(LedGreenPin, OUTPUT);

  Wire.begin(21, 22);
  lcd.begin(20, 4);
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("~> HydroSense 1.0 <~");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
    lcd.setCursor(0, 2);
    lcd.print("Wifi Dalam Pencarian");
  }
  Serial.println("WiFi Connected");
  lcd.setCursor(0, 2);
  lcd.print("= Wifi Sudah Konek =");

  timeClient.begin();
  timeClient.update();

  config.api_key = FIREBASE_API_KEY;
  config.database_url = FIREBASE_HOST;

  if (Firebase.signUp(&config, &auth, "", "")){
    Serial.println("Sudah Konek Firebase");
  }
  else{
    Serial.println(String("Error: ").concat(config.signer.signupError.message.c_str()));
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  getModeFromFirebase();
}

void loop() {
  getModeFromFirebase();
  timeClient.update();

  // if (millis() - lastDisplaySwitch >= 3000) {
  //   currentDisplay = (currentDisplay == 3) ? 1 : currentDisplay + 1;
  //   updateDisplay();
  //   lastDisplaySwitch = millis();
  // }

  if (millis() - lastDisplaySwitch >= 2000) {
    currentDisplay = (currentDisplay == 1) ? 2 : 1;
    updateDisplay();
    lastDisplaySwitch = millis();
  }

  if (mode == "1") {
    soilMoisturePercentage = readSoilMoisture();
    digitalWrite(LedRedPin, LOW);
    digitalWrite(LedGreenPin, HIGH);

    if (soilMoisturePercentage <= 30) {
      turnPumpOn();
      Firebase.setString(firebaseData, "/data/soil_status", "Tanah_Kering");
      buzzer();
    }
    else {
      turnPumpOff();
      Firebase.setString(firebaseData, "/data/soil_status", "Tanah_Aman");
    }
  }
  else if (mode == "0") {
    controlPumpWithFirebase();
    soilMoisturePercentage = readSoilMoisture();
    digitalWrite(LedGreenPin, LOW);
    digitalWrite(LedRedPin, HIGH);

    if (soilMoisturePercentage <= 30) {
      Firebase.setString(firebaseData, "/data/soil_status", "Tanah_Kering");
      buzzer();
    }
    else {
      Firebase.setString(firebaseData, "/data/soil_status", "Tanah_Aman");
    }
  }

  // sendPumpStatusToFirebase(int soilMoisturePercentage);

  updateFirebase(soilMoisturePercentage);
  delay(100);
}

float readSoilMoisture() {
  int sensorValue = analogRead(soilMoisturePin);
  return map(4095 - sensorValue, 0, 4095, 0, 100);
}

void turnPumpOn() {
  digitalWrite(relayPin, LOW);
  pumpStatus = "1";
  digitalWrite(LedYellowPin, HIGH);
  Firebase.setString(firebaseData, "/data/pump_status", pumpStatus);
}

void turnPumpOff() {
  digitalWrite(relayPin, HIGH);
  pumpStatus = "0";
  digitalWrite(LedYellowPin, LOW);
  Firebase.setString(firebaseData, "/data/pump_status", pumpStatus);
}

void updateFirebase(float moisture) {
  String pathSoilMoisture = "/data/soil_moisture";
  String pathTemperature = "/data/temperature";
  String pathHumidity = "/data/humidity";
  String pathSoilStatus = "/data/soil_status"; // Path untuk status tanah

  if (Firebase.setFloat(firebaseData, pathSoilMoisture, moisture)) {
    Serial.println("Soil moisture update ke Firebase");
  } 
  else {
    Serial.println("Gagal push soil moisture");
    Serial.println(firebaseData.errorReason());
  }

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (!isnan(temperature) && !isnan(humidity)) {
    if (Firebase.setFloat(firebaseData, pathTemperature, temperature)) {
      Serial.println("Temperature update ke Firebase");
    } 
    else {
      Serial.println("Gagal push temperature");
      Serial.println(firebaseData.errorReason());
    }

    if (Firebase.setFloat(firebaseData, pathHumidity, humidity)) {
      Serial.println("Humidity update ke Firebase");
    } 
    else {
      Serial.println("Gagal push humidity");
      Serial.println(firebaseData.errorReason());
    }
  } 
  else {
    Serial.println("Gagal membaca sensor DHT22");
  }

  if (moisture <= 30) {
    Firebase.setString(firebaseData, pathSoilStatus, "Tanah_Kering");
    buzzer();
  } 
  else {
    Firebase.setString(firebaseData, pathSoilStatus, "Tanah_Aman");
  }
}

void getModeFromFirebase() {
  String pathMode = "/data/mode";

  if (Firebase.getString(firebaseData, pathMode)) {
    mode = firebaseData.stringData();
    Serial.print("Mode: ");
    Serial.println(mode);
  }
  else {
    Serial.println("Gagal fetch mode dari firebase");
    Serial.println(firebaseData.errorReason());
  }
}

void controlPumpWithFirebase() {
  String pathPumpStatus = "/data/pump_status";

  if (Firebase.getString(firebaseData, pathPumpStatus)) {
    String status = firebaseData.stringData();
    Serial.print("Pump Status dari Firebase: ");
    Serial.println(status);
    if (status == "1") {
      turnPumpOn();
    } 
    else if (status == "0") {
      turnPumpOff();
    } 
  } 
  else {
    Serial.println("Gagal fetch status pump dari Firebase");
    Serial.println(firebaseData.errorReason());
  }
}

// void sendPumpStatusToFirebase(int soilMoisturePercentage) {

//   String pumpStatus = (soilMoisturePercentage <= 30) ? "On" : "Off";
  
//   if (Firebase.setString(firebaseData, "/data/pump_status", pumpStatus)) {
//     Serial.println("Status pump terkirim ke Firebase: " + pumpStatus);
//   } 
//   else {
//     Serial.println("Error mengirim status pump: " + firebaseData.errorReason());
//   }
// }

void updateDisplay() {
  lcd.clear();

  if (currentDisplay == 1) {
    lcd.setCursor(0, 0);
    lcd.print("~> HydroSense 1.0 <~");
    lcd.setCursor(0, 1);
    lcd.print("     " + getFormattedTime() + " WIB      ");
    if (mode == "1"){
      lcd.setCursor(0, 2);
      lcd.print("Mode  : Automatic");
    }
    else if (mode == "0"){
      lcd.setCursor(0, 2);
      lcd.print("Mode  : Manual");
    }
    // lcd.setCursor(0, 2);
    // lcd.print("Mode  : " + mode);
    if (pumpStatus == "1"){
      lcd.setCursor(0, 3);
      lcd.print("Pompa : On");
    }
    else if (pumpStatus == "0"){
      lcd.setCursor(0, 3);
      lcd.print("Pompa : Off");
    }
    // lcd.setCursor(0, 3);
    // lcd.print("Pompa : " + c);
  }
  else if (currentDisplay == 2) {
    lcd.setCursor(0, 0);
    lcd.print("###~ MONITORING ~###");
    lcd.setCursor(0, 1);
    lcd.print("Tanah : " + String(soilMoisturePercentage) + "%");
    lcd.setCursor(0, 2);
    lcd.print("Udara : " + String(dht.readHumidity()) + "%");
    lcd.setCursor(0, 3);
    lcd.print("Suhu  : " + String(dht.readTemperature()) + " C");
  }
  // else if (currentDisplay == 3) {
  //   lcd.setCursor(0, 0);
  //   lcd.print("== Suhu & Kelembapan ==");
  //   lcd.setCursor(0, 1);
  //   lcd.print("Suhu: " + String(dht.readTemperature()) + " C");
  //   lcd.setCursor(0, 2);
  //   lcd.print("Kelembapan: " + String(dht.readHumidity()) + " %");
  // }
}

void buzzer(){
  tone(buzzerPin, 1318, 300);
  delay(500);

  tone(buzzerPin, 609, 200);
  delay(500);

  noTone(buzzerPin);
}

String getFormattedTime() {
  unsigned long rawTime = timeClient.getEpochTime();
  int hours = (rawTime % 86400L) / 3600;
  int minutes = (rawTime % 3600) / 60;
  // int seconds = rawTime % 60;

  char buffer[9];
  sprintf(buffer, "%02d:%02d", hours, minutes);
  return String(buffer);
}
