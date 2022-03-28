#include <esp_now.h>
#include <WiFi.h>

#define PRINTSCANRESULTS 0
#define PEER_SSID_PREFIX "IoT_camper"

const int pin1 = 26;
const int pin2 = 18;
const int pin3 = 19;
const int pin4 = 23;
const int pin5 = 5;
const int pin6 = 13;
const int pin7 = 33;
const int pin8 = 14;

const int pin9 = 21; //12V output

int level = 0;
int in1;
int in2;
int in3;
int in4;
int in5;
int in6;
int in7;
int in8;

esp_now_peer_info_t slave;  //sending to 1 slave
uint8_t channel_old = 0;
bool slaveFound = 0;

typedef struct message_struct {
  int message_id;
  float message_value;
  String message_description;
} message_struct;
message_struct sensormessage;

typedef struct {
  int message_id;
  float message_value;
  String message_description;
} sensor_struct;
sensor_struct sensor[10];

void setup() {
  Serial.begin(115200);
  btStop();
  esp_sleep_enable_timer_wakeup(10 * 60 * 1000000); //10 * 60  = 10 min.
  //Set device in STA mode to begin with
  WiFi.mode(WIFI_STA);
  Serial.println("ESPNow/Multi-Slave/Master");
  Serial.print("STA MAC: "); Serial.println(WiFi.macAddress());
  Serial.print("Wi-Fi Channel: ");
  Serial.println(WiFi.channel());
  
  InitESPNow();
  esp_now_register_send_cb(OnDataSent);

  sensor[0] = (sensor_struct){11, 0, "V"}; //Voltage 1
  sensor[1] = (sensor_struct){12, 0, "V"}; //Voltage 2
  sensor[2] = (sensor_struct){21, 0, "C"}; //Temp outside
  sensor[3] = (sensor_struct){22, 0, "C"}; //Temp inside
  sensor[4] = (sensor_struct){31, 0, "%"}; //Hum outside
  sensor[5] = (sensor_struct){32, 0, "%"}; //Hum inside
  sensor[6] = (sensor_struct){41, 0, "g"}; //Gastank front
  sensor[7] = (sensor_struct){42, 0, "g"}; //Gastank back
  sensor[8] = (sensor_struct){51, 0, "L"}; //Drink water
  sensor[9] = (sensor_struct){52, 0, "L"}; //Wasted water

  pinMode(pin1, INPUT_PULLUP);
  pinMode(pin2, INPUT_PULLUP);
  pinMode(pin3, INPUT_PULLUP);
  pinMode(pin4, INPUT_PULLUP);
  pinMode(pin5, INPUT_PULLUP);
  pinMode(pin6, INPUT_PULLUP);
  pinMode(pin7, INPUT_PULLUP);
  pinMode(pin8, INPUT_PULLUP);
  pinMode(pin9, OUTPUT);
}

void loop() {
  ScanForSlave();
  if (slaveFound) {
    manageSlave();
    //**sensor reading
    readwaterlevel();
    esp_deep_sleep_start(); //go to sleep 
    //delay(5000);  
  } else {
    // No slave found to process
    delay(2000);
  }
}

// #########################################################################
// Read Waterlevel 
// #########################################################################
void readwaterlevel() {
  digitalWrite(pin9, HIGH); //12V sensor on, as short as possible 
  delay(50); //to get full Voltage at output
  in1 = digitalRead(pin1);
  in2 = digitalRead(pin2);
  in3 = digitalRead(pin3);
  in4 = digitalRead(pin4);
  in5 = digitalRead(pin5);
  in6 = digitalRead(pin6);
  in7 = digitalRead(pin7);
  in8 = digitalRead(pin8);
  level = (8 - (in1+in2+in3+in4+in5+in6+in7+in8)) * 100/8;
  Serial.print("Level: ");
  Serial.print(level);
  Serial.println("%");
  digitalWrite(pin9, LOW); //12V sensor off
  sensormessage.message_id = sensor[8].message_id;
  sensormessage.message_value = level;
  sensormessage.message_description = sensor[8].message_description;
  sendData();
}

// #########################################################################
// 
// #########################################################################
void sendData() {
  const uint8_t *peer_addr = slave.peer_addr;
  Serial.println("Sending data");
  Serial.println(sensormessage.message_id);
  Serial.println(sensormessage.message_value);
  Serial.println(sensormessage.message_description);
  esp_err_t result = esp_now_send(peer_addr,(uint8_t *) &sensormessage, sizeof(message_struct));
  Serial.print("Send Status: ");
  if (result == ESP_OK) {
    Serial.println("Success");
  } else if (result == ESP_ERR_ESPNOW_NOT_INIT) {
    Serial.println("ESPNOW not Init.");
  } else if (result == ESP_ERR_ESPNOW_ARG) {
    Serial.println("Invalid Argument");
  } else if (result == ESP_ERR_ESPNOW_INTERNAL) {
    Serial.println("Internal Error");
  } else if (result == ESP_ERR_ESPNOW_NO_MEM) {
    Serial.println("ESP_ERR_ESPNOW_NO_MEM");
  } else if (result == ESP_ERR_ESPNOW_NOT_FOUND) {
    Serial.println("Peer not found.");
  } else {
    Serial.println("Not sure what happened");
  }
   delay(100);
}

