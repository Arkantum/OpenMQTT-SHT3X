//////----------Declaration Librarie----------//////

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <AsyncElegantOTA.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include "SHTSensor.h"

extern "C"
{
  uint8_t temprature_sens_read();
}

const char *ssid = "OpenMQTT";
const char *password = "";

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

const int Seconde = 1000;
const int Minute = 60 * Seconde;
const int Heure = 60 * Minute;

String TempsActuel = "00:00:00";
String HeureActuel = "00";
String MinuteActuel = "00";
String SecondeActuel = "00";

int resultHeure;
int resultMinute;
int resultSeconde;

char data[100];

const char *fileconfig = "/config/config.json"; //config file
const size_t capacityConfig = 2 * JSON_ARRAY_SIZE(1000);

AsyncWebServer server(80);
DNSServer dns;
AsyncWiFiManager wifiManager(&server, &dns);
WiFiClientSecure client;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

SHTSensor sht;

UniversalTelegramBot *bot;

File root = SPIFFS.open("/");
File file = root.openNextFile();

struct StructConfig
{
  String mqttServer;
  int mqttPort = 1883;

  String hostname = "Mycodo MQTT";

  String Telegram_ID_1;
  String Telegram_ID_2;
  String Telegram_ID_3;
  String Telegram_ID_4;
  String Telegram_ID_5;
  String Telegram_TOKEN;

  String Topic_SHT3X_Temperature;
  String Topic_SHT3X_Humidity;
  String Topic_SHT3X_VPD;
  String Topic_SHT3X_DewPoint;
};

StructConfig Configuration;

const char *INPUT_mqttip = "mqttip";
const char *INPUT_hostname = "hostname";
const char *INPUT_telegramID1 = "telegramID1";
const char *INPUT_telegramID2 = "telegramID2"; //get value html
const char *INPUT_telegramID3 = "telegramID3";
const char *INPUT_telegramID4 = "telegramID4";
const char *INPUT_telegramID5 = "telegramID5";
const char *INPUT_telegramToken = "telegramToken";
const char *INPUT_Topic_SHT3X_Temperature = "SHT3XTemperature";
const char *INPUT_Topic_SHT3X_Humidity = "SHT3XHumidity";
const char *INPUT_Topic_SHT3X_VPD = "SHT3XVPD";
const char *INPUT_Topic_SHT3X_DewPoint = "SHT3XDewPoint";

float Temperature_Value;
float Humidity_Value;
float VPD_Value;
float DewPoint_Value;

unsigned long TestRoutinePrecedente = 0;
const long IntervalRoutine = 5000;

void setupMQTT()
{
  mqttClient.setServer(Configuration.mqttServer.c_str(), Configuration.mqttPort);
}

void reconnect()
{
  Serial.println("Connecting to MQTT Broker...");
  if (!mqttClient.connected())
  {
    Serial.println("Reconnecting to MQTT Broker..");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str()))
    {
      Serial.println("Connected.");
    }
  }
}

void ActualisationTempsServeur()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    TempsActuel = "Erreur temps !";
    return;
  }
  HeureActuel = timeinfo.tm_hour;
  if (timeinfo.tm_hour < 10)
  {
    HeureActuel = "0" + HeureActuel;
  }
  MinuteActuel = timeinfo.tm_min;
  if (timeinfo.tm_min < 10)
  {
    MinuteActuel = "0" + MinuteActuel;
  }
  SecondeActuel = timeinfo.tm_sec;
  if (timeinfo.tm_sec < 10)
  {
    SecondeActuel = "0" + SecondeActuel;
  }
  TempsActuel = HeureActuel + ":" + MinuteActuel + ":" + SecondeActuel;
  resultHeure = HeureActuel.toInt();
  resultMinute = MinuteActuel.toInt();
  resultSeconde = SecondeActuel.toInt();
}

void playSuccess()
{
  Serial.println("Success !");
}

