#include <PubSubClient.h>
#include "arduino.h""
#include <WiFiUdp.h>
#include <WiFiServer.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <ESP8266WiFiType.h>
#include <ESP8266WiFiSTA.h>
#include <ESP8266WiFiScan.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WiFiGeneric.h>
#include <ESP8266WiFiAP.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <SoftwareSerial.h>  


#define DEBUG

//Kamstrup setup
// Kamstrup 382Jx3
word const kregnums[] = { 0x0001,0x03ff,0x0027,0x041e,0x041f,0x0420 };
char* kregstrings[] = { "Energy_in","Current_Power","Max_Power","Voltage_p1","Voltage_p2","Voltage_p3" };
#define NUMREGS 6 		// Number of registers above
#define KAMBAUD 9600

IPAddress ip(192, 168, 1, 214);
IPAddress gw(192, 168, 1, 1);
IPAddress mask(255, 255, 255, 0);

WiFiClient ethClient;

IPAddress MQTTServer(192, 168, 1, 101);
PubSubClient MQTTclient(ethClient);

//char ssid[] = "Kjeldsen_Device";
char ssid[] = "Kjeldsen";
char password[] = "donnafrida";

ADC_MODE(ADC_VCC);

// Units
char*  units[65] = { "","Wh","kWh","MWh","GWh","j","kj","Mj",
"Gj","Cal","kCal","Mcal","Gcal","varh","kvarh","Mvarh","Gvarh",
"VAh","kVAh","MVAh","GVAh","kW","kW","MW","GW","kvar","kvar","Mvar",
"Gvar","VA","kVA","MVA","GVA","V","A","kV","kA","C","K","l","m3",
"l/h","m3/h","m3xC","ton","ton/h","h","hh:mm:ss","yy:mm:dd","yyyy:mm:dd",
"mm:dd","","bar","RTC","ASCII","m3 x 10","ton xr 10","GJ x 10","minutes","Bitfield",
"s","ms","days","RTC-Q","Datetime" };

// Pin definitions
#define PIN_KAMSER_RX  D3  // Kamstrup IR interface RX
#define PIN_KAMSER_TX  D4  // Kamstrup IR interface TX
#define LED			   D5
// Kamstrup optical IR serial
// #define KAMTIMEOUT 300  // Kamstrup timeout after transmit
#define KAMTIMEOUT 1000  // Kamstrup timeout after transmit
SoftwareSerial kamSer(PIN_KAMSER_RX, PIN_KAMSER_TX, false);  // Initialize serial


void setup() {
	ulong start = millis();

	//	ESP.system_deep_sleep_set_option(WAKE_NO_RFCAL);
	pinMode(LED, OUTPUT);
	digitalWrite(LED, LOW);
	Serial.begin(115200);
	delay(1000);
	Serial.println();
	Serial.print("Starting up KAM...");
	// setup kamstrup serial
	pinMode(PIN_KAMSER_RX, INPUT);
	pinMode(PIN_KAMSER_TX, OUTPUT);
	kamSer.begin(KAMBAUD);

	Serial.println(" Done");

	//Serial.println("\n[testKamstrup]");
	// poll the Kamstrup registers for data 
	//for (int kreg = 0; kreg < NUMREGS; kreg++) {
	//	kamReadReg(kreg);
	//	delay(100);
	//}
	Serial.print("Starting up WIFI...");
	WiFi.mode(WIFI_STA);
//	WiFi.config(ip, gw, mask);
	WiFi.begin(ssid, password);
	Serial.print("Started...waiting for connect...");

	while (WiFi.status() != WL_CONNECTED) {
		delay(250);
		Serial.print(".");
	}
	Serial.println("Done");


	Serial.print("Starting up MQTT");
	String IP = WiFi.localIP().toString();
	MQTTclient.setServer(MQTTServer, 1883);

	while (!MQTTclient.connected())
	{
		Serial.print(".");
		// Attempt to connect
		if (MQTTclient.connect(IP.c_str()))
		{
			Serial.println("connected");
			// Once connected, publish an announcement...
		}
		delay(10);
	}


	Serial.print("WiFi connected IP address: ");
	Serial.println(WiFi.localIP());
	Serial.println();
	Serial.println("Start looping...");
#ifdef DEBUG 
	MQTTclient.publish("KNet/Elmaaler/DEBUG", "Start Looping", false);
#endif


	digitalWrite(LED, HIGH);
	
	while (true)
	{

		// poll the Kamstrup registers for data 
		for (int kreg = 0; kreg < NUMREGS; kreg++) {
			kamReadReg(kreg);
			delay(100);
		}

		char s[20];
		uint16_t v = ESP.getVcc();
		sprintf(s, "%d.%03d", v / 1000, v % 1000);

		MQTTclient.publish("KNet/Elmaaler/Battery", s, true);
		itoa(millis() - start, s, 10);
		MQTTclient.publish("KNet/Elmaaler/SendTime", s, true);
		digitalWrite(PIN_KAMSER_TX, HIGH);

		//	ESP.deepSleep(5000000, WAKE_NO_RFCAL);
		delay(5000);
		digitalWrite(LED, HIGH);
	}
}
void loop() 
{
}



