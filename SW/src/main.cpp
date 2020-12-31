
/* Connections:

 D1 Mini -> LT8900

 GND        GND
 3v3        VCC
 D0         PKT
 D8         CS
 D4         RST
 D7         MOSI
 D6         MISO
 D5         SCK

*/

#include <arduino.h>
#include <SPI.h>
#include "LT8900.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
//#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <FS.h>
#include <web.h>
#include <Wire.h>
#include "ArduinoJson.h"

#define TX_REPEAT_COUNT 10

// Using LT8900 without RST and PKT pins
const uint8_t PIN_NRF_RST = 0;
const uint8_t PIN_NRF_CS = 15;
const uint8_t PIN_NRF_PKT = 0;
const uint8_t PIN_LED = 2;
const uint16_t T_BAROMESS = 30000;


bool rem_key_hold;
uint8_t rem_group_act;
int lastCounter;
uint16_t RemContr_Add;
uint8_t LearnCnt,CommandSeq;
String ssid,password,mqtt_server,host_name;
String webPage = "";

ESP8266WebServer server(80);
LT8900 lt(PIN_NRF_CS, PIN_NRF_PKT, PIN_NRF_RST);

//MPL3115A2 myPressure;
unsigned long PrevPressMess;


WiFiClient espClient;
PubSubClient client(espClient);
StaticJsonDocument<200> doc;

void FlashLed(int Number, int Del)
{
  for (int i=0;i<Number;i++)
  {
    digitalWrite (PIN_LED,0);
    delay(Del);
    digitalWrite (PIN_LED,1);
    delay(Del);
  }
  //pinMode(PIN_NRF_PKT,INPUT);
}

void Flash_Light(int Nr, int del)
{
  for (int i=0;i<Nr;i++)
  {
    client.publish("Remote/AllON","1");
    client.loop();
    delay(del);
    client.publish("Remote/AllON","0");
    client.loop();
    delay(del);
  }
}

void setup_web()
{



  server.on("/", [](){
    server.send(200, "text/html", INDEX_HTML_HEAD);
  });
  server.begin();
}

void setup_FS(void)
{
  bool ok = SPIFFS.begin();
  if (ok) {
    Serial.println("FS Opening: OK");
  }
  File f = SPIFFS.open("/wconf.txt", "r");
  if (!f) {
      Serial.println("file open failed");
  }
  ssid = f.readStringUntil('\n');
  password = f.readStringUntil('\n');
  mqtt_server = f.readStringUntil('\n');
  host_name = f.readStringUntil('\n');
  ssid.remove((ssid.length()-1));
  password.remove((password.length()-1));
  mqtt_server.remove((mqtt_server.length()-1));
  host_name.remove((host_name.length()-1));
  host_name = "Remote";
  Serial.println(host_name);

  /////////// NEED TO RUN ONLY ONCE ///////////
  //  Serial.println("Spiffs formating...");
  //  SPIFFS.format();
  //  Serial.println("Spiffs formatted");
}

void callback(char* topic, byte* payload, unsigned int length)
{
 bool res;
 // FlashLed(2, 500);
 //  TimeOut = atoi((char*)payload);
 //  Serial.print("New TimeOut: ");
 //  Serial.println(TimeOut);
  if (strcmp(topic,"Remote/Raw")==0)
  { 
    Serial.println("Callback");
    Serial.print(length); Serial.print("Bytes ");
    payload[1] = CommandSeq;
    for (uint8_t i=0;i<length;i++) {Serial.print(payload[i]); Serial.print("  "); }
    Serial.println();
    for (uint8_t i=0; i<TX_REPEAT_COUNT;i++)
    {
      res = lt.sendPacket(payload,length);
      delay(1);
    }
    if (res) 
    {
      Serial.print("Sent ");
      Serial.print(TX_REPEAT_COUNT); Serial.println(" Times.");
    } 
    CommandSeq++;
  }
}

void setup_baro()
{
  myPressure.begin(); // Get sensor online

  // Configure the sensor
  //myPressure.setModeAltimeter(); // Measure altitude above sea level in meters
  myPressure.setModeBarometer(); // Measure pressure in Pascals from 20 to 110 kPa

  myPressure.setOversampleRate(7); // Set Oversample to the recommended 128
  myPressure.enableEventFlags(); // Enable all three pressure and temp event flags
}