void Message_Recu(int NombreMessagesRecu)
{
  for (int i = 0; i < NombreMessagesRecu; i++)
  {
    // Chat id of the requester
    String chat_id = String(bot->messages[i].chat_id);
    if (chat_id != Configuration.Telegram_ID_1 && chat_id != Configuration.Telegram_ID_2 && chat_id != Configuration.Telegram_ID_3 && chat_id != Configuration.Telegram_ID_4 && chat_id != Configuration.Telegram_ID_5)
    {
      bot->sendMessage(chat_id, "Utilisateur non enregistrée", "");
      continue;
    }

    // Print the received message
    String text = bot->messages[i].text;
    Serial.println(text);

    String from_name = bot->messages[i].from_name;

    if (text == "/start")
    {
      String keyboardJson = "[[\"/Info\", \"/Recap\"]]";
      bot->sendMessageWithReplyKeyboard(chat_id, "Que voulez vous ?", "", keyboardJson, true);
    }

    if (text == "/Recap")
    {
      String Recap = "Recap : \n\n";
      Recap += "Température   :  " + String(Temperature_Value) + " °C \n";
      Recap += "Humidité      :  " + String(Humidity_Value) + " % \n";
      Recap += "Vapor Pressure Deficit :  " + String(VPD_Value) + "\n";
      Recap += "DewPoint :  " + String(DewPoint_Value) + "\n";
      bot->sendMessage(chat_id, Recap, "");
    }

    if (text == "/Info")
    {
      String Info = "Informations : \n\n";
      Info += "Adresse IP locale : " + String(WiFi.localIP()) + " \n";
      Info += "Adresse MAC locale : " + String(WiFi.macAddress()) + " \n";
      Info += "Adresse IP Mycodo : " + String(Configuration.mqttServer) + " \n";
      Info += "Hostname : " + String(Configuration.hostname) + " \n";
      bot->sendMessage(chat_id, Info, "");
    }
  }
}

void playFailed()
{
  Serial.println("Failed !");
}

void Chargement()
{
  File file = SPIFFS.open(fileconfig, "r");
  if (!file)
  {
    //fichier config absent
    Serial.println("Fichier Config absent");
  }
  DynamicJsonDocument docConfig(capacityConfig);
  DeserializationError err = deserializeJson(docConfig, file);
  if (err)
  {
    Serial.print(F("deserializeJson() avorté, problème rencontré : "));
    Serial.println(err.c_str());
  }
  Configuration.mqttServer = docConfig["mqttServer"] | "";
  Configuration.hostname = docConfig["hostname"] | "";
  Configuration.Telegram_ID_1 = docConfig["Telegram_ID_1"] | "";
  Configuration.Telegram_ID_2 = docConfig["Telegram_ID_2"] | "";
  Configuration.Telegram_ID_3 = docConfig["Telegram_ID_3"] | "";
  Configuration.Telegram_ID_4 = docConfig["Telegram_ID_4"] | "";
  Configuration.Telegram_ID_5 = docConfig["Telegram_ID_5"] | "";
  Configuration.Telegram_TOKEN = docConfig["Telegram_TOKEN"] | "";
  Configuration.Topic_SHT3X_Temperature = docConfig["Topic_SHT3X_Temperature"] | "";
  Configuration.Topic_SHT3X_Humidity = docConfig["Topic_SHT3X_Humidity"] | "";
  Configuration.Topic_SHT3X_VPD = docConfig["Topic_SHT3X_VPD"] | "";
  Configuration.Topic_SHT3X_DewPoint = docConfig["Topic_SHT3X_DewPoint"] | "";
  file.close();
  bot = new UniversalTelegramBot(Configuration.Telegram_TOKEN, client);
}

