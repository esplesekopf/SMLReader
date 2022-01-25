#include <list>
#include "config.h"
#include "debug.h"
#include <sml/sml_file.h>
#include "Sensor.h"
#include <IotWebConf.h>
#include "MqttPublisher.h"
#include "EEPROM.h"
#include <ESP8266WiFi.h>
#include <jled.h>

#define ACTION_FEQ_LIMIT 10000
#define NO_ACTION -1

std::list<Sensor*> *sensors = new std::list<Sensor*>();

void wifiConnected();
void configSaved();
void applyAction(unsigned long now);
void handleSMLLED();

#define smlLEDPin 15 //SMLLED = Sendediode

DNSServer dnsServer;
WebServer server(80);
HTTPUpdateServer httpUpdater;
WiFiClient net;

MqttConfig mqttConfig;
MqttPublisher publisher;

IotWebConf iotWebConf(WIFI_AP_SSID, &dnsServer, &server, WIFI_AP_DEFAULT_PASSWORD, CONFIG_VERSION);
IotWebConfParameter params[] = {
	IotWebConfParameter("MQTT server", "mqttServer", mqttConfig.server, sizeof(mqttConfig.server), "text", NULL, mqttConfig.server, NULL, true),
	IotWebConfParameter("MQTT port", "mqttPort", mqttConfig.port, sizeof(mqttConfig.port), "text", NULL, mqttConfig.port, NULL, true),
	IotWebConfParameter("MQTT username", "mqttUsername", mqttConfig.username, sizeof(mqttConfig.username), "text", NULL, mqttConfig.username, NULL, true),
	IotWebConfParameter("MQTT password", "mqttPassword", mqttConfig.password, sizeof(mqttConfig.password), "password", NULL, mqttConfig.password, NULL, true),
	IotWebConfParameter("MQTT topic", "mqttTopic", mqttConfig.topic, sizeof(mqttConfig.topic), "text", NULL, mqttConfig.topic, NULL, true)};

boolean needReset = false;
boolean connected = false;

//LED Sequence for IR LED
JLed ledsequence[50]={
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop(),
	JLed(smlLEDPin).Stop()
};
int countirled=-1;
int pointerirled=-1;
//IR LED OFF, LED is INVERT! = .LowActive()
JLed led=JLed(smlLEDPin).Off().LowActive();


void process_message(byte *buffer, size_t len, Sensor *sensor)
{
	// Parse
	sml_file *file = sml_file_parse(buffer + 8, len - 16);

	DEBUG_SML_FILE(file);

	publisher.publish(sensor, file);

	// free the malloc'd memory
	sml_file_free(file);
}

void setup()
{
//IR LED
pinMode(smlLEDPin,OUTPUT);
digitalWrite(smlLEDPin,HIGH);

	// Setup debugging stuff
	SERIAL_DEBUG_SETUP(115200);

#ifdef DEBUG
	// Delay for getting a serial console attached in time
	delay(2000);
#endif

	// Setup reading heads
	DEBUG("Setting up %d configured sensors...", NUM_OF_SENSORS);
	const SensorConfig *config  = SENSOR_CONFIGS;
	for (uint8_t i = 0; i < NUM_OF_SENSORS; i++, config++)
	{
		Sensor *sensor = new Sensor(config, process_message);
		sensors->push_back(sensor);
	}
	DEBUG("Sensor setup done.");

	// Initialize publisher
	// Setup WiFi and config stuff
	DEBUG("Setting up WiFi and config stuff.");

	for (uint8_t i = 0; i < sizeof(params) / sizeof(params[0]); i++)
	{
		DEBUG("Adding parameter %s.", params[i].label);
		iotWebConf.addParameter(&params[i]);
	}
	iotWebConf.setConfigSavedCallback(&configSaved);
	iotWebConf.setWifiConnectionCallback(&wifiConnected);
	iotWebConf.setupUpdateServer(&httpUpdater);

	boolean validConfig = iotWebConf.init();
	if (!validConfig)
	{
		DEBUG("Missing or invalid config. MQTT publisher disabled.");
		MqttConfig defaults;
		// Resetting to default values
		strcpy(mqttConfig.server, defaults.server);
		strcpy(mqttConfig.port, defaults.port);
		strcpy(mqttConfig.username, defaults.username);
		strcpy(mqttConfig.password, defaults.password);
		strcpy(mqttConfig.topic, defaults.topic);
	}
	else
	{
		// Setup MQTT publisher
		publisher.setup(mqttConfig);
	}

	server.on("/", [] { iotWebConf.handleConfig(); });
	server.on("/code", handleSMLLED);
	server.onNotFound([]() { iotWebConf.handleNotFound(); });

	DEBUG("Setup done.");
    
	
}

void loop()
{
	//Processing LED Array
	if (led.Update() == true)
	{
	}
	else if (led.Update() == false)
	{
		if (countirled != -1)
		{
			pointerirled += 1;
			if (pointerirled<countirled)
			{
				led = ledsequence[pointerirled];
				
			}
			else 
			{
				countirled = -1;
				pointerirled = -1;
			}
		}	
	}
	if (needReset)
	{
		// Doing a chip reset caused by config changes
		DEBUG("Rebooting after 1 second.");
		delay(1000);
		ESP.restart();
	}

	// Execute sensor state machines
	for (std::list<Sensor*>::iterator it = sensors->begin(); it != sensors->end(); ++it){
		(*it)->loop();
	}
	iotWebConf.doLoop();
	yield();
	
	


}