// kamReadReg - read a Kamstrup register
float kamReadReg(unsigned short kreg) {

	byte recvmsg[40];  // buffer of bytes to hold the received data
	float rval;        // this will hold the final value
	char t[255];         // String to hold the MQTT Tropic
	char s[40];	       // String to hold the MQTT Value 

					   // prepare message to send and send it
	byte sendmsg[] = { 0x3f, 0x10, 0x01, (kregnums[kreg] >> 8), (kregnums[kreg] & 0xff) };
	kamSend(sendmsg, 5);

	// listen if we get an answer
	unsigned short rxnum = kamReceive(recvmsg);

	// check if number of received bytes > 0 
	if (rxnum != 0) 
	{
		digitalWrite(LED, LOW);
		// decode the received message
		rval = kamDecode(kreg, recvmsg);

		sprintf(t, "KNet/Elmaaler/EL/%s", kregstrings[kreg]);
		dtostrf(rval, 8, 4, s);

		Serial.print(t); Serial.print(" = "); Serial.println(s);
		MQTTclient.publish(t, s, true);

		return rval;
	}
}

// kamSend - send data to Kamstrup meter
void kamSend(byte const *msg, int msgsize) {

	// append checksum bytes to message
//	byte newmsg[msgsize + 2];
	byte newmsg[1024];
	for (int i = 0; i < msgsize; i++) { newmsg[i] = msg[i]; }
	newmsg[msgsize++] = 0x00;
	newmsg[msgsize++] = 0x00;
	int c = crc_1021(newmsg, msgsize);
	newmsg[msgsize - 2] = (c >> 8);
	newmsg[msgsize - 1] = c & 0xff;

	// build final transmit message - escape various bytes
	byte txmsg[20] = { 0x80 };   // prefix
	int txsize = 1;
	for (int i = 0; i < msgsize; i++) {
		if (newmsg[i] == 0x06 or newmsg[i] == 0x0d or newmsg[i] == 0x1b or newmsg[i] == 0x40 or newmsg[i] == 0x80) {
			txmsg[txsize++] = 0x1b;
			txmsg[txsize++] = newmsg[i] ^ 0xff;
		}
		else {
			txmsg[txsize++] = newmsg[i];
		}
	}
	txmsg[txsize++] = 0x0d;  // EOF

							 // send to serial interface
	for (int x = 0; x < txsize; x++) {
		kamSer.write(txmsg[x]);
	}

}

// kamReceive - receive bytes from Kamstrup meter
unsigned short kamReceive(byte recvmsg[]) {

	byte rxdata[50];  // buffer to hold received data
	unsigned long rxindex = 0;
	unsigned long starttime = millis();

	kamSer.flush();  // flush serial buffer - might contain noise

	byte r;
	// loop until EOL received or timeout
	while (r != 0x0d) {

		// handle rx timeout
		if (millis() - starttime > KAMTIMEOUT) {
			Serial.println("Timed out listening for data");
#ifdef DEBUG 
			MQTTclient.publish("KNet/Elmaaler/DEBUG", "Timed out listening for data", false);
#endif

			return 0;
		}

		// handle incoming data
		if (kamSer.available()) {
			// receive byte
			r = kamSer.read();
#ifdef DEBUG 
			char s[20];
			MQTTclient.publish("KNet/Elmaaler/DEBUG/Received",  itoa(r, s, 10), false);
#endif
			if (r != 0x40) {  // don't append if we see the start marker
							  // append data
				rxdata[rxindex] = r;
				rxindex++;
			}

		}
	}

	// remove escape markers from received data
	unsigned short j = 0;
	for (unsigned short i = 0; i < rxindex - 1; i++) {
		if (rxdata[i] == 0x1b) {
			byte v = rxdata[i + 1] ^ 0xff;
			if (v != 0x06 and v != 0x0d and v != 0x1b and v != 0x40 and v != 0x80) {
				{
					Serial.print("Missing escape ");
#ifdef DEBUG 
					MQTTclient.publish("KNet/Elmaaler/DEBUG", "Missing escape", false);
#endif

				}
				Serial.println(v, HEX);
			}
			recvmsg[j] = v;
			i++; // skip
		}
		else {
			recvmsg[j] = rxdata[i];
		}
		j++;
	}

	// check CRC
	if (crc_1021(recvmsg, j)) {
		Serial.println("CRC error: ");
#ifdef DEBUG 
		MQTTclient.publish("KNet/Elmaaler/DEBUG", "CDC Error", false);
#endif
		return 0;
	}

	return j;

}

// kamDecode - decodes received data
float kamDecode(unsigned short const kreg, byte const *msg) {

	// skip if message is not valid
	if (msg[0] != 0x3f or msg[1] != 0x10) {
		return false;
	}
	if (msg[2] != (kregnums[kreg] >> 8) or msg[3] != (kregnums[kreg] & 0xff)) {
		return false;
	}

	// decode the mantissa
	long x = 0;
	for (int i = 0; i < msg[5]; i++) {
		x <<= 8;
		x |= msg[i + 7];
	}

	// decode the exponent
	int i = msg[6] & 0x3f;
	if (msg[6] & 0x40) {
		i = -i;
	};
	float ifl = pow(10, i);
	if (msg[6] & 0x80) {
		ifl = -ifl;
	}

	// return final value
	return (float)(x * ifl);

}

// crc_1021 - calculate crc16
long crc_1021(byte const *inmsg, unsigned int len) {
	long creg = 0x0000;
	for (unsigned int i = 0; i < len; i++) {
		int mask = 0x80;
		while (mask > 0) {
			creg <<= 1;
			if (inmsg[i] & mask) {
				creg |= 1;
			}
			mask >>= 1;
			if (creg & 0x10000) {
				creg &= 0xffff;
				creg ^= 0x1021;
			}
		}
	}
	return creg;
}