void Sauvegarde()
{
  String jsondoc = "";
  DynamicJsonDocument docConfig(capacityConfig);
  docConfig["mqttServer"] = Configuration.mqttServer;
  docConfig["hostname"] = Configuration.hostname;
  docConfig["Telegram_ID_1"] = Configuration.Telegram_ID_1;
  docConfig["Telegram_ID_2"] = Configuration.Telegram_ID_2;
  docConfig["Telegram_ID_3"] = Configuration.Telegram_ID_3;
  docConfig["Telegram_ID_4"] = Configuration.Telegram_ID_4;
  docConfig["Telegram_ID_5"] = Configuration.Telegram_ID_5;
  docConfig["Telegram_TOKEN"] = Configuration.Telegram_TOKEN;
  docConfig["Topic_SHT3X_Temperature"] = Configuration.Topic_SHT3X_Temperature;
  docConfig["Topic_SHT3X_Humidity"] = Configuration.Topic_SHT3X_Humidity;
  docConfig["Topic_SHT3X_VPD"] = Configuration.Topic_SHT3X_VPD;
  docConfig["Topic_SHT3X_DewPoint"] = Configuration.Topic_SHT3X_DewPoint;
  File f = SPIFFS.open(fileconfig, "w");
  if (!f)
  {
    Serial.println("Fichier Config absent - Création du fichier");
  }
  serializeJson(docConfig, jsondoc);
  f.print(jsondoc); //save in spiffs
  f.close();
  Serial.println(jsondoc);
  bot = new UniversalTelegramBot(Configuration.Telegram_TOKEN, client);
  playSuccess();
}

void initWiFi()
{
  WiFi.setHostname(Configuration.hostname.c_str()); //define hostname
  wifiManager.autoConnect(ssid, password);          // create an host if the network is not finded

  Serial.println(WiFi.localIP());

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer); //GMT TO FRANCE

  server.begin();
}

void initSensor()
{
  if (Configuration.Topic_SHT3X_Temperature != "" || Configuration.Topic_SHT3X_Humidity != "" || Configuration.Topic_SHT3X_VPD != "" || Configuration.Topic_SHT3X_DewPoint != "")
  {
    if (sht.init())
    {
      Serial.print("init(): success init of the sht3X\n");
    }
    else
    {
      Serial.print("init(): failed init of the sht3X\n");
    }
    sht.setAccuracy(SHTSensor::SHT_ACCURACY_MEDIUM); // only supported by SHT3x
  }
}

