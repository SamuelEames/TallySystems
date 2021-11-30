
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED}; 			// MAC address of this Ethernet Shield.
EthernetUDP Udp;
IPAddress ip(10, 1, 100, 15); 													// static IP address that Arduino uses to connect to LAN.
IPAddress multicastip(224, 0, 168, 168); 								// the Multicast IP to listen on.

unsigned int multicastport = 8903; 											// the Multicast Port to listen on.
char packetBuffer[UDP_TX_PACKET_MAX_SIZE]; 							// buffer to hold incoming packet.

void setup() 
{
	// Setup Ethernet
	Ethernet.begin(mac, ip);
	Udp.beginMulticast(multicastip, multicastport);



	Serial.begin(115200);
	Serial.print ("Starting to Listen for UDP Multicast Traffic on IP: ");
	Serial.print (multicastip);
	Serial.print (" Port: ");
	Serial.print (multicastport);

}

uint8_t packetSize; 	 	// Holds size of last UDP message


void loop() 
{

	packetSize = Udp.parsePacket();

	if(packetSize)
	{
		// UDP_InfoDump();

		Udp.read(packetBuffer,UDP_TX_PACKET_MAX_SIZE); 		// read the packet into packetBufffer

		// UDP_PacketDump();

		unpackTSLTally();
	}
}


void UDP_InfoDump()
{
	// Prints message info to console
	Serial.println();
	Serial.print("Received packet of size ");
	Serial.println(packetSize);
	Serial.print("From ");
	IPAddress remote = Udp.remoteIP();
	for (uint8_t i =0; i < 4; i++)
	{
		Serial.print(remote[i], DEC);
		if (i < 3)
			Serial.print(".");
	}
	Serial.print(", port ");
	Serial.println(Udp.remotePort());

	return;
}


void UDP_PacketDump()
{
	// Prints received UDP data to console

	Serial.print("Contents:");
	Serial.println(packetBuffer);

	Serial.print("HEX = ");
	for (uint8_t i = 0; i < packetSize; ++i)
	{
		Serial.print(packetBuffer[i], HEX);
		Serial.print(" ");
	}
	
	Serial.println();

	return;
}




void unpackTSLTally()
{

	uint8_t TallyNum = 0;
	bool TallyPrev = false;
	bool TallyProg = false;

	if (packetSize == 18)
	{
		// Get Tally Number
		TallyNum = packetBuffer[0] - 0x80;

		Serial.print("Tally Number ");
		Serial.print(TallyNum, DEC);



		// Get Preview State
		TallyPrev = packetBuffer[1] & 0x01;

		// Get Program State
		TallyProg = (packetBuffer[1] >> 1) & 0x01;

		if (TallyPrev)
			Serial.print(" PREVIEW");

		if (TallyProg)
			Serial.print(" PROGRAM");

		Serial.println();

		// Print name
		Serial.print("DisplayData = ");
		for (uint8_t i = 2; i < 18; ++i)
			Serial.write(packetBuffer[i]);

		Serial.println();
		
	}
}