void handle_baro()
{
  if ((millis()-PrevPressMess)<0) PrevPressMess = 0;
  if ((millis()-PrevPressMess)>T_BAROMESS)
  {
    PrevPressMess = millis();
    float pressure = myPressure.readPressure();
    float temperature = myPressure.readTemp();
    client.publish(String(host_name+"/status/pressure").c_str(),String(pressure).c_str());
    client.publish(String(host_name+"/status/temp").c_str(),String(temperature).c_str());
    Serial.print(pressure,2);Serial.print(" ");
    Serial.println(temperature,2);
    client.loop();
  }
}

void setup_wifi()
{
  WiFi.softAPdisconnect(true);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.hostname(host_name);
  WiFi.begin(ssid.c_str(), password.c_str());
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("Connecting to: "); Serial.println(ssid.c_str());}
  FlashLed(3, 200);
  Serial.println("Wifi Connected!");
  delay(1000);
  client.setServer(mqtt_server.c_str(), 1883);
  

}

/*void setup_OTA()
{
  ArduinoOTA.onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
      {
        type = "filesystem";
        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        SPIFFS.end();
      }

      Serial.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.setHostname(host_name.c_str());
    ArduinoOTA.begin();
    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println(ArduinoOTA.getHostname());
}
*/

void setup_spi()
{
  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE1);
  SPI.setClockDivider(SPI_CLOCK_DIV4);
}

void setup_Lt8900()
{
  lt.softReset();
  lt.begin();
  lt.setDataRate(LT8900::LT8900_1MBPS);
  lt.setChannel(74);
  lt.startListening();
  //lt.whatsUp(Serial);
}

void EEprom_Write2Bytes(int Address,uint16_t Data)
{
  EEPROM.write(Address, Data / 256);
  EEPROM.write(Address+1, Data % 256);
  EEPROM.end();
}

uint16_t EEprom_Read2Bytes(int Address)
{
  return EEPROM.read(Address)*256 + EEPROM.read(Address+1);
}