void LoopSensor()
{
  if (!sht.readSample())
  {
    Serial.print("Error in readSample() for sht3x\n");
  }

  sht.readSample();

  if (Configuration.Topic_SHT3X_Temperature != "")
  {
    sprintf(data, "%f", sht.getTemperature());
    mqttClient.publish(Configuration.Topic_SHT3X_Temperature.c_str(), data);
  }

  if (Configuration.Topic_SHT3X_Humidity != "")
  {
    sprintf(data, "%f", sht.getHumidity());
    mqttClient.publish(Configuration.Topic_SHT3X_Humidity.c_str(), data);
  }

  if (Configuration.Topic_SHT3X_VPD != "")
  {
    float T = sht.getTemperature();
    sprintf(data, "%f", 0.6108 * exp(17.27 * T / (T + 237.3)));
    mqttClient.publish(Configuration.Topic_SHT3X_VPD.c_str(), data);
  }

  if (Configuration.Topic_SHT3X_DewPoint != "")
  {
    float T = sht.getTemperature();
    float H = sht.getHumidity();
    sprintf(data, "%f", T - ((100 - H) / 5));
    mqttClient.publish(Configuration.Topic_SHT3X_DewPoint.c_str(), data);
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.print("\n");

  SPIFFS.begin();

  Wire.begin();

  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

  if (!SPIFFS.begin())
  {
    Serial.println("Erreur SPIFFS...");
    return;
  }

  while (file)
  {
    Serial.print("Fichier : ");
    Serial.println(file.name());
    file.close();
    file = root.openNextFile();
  }

  Chargement();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/index.html", "text/html"); });

  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/favicon.ico", "image/ico"); });

  server.on("/js/scheduler.js", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/js/scheduler.js", "text/javascript"); });

  server.on("/js/jquery-3.5.1.min.js", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/js/jquery-3.5.1.min.js", "text/javascript"); });

  server.on("/css/doc.css", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/css/doc.css", "text/css"); });

  server.on("/css/scheduler.css", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/css/scheduler.css", "text/css"); });

  server.on("/config/config.json", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/config/config.json", "application/json"); });

  server.on("/Temps", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plain", TempsActuel); });

  server.on("/InternalTemp", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plain", "Internal Temperature : " + String((temprature_sens_read() - 32) / 1.8) + " °C"); });

  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              if (request->hasParam(INPUT_mqttip))
              {
                Configuration.mqttServer = String(request->getParam(INPUT_mqttip)->value());
              }
              else if (request->hasParam(INPUT_hostname))
              {
                Configuration.hostname = String(request->getParam(INPUT_hostname)->value());
              }

              else if (request->hasParam(INPUT_telegramID1))
              {
                Configuration.Telegram_ID_1 = request->getParam(INPUT_telegramID1)->value();
              }
              else if (request->hasParam(INPUT_telegramID2))
              {
                Configuration.Telegram_ID_2 = request->getParam(INPUT_telegramID2)->value();
              }
              else if (request->hasParam(INPUT_telegramID3))
              {
                Configuration.Telegram_ID_3 = request->getParam(INPUT_telegramID3)->value();
              }
              else if (request->hasParam(INPUT_telegramID4))
              {
                Configuration.Telegram_ID_4 = request->getParam(INPUT_telegramID4)->value();
              }
              else if (request->hasParam(INPUT_telegramID5))
              {
                Configuration.Telegram_ID_5 = request->getParam(INPUT_telegramID5)->value();
              }
              else if (request->hasParam(INPUT_telegramToken))
              {
                Configuration.Telegram_TOKEN = request->getParam(INPUT_telegramToken)->value();
              }

              else if (request->hasParam(INPUT_Topic_SHT3X_Temperature))
              {
                Configuration.Topic_SHT3X_Temperature = request->getParam(INPUT_Topic_SHT3X_Temperature)->value();
              }
              else if (request->hasParam(INPUT_Topic_SHT3X_Humidity))
              {
                Configuration.Topic_SHT3X_Humidity = request->getParam(INPUT_Topic_SHT3X_Humidity)->value();
              }
              else if (request->hasParam(INPUT_Topic_SHT3X_VPD))
              {
                Configuration.Topic_SHT3X_VPD = request->getParam(INPUT_Topic_SHT3X_VPD)->value();
              }
              else if (request->hasParam(INPUT_Topic_SHT3X_DewPoint))
              {
                Configuration.Topic_SHT3X_DewPoint = request->getParam(INPUT_Topic_SHT3X_DewPoint)->value();
              }
              else
              {
                Serial.println("/get failed");
                playFailed();
              }
              Sauvegarde();
              request->send(204);
            });

  initWiFi();
  initSensor();

  setupMQTT();

  AsyncElegantOTA.begin(&server);
}

void loop()
{
  ActualisationTempsServeur();

  AsyncElegantOTA.loop();
  if (Configuration.mqttServer != "")
  {
    mqttClient.loop();
    if (!mqttClient.connected())
    {
      reconnect();
    }
  }

  if (resultHeure == 0 && resultMinute == 0 && resultSeconde == 0)
  {
    ESP.restart();
  }

  if (millis() > IntervalRoutine + TestRoutinePrecedente)
  {
    int NombreMessagesRecu = bot->getUpdates(bot->last_message_received + 1);
    while (NombreMessagesRecu)
    {
      Message_Recu(NombreMessagesRecu);
      NombreMessagesRecu = bot->getUpdates(bot->last_message_received + 1);
    }
    LoopSensor();
    TestRoutinePrecedente = millis();
  }

  delay(100);
}