// #########################################################################
// 
// #########################################################################
void InitESPNow() {
  if (esp_now_init() == ESP_OK) {
    Serial.println("ESPNow Init Success");
  } else {
    Serial.println("ESPNow Init Failed");
    ESP.restart();
  }
}

// #########################################################################
// Scan for slaves in AP or STA mode
// #########################################################################
void ScanForSlave() {
  int8_t scanResults = WiFi.scanNetworks();
  slaveFound = 0;
  memset(&slave, 0, sizeof(slave));
  Serial.println("");
  if (scanResults == 0) {
    Serial.println("No WiFi devices in AP/STA Mode found");
  } else {
    Serial.print("Found "); Serial.print(scanResults); Serial.println(" devices ");
    for (int i = 0; i < scanResults; ++i) {
      String SSID = WiFi.SSID(i);
      int32_t RSSI = WiFi.RSSI(i);
      String BSSIDstr = WiFi.BSSIDstr(i);
      uint8_t channel = WiFi.channel(i);

      if (PRINTSCANRESULTS) {
        Serial.print(i + 1); 
        Serial.print(": "); 
        Serial.print(SSID); 
        Serial.print(" ["); 
        Serial.print(BSSIDstr); 
        Serial.print("]"); 
        Serial.print(" ("); 
        Serial.print(RSSI); 
        Serial.print(")");
        Serial.print(" Chn: ");
        Serial.print(channel);
        Serial.println("");
      }
      delay(10);
      if (SSID.indexOf(PEER_SSID_PREFIX) == 0) {
        Serial.println("Found a Slave.");
        Serial.print(i + 1); 
        Serial.print(": "); 
        Serial.print(SSID); 
        Serial.print(" ["); 
        Serial.print(BSSIDstr); 
        Serial.print("]"); 
        Serial.print(" ("); 
        Serial.print(RSSI); 
        Serial.print(")");
        Serial.print(" Chn: ");
        Serial.print(channel);
        Serial.println("");
        int mac[6];
        if ( 6 == sscanf(BSSIDstr.c_str(), "%x:%x:%x:%x:%x:%x%c",  &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5] ) ) {
          for (int ii = 0; ii < 6; ++ii ) {
            slave.peer_addr[ii] = (uint8_t) mac[ii];
          }
        }
        slave.channel = channel;
        slave.encrypt = 0;
        if(channel != channel_old){
          setupDummyAP(); //change channel
          WiFi.disconnect();
          WiFi.mode(WIFI_STA);
          channel_old = channel;
        }
        slaveFound = 1;
        break;
      }
    }
  }
  if (slaveFound) {
  Serial.println(" Slave found, processing..");
  } else {
    Serial.println("No Slave Found, trying again.");
  }
  WiFi.scanDelete();
}

// #########################################################################
// Set dummy AP to change to correct channel
// #########################################################################
void setupDummyAP() {
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  delay(100);
  const char *dummy = "abcdefgh";
  //         SSID ,  PSK ,  channel  , BSSID, connect
  WiFi.softAP(dummy, dummy, slave.channel, 0, false);
  delay(250);
  // DEBUG: print WiFi connection info
  //   Mode, Channel, SSID, Passphrase , BSSID, AP MAC
  //WiFi.printDiag(Serial);
  Serial.print("Channel new: ");
  Serial.println(WiFi.channel());
  delay(100);
}

// #########################################################################
// Pair to slave
// #########################################################################
void manageSlave() {
  if (slaveFound) {
    const esp_now_peer_info_t *peer = &slave;
    const uint8_t *peer_addr = slave.peer_addr;
    Serial.print("Processing: ");
    for (int ii = 0; ii < 6; ++ii ) {
      Serial.print((uint8_t) slave.peer_addr[ii], HEX);
      if (ii != 5) Serial.print(":");
    }
    Serial.print(" Status: ");
    bool exists = esp_now_is_peer_exist(peer_addr);
    if (exists) {
      // Slave already paired.
      Serial.println("Already Paired");
    } else {
      // Slave not paired, attempt pair
      esp_err_t addStatus = esp_now_add_peer(peer);
      if (addStatus == ESP_OK) {
        Serial.println("Pair success");
      } else if (addStatus == ESP_ERR_ESPNOW_NOT_INIT) {
        // How did we get so far!!
        Serial.println("ESPNOW Not Init");
      } else if (addStatus == ESP_ERR_ESPNOW_ARG) {
        Serial.println("Add Peer - Invalid Argument");
      } else if (addStatus == ESP_ERR_ESPNOW_FULL) {
        Serial.println("Peer list full");
      } else if (addStatus == ESP_ERR_ESPNOW_NO_MEM) {
        Serial.println("Out of memory");
      } else if (addStatus == ESP_ERR_ESPNOW_EXIST) {
        Serial.println("Peer Exists");
      } else {
        Serial.println("misc error");
      }
      delay(100);
    }
  } else {
    Serial.println("No Slave found to process");
  }
}

// #########################################################################
// callback when data is sent from Master to Slave
// #########################################################################
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print("Last Packet Sent to: "); Serial.println(macStr);
  Serial.print("Last Packet Send Status: "); Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}