/* void ParseRemoteComm(uint8_t buf[])
{
  String Topic_Out;
  uint16_t LastRemAdd;
  if (lastCounter != buf[5])
  {
    lastCounter = buf[5];
    LastRemAdd = buf[1] * 256 + buf[2];
    if (buf[4] == 0x15 && (millis() < 90000))               // Enable Remote learning for 90s after start
    {                                                       // Remote Address Learning Mode
      LearnCnt++;
      if(LearnCnt >= 20)
      {                                                     // If keep pressed for 5 Sec
        RemContr_Add = buf[1] * 256 + buf[2];
        EEprom_Write2Bytes(0,RemContr_Add);                 // Save Remote Address to EEprom at adr0
        Serial.print("New Remote Learned Adress: ");
        Serial.println(RemContr_Add);
        Flash_Light(5, 300);

      }
    }  else LearnCnt = 0;
    if (LastRemAdd == RemContr_Add)
    {

      String Raw = String(buf[1],HEX) + " " + String(buf[2],HEX) + " " + String(buf[3],HEX) + " " + String(buf[4],HEX) + " " + String(buf[5],HEX);
      //client.publish("Remote/RAW",Raw.c_str());

      rem_group_act = buf[3];                     //Group 1,2,3,4 All-0
      if (buf[4]/16) rem_key_hold = true;
                else rem_key_hold = false;
      int actComm = buf[4] % 16;                  //AllON-0x05, AllOFF-0x09,
                                                  //UP-0x0C, DOWN-0x04,
                                                  //LEFT-0x0E, RIGHT-0x0F,
                                                  //GR1ON-0x08, GR1OFF-0x0B,
                                                  //GR2ON-0x0D, GR2OFF-0x03,
                                                  //GR3ON-0x07, GR3OFF-0x0A,
                                                  //GR4ON-0x02, GR4OFF-0x06,
      // Main ON
      if (actComm == 0x05)
      {
        client.publish(String(host_name+"/status/Gr0ON").c_str(),"1");
      }
      // Main OFF or All Night mode
      else if (actComm == 0x09)
      {
        if (!rem_key_hold)
        {
          client.publish(String(host_name+"/status/Gr0ON").c_str(),"0");
        }
        else if (rem_key_hold)
        {
          client.publish("Remote/Gr0Night","1");
        }
      }
      // Group 1 OFF or Night Mode
      else if (actComm == 0x0B)
      {
        if (rem_key_hold)
        {
          client.publish("Remote/Gr1Night","1");
        }
        else if (!rem_key_hold)
        {
          client.publish("Remote/Gr1On","0");
        }
      }
      // Group 1 ON or FULL Power
      else if (actComm == 0x08)
      {
        if (!rem_key_hold)
        {
          client.publish("Remote/Gr1On","1");
        }
        else if (rem_key_hold)
        {
          client.publish("Remote/Gr1Pow","100");
        }
      }
      // Group 2 OFF or Night Mode
      else if (actComm == 0x03)
      {
        if (rem_key_hold)
        {
          client.publish("Remote/Gr2Night","1");
        }
        else if (!rem_key_hold)
        {
          client.publish("Remote/Gr2On","0");
        }
      }
      // Group 2 ON or FULL Power
      else if (actComm == 0x0D)
      {
        if (!rem_key_hold)
        {
          client.publish("Remote/Gr2On","1");
        }
        else if (rem_key_hold)
        {
          client.publish("Remote/Gr2Pow","100");
        }
      }
      // Group 3 OFF or Night Mode
      else if (actComm == 0x0A)
      {
        if (rem_key_hold)
        {
          client.publish("Remote/Gr3Night","1");
        }
        else if (!rem_key_hold)
        {
          client.publish("Remote/Gr3On","0");
        }
      }
      // Group 3 ON or full power
      else if (actComm == 0x07)
      {
        if (!rem_key_hold)
        {
          client.publish("Remote/Gr3On","1");
        }
        else if (rem_key_hold)
        {
          client.publish("Remote/Gr3Pow","100");
        }
      }
      // Group 4 OFF or Night Mode
      else if (actComm == 0x06)
      {
        if (rem_key_hold)
        {
          client.publish("Remote/Gr4Night","1");
        }
        else if (!rem_key_hold)
        {
          client.publish("Remote/Gr4On","0");
        }
      }
      // Group 4 ON or FULL Power
      else if (actComm == 0x02)
      {
        if (!rem_key_hold)
        {
          client.publish("Remote/Gr4On","1");
        }
        else if (rem_key_hold)
        {
          client.publish("Remote/Gr4Pow","100");
        }
      }
      // Power UP for Groups
      else if (actComm == 0x0C)
      {
        if (rem_group_act == 0)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr0Pow","1");
          }
          else
          {
            client.publish("Remote/Gr0Pow","10");
          }
        }
        else if (rem_group_act == 1)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr1Pow","1");
          }
          else
          {
            client.publish("Remote/Gr1Pow","10");
          }
        }
        else if (rem_group_act == 2)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr2Pow","1");
          }
          else
          {
            client.publish("Remote/Gr2Pow","10");
          }
        }
        else if (rem_group_act == 3)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr3Pow","1");
          }
          else
          {
            client.publish("Remote/Gr3Pow","10");
          }
        }
        else if (rem_group_act == 4)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr4Pow","1");
          }
          else
          {
            client.publish("Remote/Gr4Pow","10");
          }
        }
      }
      // Power DOWN for Groups
      else if (actComm == 0x04)
      {
        if (rem_group_act == 0)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr0Pow","-1");
          }
          else
          {
            client.publish("Remote/Gr0Pow","-10");
          }
        }
        else if (rem_group_act == 1)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr1Pow","-1");
          }
          else
          {
            client.publish("Remote/Gr1Pow","-10");
          }
        }
        else if (rem_group_act == 2)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr2Pow","-1");
          }
          else
          {
            client.publish("Remote/Gr2Pow","-10");
          }
        }
        else if (rem_group_act == 3)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr3Pow","-1");
          }
          else
          {
            client.publish("Remote/Gr3Pow","-10");
          }
        }
        else if (rem_group_act == 4)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr4Pow","-1");
          }
          else
          {
            client.publish("Remote/Gr4Pow","-10");
          }
        }
      }
      // CWW to warm for Groups
      else if (actComm == 0x0E)
      {
        if (rem_group_act == 0)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr0CWW","-1");
          }
          else
          {
            client.publish("Remote/Gr0CWW","-10");
          }
        }
        else if (rem_group_act == 1)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr1CWW","-1");
          }
          else
          {
            client.publish("Remote/Gr1CWW","-10");
          }
        }
        else if (rem_group_act == 2)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr2CWW","-1");
          }
          else
          {
            client.publish("Remote/Gr2CWW","-10");
          }
        }
        else if (rem_group_act == 3)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr3CWW","-1");
          }
          else
          {
            client.publish("Remote/Gr3CWW","-10");
          }
        }
        else if (rem_group_act == 4)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr4CWW","-1");
          }
          else
          {
            client.publish("Remote/Gr4CWW","-10");
          }
        }
      }
      // CWW to cold for Groups
      else if (actComm == 0x0F)
      {
        if (rem_group_act == 0)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr0CWW","1");
          }
          else
          {
            client.publish("Remote/Gr0CWW","10");
          }
        }
        else if (rem_group_act == 1)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr1CWW","1");
          }
          else
          {
            client.publish("Remote/Gr1CWW","10");
          }
        }
        else if (rem_group_act == 2)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr2CWW","1");
          }
          else
          {
            client.publish("Remote/Gr2CWW","10");
          }
        }
        else if (rem_group_act == 3)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr3CWW","1");
          }
          else
          {
            client.publish("Remote/Gr3CWW","10");
          }
        }
        else if (rem_group_act == 4)
        {
          if(!rem_key_hold)
          {
            client.publish("Remote/Gr4CWW","1");
          }
          else
          {
            client.publish("Remote/Gr4CWW","10");
          }
        }
      }

      client.loop();                // Update MQTT client
    }
  }
}
 */