void configSaved()
{
	DEBUG("Configuration was updated.");
	needReset = true;
}

void wifiConnected()
{
	DEBUG("WiFi connection established.");
	connected = true;
	publisher.connect();
}




void handleSMLLED()
{
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal())
  {
    // -- Captive portal request were already served.
    return;
  }

  if (server.hasArg("ledstate"))
  {
    String ledstate = server.arg("ledstate");
    pointerirled=-1;
	countirled=-1;
	if (ledstate.equals("on"))
    {
	  //digitalWrite(smlLEDPin, !HIGH); //LED AN
	  JLed(smlLEDPin).On().LowActive().Update();
	  DEBUG("LED on");
    }
    else if (ledstate.equals("off"))
    {
	  //digitalWrite(smlLEDPin, !LOW); //LED AUS
	  JLed(smlLEDPin).Off().LowActive().Update();
	  DEBUG("LED off");
    }
	else
	{
		DEBUG("handleSMLLED");
		int i;
		int ledi=0;
		for (i=0; ledstate[i];i++)
		{
			
			if (ledstate[i] == 'P')
			{
				ledsequence[ledi]=JLed(smlLEDPin).Off().DelayAfter(1000).LowActive();
				ledi ++;

			} 
			else if (ledstate[i] == 'p')
			{
				ledsequence[ledi]=JLed(smlLEDPin).Off().DelayAfter(500).LowActive();
				ledi ++;
			}
			else if (ledstate[i] == 'L')
			{
				ledsequence[ledi]=JLed(smlLEDPin).Blink(5000,1).LowActive();
				ledi ++;
			}
			else if (ledstate[i] == 'C')
			{
				ledsequence[ledi]=JLed(smlLEDPin).Off().DelayAfter(4000).LowActive();
				ledi ++;
			}
			else if (ledstate[i] == 'c')
			{
				ledsequence[ledi]=JLed(smlLEDPin).Off().DelayAfter(2500).LowActive();
				ledi ++;
			}
			else if (ledstate[i]-'0' == 0)
			{
				ledsequence[ledi]=JLed(smlLEDPin).Off().DelayAfter(200).LowActive();
				ledi ++;
			}
			else if (ledstate[i] == '-')
			{
				ledsequence[ledi]=JLed(smlLEDPin).Off().DelayAfter(100).LowActive();	
				ledi ++;
			}
			else if (int(ledstate[i]-'0') >=1 && int(ledstate[i]-'0')<=9)
			{
				ledsequence[ledi]=JLed(smlLEDPin).Blink(1000,200).Repeat((ledstate[i]-'0')-1).LowActive();
				ledi ++;
				ledsequence[ledi]=JLed(smlLEDPin).Blink(1000,1).Repeat(1).LowActive();
				ledi ++;
			}
			
		}	
		countirled=ledi;
		//DEBUG("%s,%d","Pointer TEST=", countirled);
	
	}
  }
  else if (server.hasArg("reboot"))
  {
	String rebootcode = server.arg("reboot");
	if (rebootcode.equals("now"))
	{
		needReset=true;
	}
  }
  
  String s = F("<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>");
  s += iotWebConf.getHtmlFormatProvider()->getStyle();
  s += "<title>Send LED Sequence</title></head><body>";
  s += iotWebConf.getThingName();
  s += "<div>";
  s += "<button type='button' onclick=\"location.href='?ledstate=on';\" >LED ON</button>";
  s += "<button type='button' onclick=\"location.href='?ledstate=off';\" >LED OFF</button>";
  s += "<button type='button' onclick=\"location.href='?ledstate=1';\" >LED 1000ms ON</button>";
  s += "<button type='button' onclick=\"location.href='?';\" >Refresh</button>";
  s += "<form action=\"/code\">";
  s += "LEDSequenz: <input type=\"text\" name=\"ledstate\" style=\"width:50%;\"/>";
  s += "<input type=\"submit\" value=\"Send LED Sequenz\" style=\"width:25%;\" />";
  s += "</form>";
  s += "<p>P=Wait 1000ms, p=Wait 500ms,- =Wait 100ms, c=Wait 2500ms, C=Wait 3200ms, L=turn LED 5000ms on, 1-9 turns LED on as often as the number specifies for 1000ms with 200ms pause in between.</p>";
  s += "<p>If your PIN is 1234 then the sequenz is 2P1C2C3C4.</p>";
  s += "<p>Note: If the PIN contains a 0 in the second or third position, it may be helpful to enter a c instead of a C after the 0 in the sequence. Also, all waiting characters can be combined. CPp waits 4700ms until the next number.</p>";
  s += "<p>PIN 0011 = 2P0C0c1C1 or PIN 1023 = 2P1C0c3C3 or PIN 0008 = 2P0C0c0c8</p>";
  s += "<p>If all of the above does not work for you, then the code can also be entered manually. Just use the LED 1000ms ON button. Click this button as often as necessary and watch the display of your smartmeter. </p>";
  s += "</div>";
  s += "<div>Go to <a href='/'>configure page</a> to change values.</div>";
  s += "</body></html>\n";

  server.send(200, "text/html", s);
}