void Publish_Remote(uint8_t buf[])
{
  char outbuffer[256];
  if (buf[0] != 90)
    return;
  if (lastCounter == buf[5])
    return;
  if (lastCounter != buf[5])
    lastCounter = buf[5];
  doc["ID"] = buf[1] * 256 + buf[2];
  doc["GROUP"] = buf[3];
  doc["HOLD"] = buf[4] & 0x10;
  doc["COMM"] = buf[4] % 16;
  size_t n = serializeJson(doc, outbuffer);
  client.publish("Remote/CommandRec", outbuffer, n);
}

void setup()
{
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, 1);
  Serial.begin(115200);
  setup_FS();
  EEPROM.begin(512);
  RemContr_Add = EEprom_Read2Bytes(0);
  setup_spi();

  setup_wifi();
  delay(500);

  //setup_OTA();
  setup_Lt8900();
  //setup_baro();
  setup_web();
  Serial.println("Boot completed.");
  Serial.print("Using Remote: ");Serial.println(RemContr_Add);
}

void loop()
{
   if (WiFi.status() != WL_CONNECTED) ESP.reset();
   while (!client.connected())
   {
     if (client.connect(host_name.c_str(),"Remote/Status/Online",0,false,"0"))
     {
       FlashLed(5, 100);
       Serial.println("MQTT Online!");
       String Out_Topic = "Remote/Status/Online";
       client.publish(Out_Topic.c_str(),"1",true);
       client.setCallback(callback);
       client.subscribe("Remote/Raw");

       lt.softReset();
       lt.begin();
       lt.setChannel(74);
       lt.startListening();
     } else
     {
       delay(1000);
     }
   }
   if (lt.available())
   {
     //Serial.print("Incoming Remote Command");
     uint8_t buf[20];
     int packetSize = lt.read(buf, 20);
     
     if (packetSize > 0)
     {
       
       /* Serial.print(packetSize);
       Serial.print("   ");
       for (int i = 0; i < packetSize; i++)
       {

         Serial.print(buf[i]);
       } 
       Serial.println(); */
       if (buf[0]!=90) client.publish("Remote/RawIn",buf,packetSize);
       Publish_Remote(buf);
     }
     /*else
       {
         Serial.print("Error! "); 
         Serial.println(packetSize);
         
       }*/
       lt.startListening(); // LT8900 Rx Enable
   }
  client.loop();                // Update MQTT client
  //server.handleClient();
}
