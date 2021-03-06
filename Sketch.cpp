﻿/*
 * HeatingController2.ino
 *
 * Created: 1/28/2016 3:29:48 PM
 * Author: Ivanov
 */ 


#include "Sketch.h"

const char PROGMEM MQTT_COMMAND_TOPIC[] = {"ehome/heating/commands"}; //22
const char PROGMEM MQTT_STATUSES_TOPIC[] = { "ehome/heating/statuses" }; //22
const char PROGMEM TOTAL_CONSUMPTION[] = { "total_consumption" };

OneWire  ds(SENSOR);
//CommBuffer serialReadBuffer(COMMAND_BUFFER_LEN);
byte type_s = 0; /*sensor type. Hack to be able to use copypasted code*/
HeaterItem heaterItems[NUMBER_OF_HEATERS];

//byte connectedSensors[NUMBER_OF_HEATERS][8]; /*holds addresses of connected sensors*/
//byte connectedSensorCount = 0; /*Number of connected sensors*/
//byte unconfiguredSensors[NUMBER_OF_HEATERS][8]; /*holds addresses of connected sensors*/
//byte unconfiguredSensorCount = 0; /*Number of disconnected sensors*/
//byte configuredItemsCount = 0;

uint16_t consumptionLimit;
uint16_t totalConsumption;
float hysteresis;

//float temperatures[NUMBER_OF_HEATERS] = {0}; /*holds readings from all sensors*/
	
volatile boolean flagTimer1 = false;
volatile boolean flagTimer2 = false;
volatile boolean mode = MODE_HEATER_ON;

boolean flagEmergency = false;
unsigned long emergencyHandled;

struct EepromDelayedWriteData {
	boolean isWriteInitiated;
	unsigned long writeInitiatedTime;
	uint8_t heaterNumber;
	uint8_t offset;
	} eepromDelayedWriteData;

EthernetClient ethClient;
PubSubClient mqttClient(ethClient);

/*
enum
{
	ADC0,
	TOTAL_REGS_SIZE
};
unsigned int holdingRegs[TOTAL_REGS_SIZE];
*/

void heatersOff(int availablePower, HeaterItem** autoHeaters, int autoHeatersCount, HeaterItem** manualHeaters, int manualHeatersCount) {
	DEBUG_PRINT(F("heatersOff (")); DEBUG_PRINT(availablePower); DEBUG_PRINTLN(F(")"));
	
	/************************************************************************/
	/* Manual heaters                                                       */
	/************************************************************************/
	sortHeaters(manualHeaters, manualHeatersCount); //Sort by priority then by temperature
	
		DEBUG_PRINTLN(F("Processing manual heaters:"));
	
	for (int i=0; i<manualHeatersCount; i++) {
			if (!manualHeaters[i]->wantsOn) { //turn off
				manualHeaters[i]->actualState = false;
				digitalLow(manualHeaters[i]->pin);
				availablePower += manualHeaters[i]->powerConsumption;
				
					printAddress(manualHeaters[i]->address, ADDR_LEN); DEBUG_PRINT(F( " Manual heater ")); DEBUG_PRINT(manualHeaters[i]->port); DEBUG_PRINTLN(F(" set to OFF (by request)."));
				
			}
	}

	/************************************************************************/
	/* Auto heaters                                                         */
	/************************************************************************/	
	sortHeaters(autoHeaters, autoHeatersCount); //Sort by priority then by temperature
	
		DEBUG_PRINTLN(F("Processing auto heaters:"));
		listHeaters(autoHeaters, autoHeatersCount);
	

	for (int i=0; i<autoHeatersCount; i++) { //Process auto heaters normally. Turn off if targetTemp reached.
		if (!autoHeaters[i]->wantsOn) { //Needs to be turned off.
			autoHeaters[i]->actualState = false;
			digitalLow(autoHeaters[i]->pin);
			availablePower += autoHeaters[i]->powerConsumption;
			
				printAddress(autoHeaters[i]->address, ADDR_LEN); DEBUG_PRINT(F(" Auto heater ")); DEBUG_PRINT(autoHeaters[i]->port); DEBUG_PRINTLN(F(" set to OFF (targetTemp reached)."));
			
		}
	}

	/************************************************************************/
	/* Emergency                                                            */
	/************************************************************************/
	if (flagEmergency) {
		//TODO: sort heaters by power consumption
		for (int i=autoHeatersCount; (availablePower < 0) && (i-- > 0);) { //Will run if we didn't free up enough power. Free some more.
			if (autoHeaters[i]->actualState) { //If heater is on, turn it off
				autoHeaters[i]->actualState = false;
				digitalLow(autoHeaters[i]->pin);
				availablePower += autoHeaters[i]->powerConsumption;
				printAddress(autoHeaters[i]->address, ADDR_LEN); DEBUG_PRINT(F(" Auto heater ")); DEBUG_PRINT(autoHeaters[i]->port); DEBUG_PRINTLN(F(" set to OFF (not enough power, EMERGENCY)."));
			}
		}
	
		for (int i=manualHeatersCount; (availablePower < 0) && (i-- > 0);) { //Will run if we didn't free up enough power. Free some more.
			if (manualHeaters[i]->actualState) { //If heater is on, turn it off
				manualHeaters[i]->actualState = false;
				digitalLow(manualHeaters[i]->pin);
				availablePower += manualHeaters[i]->powerConsumption;

			
				printAddress(manualHeaters[i]->address, ADDR_LEN); DEBUG_PRINT(F(" Manual heater ")); DEBUG_PRINT(autoHeaters[i]->port); DEBUG_PRINTLN(F(" set to OFF (not enough power)."));
			
			}
		}
	}
}

void heatersOn(int availablePower, HeaterItem** autoHeaters, int autoHeatersCount, HeaterItem** manualHeaters, int manualHeatersCount) {

	
	DEBUG_PRINT(F("heatersOn (")); DEBUG_PRINT(availablePower); DEBUG_PRINTLN(F(")"));
	
	if (availablePower <= 0) {
		return;
	}
	
	
	/************************************************************************/
	/* Manual heaters                                                       */
	/************************************************************************/
	sortHeaters(manualHeaters, manualHeatersCount);
	
	DEBUG_PRINTLN(F("Processing manual heaters"));
	listHeaters(manualHeaters, manualHeatersCount);
	
	
	for (int i=0; i<manualHeatersCount; i++) {
		if (manualHeaters[i]->wantsOn && !manualHeaters[i]->actualState) { //If needs to be turned on and is still not on
			if (manualHeaters[i]->powerConsumption <= availablePower) { //Can be safely turned on
				availablePower -= manualHeaters[i]->powerConsumption;
				manualHeaters[i]->actualState = true;
				digitalHigh(manualHeaters[i]->pin);
				
				printAddress(manualHeaters[i]->address, ADDR_LEN); DEBUG_PRINT(F(" Manual heater ")); DEBUG_PRINT(manualHeaters[i]->port); DEBUG_PRINTLN(F(" set to ON (by request)."));
				
			} else { //Not enough power, no action
				printAddress(manualHeaters[i]->address, ADDR_LEN); DEBUG_PRINT(F(" Manual heater ")); DEBUG_PRINT(manualHeaters[i]->port); DEBUG_PRINTLN(F(" not set to ON (not enough power)."));
				
			}
		}
	}

	/************************************************************************/
	/* Auto heaters                                                         */
	/************************************************************************/	
	sortHeaters(autoHeaters, autoHeatersCount);
	
	DEBUG_PRINTLN(F("Processing auto heaters."));
	listHeaters(autoHeaters, autoHeatersCount);
	
	for (int i=0; i<autoHeatersCount; i++) {
		if (autoHeaters[i]->wantsOn && !autoHeaters[i]->actualState) { //If needs to be turned on and is still not on
			if (autoHeaters[i]->powerConsumption <= availablePower) { //Can be safely turned on
				availablePower -= autoHeaters[i]->powerConsumption;
				autoHeaters[i]->actualState = true;
				digitalHigh(autoHeaters[i]->pin);
				
				printAddress(autoHeaters[i]->address, ADDR_LEN); DEBUG_PRINT(F(" Auto heater ")); DEBUG_PRINT(autoHeaters[i]->port); DEBUG_PRINTLN(F(" set to ON."));
				
			} else { //Not enough power, no action
			
				printAddress(autoHeaters[i]->address, ADDR_LEN); DEBUG_PRINT(F(" Auto heater ")); DEBUG_PRINT(autoHeaters[i]->port); DEBUG_PRINTLN(F(" not set to ON (not enough power)."));
				
			}
		}
	}
	
	/************************************************************************/
	/* Reporting                                                            */
	/************************************************************************/
	reportTotalConsumption();
	for (int i=0; i<NUMBER_OF_HEATERS; i++) {
		//reportTemp(&heaterItems[i]);
		//reportActualState(&heaterItems[i]);
	}
}

void processHeaters(int currentConsumption, boolean mode) {
	
	DEBUG_PRINTLN(F("==========")); DEBUG_PRINT(F("Current consumption: ")); DEBUG_PRINTLN(currentConsumption);
	DEBUG_MEMORY();
	
	int autoHeatersCount = 0;
	int manualHeatersCount = 0;
	//int heatersConsumption = 0, otherConsumption = 0,
	int availablePower = 0;
	//static int manualHeatersConsumption = 0, autoHeatersConsumption = 0;
	HeaterItem *autoHeaters[NUMBER_OF_HEATERS], *manualHeaters[NUMBER_OF_HEATERS];

	for(int i=0; i<NUMBER_OF_HEATERS; i++) {
		if(heaterItems[i].isEnabled) {
			if (heaterItems[i].isAuto) { //Split heaters into auto and manual
				if (heaterItems[i].isConnected) {
					autoHeaters[autoHeatersCount++] = &heaterItems[i];
					if (heaterItems[i].getDelta() > 0) { //Needs to be turned on
						heaterItems[i].wantsOn = true;
					} else if (heaterItems[i].getDelta() < -hysteresis) { //Needs to be turned off
						heaterItems[i].wantsOn = false;
					}
				} else { /*Turn it off just in case. Heaters with no sensors can't be in auto mode*/
					digitalLow(heaterItems[i].pin);
					heaterItems[i].actualState = false;
					heaterItems[i].isOn = false;
				}
			} else {
				manualHeaters[manualHeatersCount++] = &heaterItems[i];
				heaterItems[i].wantsOn = heaterItems[i].isOn;
			}
		}
	}
	
	availablePower = consumptionLimit - currentConsumption;
	
	if (mode == MODE_HEATER_ON) {
		heatersOn(availablePower, autoHeaters, autoHeatersCount, manualHeaters, manualHeatersCount);
	} else {
		heatersOff(availablePower, autoHeaters, autoHeatersCount, manualHeaters, manualHeatersCount);
	}
}

void timer1_ISR() {
	flagTimer1 = true;
}

void timer2_ISR() {
	flagTimer2 = true;
}

void sortHeaters(HeaterItem **array, int size) {
	int jump = size;
	bool swapped = true;
	HeaterItem *tmp;
	while(jump > 1 || swapped) {
		if (jump > 1)
			jump /= 1.24733;
		swapped = false;
		for (int i=0; i + jump < size; ++i) {
			if(*array[i + jump] > *array[i]) {
				tmp = array[i];
				array[i] = array[i + jump];
				array[i + jump] = tmp;
				swapped = true;
			}
		}	
	}
}

/*
void processSerial()
{
	while(Serial.available())
	{
		int c = Serial.read();
		serialReadBuffer.addChar(c);
	}
}
*/

void detectSensors() {
	DEBUG_PRINTLN(F("Detecting sensors..."));
	DEBUG_MEMORY();
	byte *unconnected[NUMBER_OF_HEATERS], *unconfigured[NUMBER_OF_HEATERS];
	byte addr[8];
	byte connectedSensors[NUMBER_OF_HEATERS][8];

	uint8_t connectedSensorCount = 0, unconfiguredCount = 0, unconnectedCount = 0;
	
	ds.reset_search();
	while (ds.search(addr)) {
		if (OneWire::crc8(addr, 7) != addr[7]) { /*Check CRC*/
			DEBUG_PRINTLN(F("CRC is not valid!"));
			connectedSensorCount = 0;
			for (uint8_t i = 0;i < NUMBER_OF_HEATERS;i++) {
				heaterItems[i].isConnected = false;
			}
			return;
		}

		memcpy(connectedSensors[connectedSensorCount], addr, 8);
		DEBUG_PRINT(F(" ")); DEBUG_PRINT(connectedSensorCount); DEBUG_PRINT(F(": "));
		printAddress(connectedSensors[connectedSensorCount],  6);DEBUG_PRINTLN();
		connectedSensorCount++;
	}

	DEBUG_PRINTLN();DEBUG_PRINTLN(F("Unconfigured sensors:"));
	for (uint8_t i = 0;i < connectedSensorCount;i++) {
		bool configured = false;
		for (uint8_t j = 0;j < NUMBER_OF_HEATERS;j++) {
			if (arraysEqual(connectedSensors[i], heaterItems[j].sensorAddress)) {
				heaterItems[j].isConnected = true;
				configured = true;
				break;
			}
		}
		if (!configured) {
			unconfigured[unconfiguredCount] = connectedSensors[i];
			unconfiguredCount++;
			printAddress(connectedSensors[i]+1, 6);DEBUG_PRINTLN();
		}
	}

	DEBUG_PRINTLN();DEBUG_PRINTLN(F("Configured but not connected sensors:"));
	for (uint8_t i = 0; i < NUMBER_OF_HEATERS; i++) {
		bool connected = false;
		for (uint8_t j = 0;j < connectedSensorCount; j++) {
			if (arraysEqual(heaterItems[i].sensorAddress, connectedSensors[j])) {
				connected = true;
				break;
			}
		}
		if (!connected) {
			heaterItems[i].isConnected = false;
			unconnected[unconnectedCount] = heaterItems[i].sensorAddress;
			unconnectedCount++;
			printAddress(heaterItems[i].sensorAddress+1, 6);DEBUG_PRINTLN();
		}
	}
	DEBUG_MEMORY();
	DEBUG_PRINTLN(F("Detection finished"));
}

void startSensor(byte *addr) {
		ds.reset();
		ds.select(addr);
		ds.write(0x44, 0);        // start conversion, with parasite power off at the end	
}

float readSensor(byte *addr) {
		float celsius = 0;
		byte data[9] = {0};
		//delay(1000);     // maybe 750ms is enough, maybe not
		// we might do a ds.depower() here, but the reset will take care of it.
		
		ds.reset();
		ds.select(addr);
		ds.write(0xBE);         // Read Scratchpad

		for (int i = 0; i < 9; i++) {           // we need 9 bytes
			data[i] = ds.read();
		}
		if (OneWire::crc8(data, 8) != data[8])
		{
			return CRC_ERROR;
		}

		// Convert the data to actual temperature
		// because the result is a 16 bit signed integer, it should
		// be stored to an "int16_t" type, which is always 16 bits
		// even when compiled on a 32 bit processor.
		int16_t raw = (data[1] << 8) | data[0];
		if (type_s) {
			raw = raw << 3; // 9 bit resolution default
			if (data[7] == 0x10) {
				// "count remain" gives full 12 bit resolution
				raw = (raw & 0xFFF0) + 12 - data[6];
			}
			} else {
			byte cfg = (data[4] & 0x60);
			// at lower res, the low bits are undefined, so let's zero them
			if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
			else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
			else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
			//// default is 12 bit resolution, 750 ms conversion time
		}
		celsius = (float)raw / 16.0;
		
		if (celsius == 85) {
			celsius = NO_DATA;
		}
		
		return celsius;
}

void startSensorsRead() {
	for (int i=0; i<NUMBER_OF_HEATERS; i++) { //Start measurement on all sensors
		if (heaterItems[i].isConnected) {
			startSensor(heaterItems[i].sensorAddress);
		}
	}
}

void endSensorsRead() {
	for (int i=0; i<NUMBER_OF_HEATERS; i++) { //Read results
		if (heaterItems[i].isEnabled && heaterItems[i].isConnected) {
			float temp = readSensor(heaterItems[i].sensorAddress);
			if (temp < CRC_ERROR) {
				heaterItems[i].setTemperature(temp);
			} else {
				byte respBuffer[7], respLen = 0;
				//makeCommand(TEMPREADERROR, heaterItems[i].address, NULL, 0, respBuffer, &respLen);
				Serial.write(respBuffer, respLen);
				Serial.print(1,DEC);
			}
		}
	}
}

void processCommand() {
	/*
	byte command[15];
	int commandLen=15;
	serialReadBuffer.getCommand(command, &commandLen);
	if(commandLen>0) {
		if (commandIsValid(command, commandLen)) {
			HeaterItem *heater;
			uint8_t heaterNumber;
			for (heaterNumber=0; heaterNumber<NUMBER_OF_HEATERS; heaterNumber++) { //Locate heater that the command is addressed to
				if (heaterItems[heaterNumber].address[0] == command[1] && heaterItems[heaterNumber].address[1] == command[2] && heaterItems[heaterNumber].address[2] == command[3]) {
					heater = &heaterItems[heaterNumber];
					break;
				}
			}
			if (heater == NULL) { //This command was not for us
				return;
			}
			
			int integer = 0, decimal = 0;
			byte respLen;
			int value;
			float f;
			switch (command[0]) {
				case SETENABLED:
					value = command[4];
					dataBuffer[0] = value;
					heater->isEnabled = value;
					makeCommand(REPORTSETENABLED, heater->address, dataBuffer, 1, respBuffer, &respLen);
					Serial.write(respBuffer, respLen);
					eepromDelayedWrite(heaterNumber, IS_ENABLED);
					break;
				case SETTARGETTEMP:
					value = command[4];
					dataBuffer[0] = value;
					heater->setTargetTemperature(value);
					makeCommand(REPORTSETTARGETTEMP, heater->address, dataBuffer, 1, respBuffer, &respLen);
					Serial.write(respBuffer, respLen);
					eepromDelayedWrite(heaterNumber, TARGET_TEMP);
					break;
				case SETPRIORITY:
					value = command[4];
					dataBuffer[0] = value;
					heater->priority = value;
					makeCommand(REPORTSETPRIORITY, heater->address, dataBuffer, 1, respBuffer, &respLen);
					Serial.write(respBuffer, respLen);
					eepromDelayedWrite(heaterNumber, PRIORITY);
					break;
				case SETAUTO:
					value = command[4];
					dataBuffer[0] = value;
					heater->isAuto = value;
					makeCommand(REPORTSETAUTO, heater->address, dataBuffer, 1, respBuffer, &respLen);
					Serial.write(respBuffer, respLen);					
					eepromDelayedWrite(heaterNumber, IS_AUTO);
					break;
				case SETONOFF:
					if (!heater->isAuto) { //only set heater to on if it's not in auto mode
						value = command[4];
						dataBuffer[0] = value;
						heater->isOn = value;
						makeCommand(REPORTSETONOFF, heater->address, dataBuffer, 1, respBuffer, &respLen);
						Serial.write(respBuffer, respLen);
						eepromDelayedWrite(heaterNumber, IS_ON);
					}
					break;
				case SETSENSOR:
					memcpy(dataBuffer, command+4, 8);
					memcpy(heater->sensorAddress, command+4, 8);
					makeCommand(REPORTSETSENSOR, heater->address, dataBuffer, 8, respBuffer, &respLen);
					Serial.write(respBuffer, respLen);
					eepromDelayedWrite(heaterNumber, SENSOR_ADDRESS);
					//checkConnected();
					//TODO check if it's connected
					break;
				case SETPORT:
					value = command[4];
					dataBuffer[0] = value;
					heater->port = value;
					heater->pin = pins[value-1]; //ports are 1-based
					makeCommand(REPORTSETPORT, heater->address, dataBuffer, 1, respBuffer, &respLen);
					Serial.write(respBuffer, respLen);
					eepromDelayedWrite(heaterNumber, PORT);
					break;
				case SETADJUST:
					integer = command[5];
					decimal = command[6];
					f = (integer*100 + decimal)/100.00;
					if (command[4]) { //negative number
						f = -f;
					}
					heater->setTemperatureAdjust(f);
					heater->getTemperatureAdjustBytes(dataBuffer);
					makeCommand(REPORTSETADJUST, heater->address, dataBuffer, 3, respBuffer, &respLen);
					Serial.write(respBuffer, respLen);
					eepromDelayedWrite(heaterNumber, TEMP_ADJUST);
					break;
				case SETCONSUMPTION:
					value = command[4] <<8 | command[5];
					memcpy(dataBuffer, command+4, 2);
					heater->powerConsumption = value;
					makeCommand(REPORTSETCONSUMPTION, heater->address, dataBuffer, 2, respBuffer, &respLen);
					Serial.write(respBuffer, respLen);
					eepromDelayedWrite(heaterNumber, CONSUMPTION);
					break;
				case GETTEMP:
					reportTemp(heater);
					break;
				case GETACTUALSTATE:
					reportActualState(heater);
					break;
				case GETSTATE:
					dataBuffer[0] = heater->port;
					memcpy(dataBuffer+1, heater->sensorAddress, 8);
					dataBuffer[9] = heater->isAuto;
					dataBuffer[10] = heater->isOn;
					dataBuffer[11] = heater->actualState;
					dataBuffer[12] = heater->priority;
					dataBuffer[13] = heater->getTargetTemperature();
					heater->getTemperatureBytes(dataBuffer+14);
					heater->getTemperatureAdjustBytes(dataBuffer+17);
					dataBuffer[20] = heater->isEnabled;
					dataBuffer[21] = heater->powerConsumption >> 8;
					dataBuffer[22] = heater->powerConsumption;
					makeCommand(REPORTSTATE, heater->address, dataBuffer, 23, respBuffer, &respLen);
					Serial.write(respBuffer,respLen);
					break;
				case SETCONSUMPTIONLIMIT:
					value = command[4] <<8 | command[5];
					consumptionLimit = value;
					eeprom_update_block((void*)&consumptionLimit, (void*)CONSUMPTION_LIMIT, 2);
				case GETCONSUMPTIONLIMIT:
					dataBuffer[0] = *((byte*)&consumptionLimit + 1);
					dataBuffer[1] = *((byte*)&consumptionLimit);
					makeCommand(REPORTCONSUMPTIONLIMIT, heater->address, dataBuffer, 2, respBuffer, &respLen);
					Serial.write(respBuffer, respLen);
					break;
				case SETHYSTERESIS:
					integer = command[4];
					decimal = command[5];
					f = (integer*100 + decimal)/100.00;
					hysteresis = f;
					eeprom_update_block((void*)&hysteresis, (void*)HYSTERESYS, 4);
				case GETHYSTERESIS:
					integer = hysteresis;
					decimal = hysteresis*100 - integer*100;
					dataBuffer[0] = integer;
					dataBuffer[1] = decimal;
					makeCommand(REPORTHYSTERESIS, heater->address, dataBuffer, 2, respBuffer, &respLen);
					Serial.write(respBuffer, respLen);
					break;
				case GETTOTALCONSUMPTION:
					reportTotalConsumption();
					break;
				default:
					break;
			}
		} else {

			DEBUG_PRINTLN(F("Invalid command"));

		}
	}
	*/
}

/*
bool commandIsValid(byte *command, int len) {
	if (len >= 4) { //valid command is at least 4 bytes long
		uint8_t crc = 0;
		crc = calculateCRC(command, len-1);
		if (crc == (command[len-1] | 0x80)) {
			return true;
		}
	}
	return false;
}
*/

byte calculateCRC(byte *command, int len) {
	uint8_t crc = 0;
	for (int i=0; i<len; i++) { //loop through all bytes but last
		crc += command[i];
	}
	crc &= 0xF7;
	return crc;
}

void eepromWriteHeater(uint8_t i){
	uint8_t *baseAddress = (uint8_t*)(i*HEATER_RECORD_LEN);
	
	for (uint8_t j=0; j<8; j++) { //sensorAddress
		eeprom_update_byte((uint8_t*)(SENSOR_ADDRESS + baseAddress + j), heaterItems[i].sensorAddress[j]);
	}
	eeprom_update_byte(IS_ENABLED + baseAddress, (byte)heaterItems[i].isEnabled);
	eeprom_update_byte(PORT + baseAddress, heaterItems[i].port);
	eeprom_update_byte(IS_AUTO + baseAddress, heaterItems[i].isAuto);
	eeprom_update_byte(IS_ON + baseAddress, heaterItems[i].isOn);
	eeprom_update_byte(PRIORITY + baseAddress, heaterItems[i].priority);
	float t = heaterItems[i].getTargetTemperature();
	for (uint8_t j=0; j<4; j++) {
		eeprom_update_byte(TARGET_TEMP + baseAddress + j, *((byte*)(&t + j)));
	}
	t = heaterItems[i].getTemperatureAdjust();
	for (uint8_t j=0; j<4; j++) {
		eeprom_update_byte(TEMP_ADJUST + baseAddress + j, *((byte*)(&t + j)));
	}
	uint16_t c = heaterItems[i].powerConsumption;
	for (uint8_t j=0; j<2; j++) {
		eeprom_update_byte(CONSUMPTION + baseAddress + j, *((byte*)(&c + j)));
	}
	
	
}

void eepromDelayedWrite(uint8_t heaterNumber, uint8_t offset) {
	DEBUG_PRINTLN ("eepromDelayedWrite");
	DEBUG_PRINT("  Heater ");DEBUG_PRINT(heaterNumber);DEBUG_PRINT(", offset");DEBUG_PRINTLN(offset);
	if(!eepromDelayedWriteData.isWriteInitiated) { //no write has been initiated previously. Schedule a new one
		DEBUG_PRINTLN("    New write scheduled");
		eepromDelayedWriteData.isWriteInitiated = true;
		eepromDelayedWriteData.writeInitiatedTime = millis();
		eepromDelayedWriteData.heaterNumber = heaterNumber;
		eepromDelayedWriteData.offset = offset;
	} else { // there is a write pending
		DEBUG_PRINTLN("    Write pending");
		if (eepromDelayedWriteData.heaterNumber == heaterNumber && eepromDelayedWriteData.offset == offset) { //same heater and same offset. Reschedule write
			DEBUG_PRINTLN("      Rescheduling");
			eepromDelayedWriteData.writeInitiatedTime = millis();
		} else { //another heater or offset. Write the current data and schedule the new write
			DEBUG_PRINTLN("      Flushing and scheduling a new one");
			eepromWriteItem(eepromDelayedWriteData.heaterNumber, eepromDelayedWriteData.offset);
			eepromDelayedWriteData.isWriteInitiated = true;
			eepromDelayedWriteData.writeInitiatedTime = millis();
			eepromDelayedWriteData.heaterNumber = heaterNumber;
			eepromDelayedWriteData.offset = offset;
		}
	}
}

void eepromWriteItem(uint8_t heaterNumber, uint8_t offset) {
	DEBUG_PRINTLN("eepromWriteItem");
	DEBUG_PRINT("  Heater ");DEBUG_PRINT(heaterNumber);DEBUG_PRINT(", offset ");DEBUG_PRINTLN(offset);
	uint8_t *baseAddress = (uint8_t*)(heaterNumber*HEATER_RECORD_LEN);
	float t;
	uint16_t c;
	uint8_t arr[8];
	boolean flagWriteError = false;
	switch(offset) {
		case SENSOR_ADDRESS:
			eeprom_update_block(heaterItems[heaterNumber].sensorAddress, SENSOR_ADDRESS + baseAddress, 8);
			eeprom_read_block(arr, SENSOR_ADDRESS + baseAddress, 8);
			if (!arraysEqual(arr, heaterItems[heaterNumber].sensorAddress)) {
				flagWriteError = true;
			}
			break;
		case IS_ENABLED:
			eeprom_update_byte(IS_ENABLED + baseAddress, (byte)heaterItems[heaterNumber].isEnabled);
			if (eeprom_read_byte(IS_ENABLED + baseAddress) != (byte)heaterItems[heaterNumber].isEnabled) {
				flagWriteError = true;
			}
			break;
		case IS_ON:
			eeprom_update_byte(IS_ON + baseAddress, (byte)heaterItems[heaterNumber].isOn);
			if (eeprom_read_byte(IS_ON + baseAddress) != (byte)heaterItems[heaterNumber].isOn) {
				flagWriteError = true;
			}
			break;
		case IS_AUTO:
			eeprom_update_byte(IS_AUTO + baseAddress, (byte)heaterItems[heaterNumber].isAuto);
			if (eeprom_read_byte(IS_AUTO + baseAddress) != (byte)heaterItems[heaterNumber].isAuto) {
				flagWriteError = true;
			}
			break;
		case PORT:
			eeprom_update_byte(PORT + baseAddress, heaterItems[heaterNumber].port);
			if (eeprom_read_byte(PORT + baseAddress) != heaterItems[heaterNumber].port) {
				flagWriteError = true;
			}
			break;
		case PRIORITY:
			eeprom_update_byte(PRIORITY + baseAddress, (byte)heaterItems[heaterNumber].priority);
			if (eeprom_read_byte(PRIORITY + baseAddress) != (byte)heaterItems[heaterNumber].priority) {
				flagWriteError = true;
			}
			break;
		case TARGET_TEMP:
			t = heaterItems[heaterNumber].getTargetTemperature();
			eeprom_update_block(&t, TARGET_TEMP + baseAddress, 4);
			eeprom_read_block(arr, TARGET_TEMP + baseAddress, 4);
			if (!arraysEqual(arr, (byte*)&t)) {
				flagWriteError = true;
			}
			break;
		case TEMP_ADJUST:
			t = heaterItems[heaterNumber].getTemperatureAdjust();
			eeprom_update_block(&t, TEMP_ADJUST + baseAddress, 4);
			eeprom_read_block(arr, TEMP_ADJUST + baseAddress, 4);
			if (!arraysEqual(arr, (byte*)&t)) {
				flagWriteError = true;
			}
			break;
		case CONSUMPTION:
			c = heaterItems[heaterNumber].powerConsumption;
			eeprom_update_block(&c, CONSUMPTION + baseAddress, 2);
			eeprom_read_block(arr, CONSUMPTION + baseAddress, 2);
			if (!arraysEqual(arr, (byte*)&c)) {
				flagWriteError = true;
			}
		default:
			break;
	}
	if (flagWriteError) {
		DEBUG_PRINTLN("Write error!!!!!!!");
		byte comBuffer[6], comBufferLen = 0;
		//makeCommand(EEPROMERROR, heaterItems[heaterNumber].address, NULL, 0, comBuffer, &comBufferLen);
		Serial.write(comBuffer, comBufferLen);
	} else {
		DEBUG_PRINTLN("Write successful.");
	}
	eepromDelayedWriteData.isWriteInitiated = false; //Write complete. Unschedule.
}

void eepromReadHeater(uint8_t heaterNumber) {
	uint8_t *baseAddress = (uint8_t *)(heaterNumber*HEATER_RECORD_LEN);
	eeprom_read_block(heaterItems[heaterNumber].sensorAddress, baseAddress + SENSOR_ADDRESS, 8);
	heaterItems[heaterNumber].isEnabled = eeprom_read_byte(baseAddress + IS_ENABLED);
	heaterItems[heaterNumber].port = eeprom_read_byte(baseAddress + PORT);
	heaterItems[heaterNumber].pin = pins[heaterItems[heaterNumber].port - 1];
	heaterItems[heaterNumber].isOn = eeprom_read_byte(baseAddress + IS_ON);
	heaterItems[heaterNumber].isAuto = eeprom_read_byte(baseAddress + IS_AUTO);
	heaterItems[heaterNumber].priority = eeprom_read_byte(baseAddress + PRIORITY);
	float t;
	eeprom_read_block(&t, baseAddress + TARGET_TEMP, 4);
	heaterItems[heaterNumber].setTargetTemperature(t);
	eeprom_read_block(&t, baseAddress + TEMP_ADJUST, 4);
	heaterItems[heaterNumber].setTemperatureAdjust(t);
	uint16_t c;
	eeprom_read_block(&c, baseAddress + CONSUMPTION, 2);
	heaterItems[heaterNumber].powerConsumption = c;
}

bool arraysEqual(byte *array1, byte *array2) {
	if (sizeof(array1) != sizeof(array2)) {
		return false;
	}
	for (uint8_t i=0; i<sizeof(array1); i++) {
		if (array1[i] != array2[i]) {
			return false;
		}
	}
	return true;
}

void printAddress(const byte* address, const uint8_t len) {
#ifdef DEBUG
	char buff[13];
	byteArrayToString(address, len, buff);
	DEBUG_PRINT(buff);
#endif
}

void initHeaters() {
	heaterItems[0].address = HEATER1;
	heaterItems[1].address = HEATER2;
	heaterItems[2].address = HEATER3;
	heaterItems[3].address = HEATER4;
	heaterItems[4].address = HEATER5;
	heaterItems[5].address = HEATER6;
	heaterItems[6].address = HEATER7;
	heaterItems[7].address = HEATER8;
	heaterItems[8].address = HEATER9;
	heaterItems[9].address = HEATER10;

	for (uint8_t i=0; i<NUMBER_OF_HEATERS; i++) {
		eepromReadHeater(i);
		validateHeater(i);
	}
	eeprom_read_block((void*)&consumptionLimit, (void*)CONSUMPTION_LIMIT, 2);
	if (consumptionLimit < 0 || consumptionLimit > MAX_CONSUMPTION_LIMIT) {
		consumptionLimit = DEFAULT_CONSUMPTION_LIMIT;
	}
	eeprom_read_block((void*)&hysteresis, (void*)HYSTERESYS, 4);
	if (hysteresis < 0 || hysteresis > 2) {
		hysteresis = DEFAULT_HYSTERESIS;
	}

}

void validateHeater(uint8_t heaterNumber) {
	heaterItems[heaterNumber].actualState = false;
	heaterItems[heaterNumber].wantsOn = false;
// 	
// 	if (heaterItems[heaterNumber].getTargetTemperature() < 0 || heaterItems[heaterNumber].getTargetTemperature() > 30) {
// 		heaterItems[heaterNumber].setTargetTemperature(DEFAULT_TEMPERATURE);
// 	}
// 	if (heaterItems[heaterNumber].getTemperatureAdjust() < -7 || heaterItems[heaterNumber].getTemperatureAdjust() > 7) {
// 		heaterItems[heaterNumber].setTemperatureAdjust(DEFAULT_TEMPERATURE_ADJUST);
// 	}
	if (heaterItems[heaterNumber].isAuto) {
		heaterItems[heaterNumber].isOn = false;
		heaterItems[heaterNumber].wantsOn = false;
	}
	if (heaterItems[heaterNumber].port > NUMBER_OF_HEATERS + 1) {
		heaterItems[heaterNumber].isEnabled = false;
	}
}

void listHeaters(HeaterItem **array, int size) {

	for (int i=0; i<size; i++) {
		printAddress(array[i]->address+1, 6); DEBUG_PRINT(F(": ")); DEBUG_PRINT(array[i]->priority); DEBUG_PRINT(F("   ")); DEBUG_PRINT(array[i]->getTemperature()); DEBUG_PRINT(F("   ")); DEBUG_PRINT(array[i]->getDelta()); DEBUG_PRINT(F(" "));
		if (array[i]->actualState) {
			DEBUG_PRINT(F("x"));
		}
		if (array[i]->wantsOn) {
			DEBUG_PRINT(F("w"));
		}
		DEBUG_PRINT(F("\n"));
	}
	DEBUG_PRINTLN();

}

/*
void makeCommand(byte command, const byte* address, byte* data, int dataLen, byte* comBuffer, byte* comBufferLen) {
	comBuffer[0] = BEGINTRANSMISSION;
	comBuffer[1] = command;
	memcpy(comBuffer + 2, address, 3);
	memcpy(comBuffer + 5, data, dataLen);
	comBuffer[5+dataLen] = calculateCRC(comBuffer+1, 4 + dataLen);
	comBuffer[6+dataLen] = 0x3b;
	*comBufferLen = 7 + dataLen;
}
*/

void initPins() {
	for (int i=0;i<NUMBER_OF_HEATERS;i++) {
		pinAsOutput(pins[i]);
		digitalLow(pins[i]);
	}
}

unsigned long elapsedSince(unsigned long then) {
	unsigned long now = millis();
	return (now - then);
}

/*
void reportTemp(HeaterItem *heater) {
	byte respLen = 0;
	heater->getTemperatureBytes(dataBuffer);
	makeCommand(REPORTTEMP, heater->address, dataBuffer, 3, respBuffer, &respLen);
	Serial.write(respBuffer, respLen);
}

void reportActualState(HeaterItem *heater) {
	byte respLen = 0;
	dataBuffer[0] = heater->actualState;
	makeCommand(REPORTACTUALSTATE, heater->address, dataBuffer, 1, respBuffer, &respLen);
	Serial.write(respBuffer, respLen);
}
*/
void reportTotalConsumption() {
	StaticJsonDocument<38> json;
	//Serial.println(MQTT_STATUSES_TOPIC);
	char payload[50];
	json[TOTAL_CONSUMPTION] = totalConsumption;
	serializeJson(json, payload);
	mqttClient.publish(MQTT_STATUSES_TOPIC, payload);
	Serial.println(payload);
	//Serial.println();Serial.print(F("!!! Free memory: "));Serial.print(freeMemory());Serial.println(F(" !!!"));Serial.println();
}

void setup()
{
	emergencyHandled = millis();
	
	Timer1.initialize(PROCESSING_INTERVAL);
	Timer1.attachInterrupt(timer1_ISR);
	Timer1.stop();
	
	MsTimer2::set(READ_SENSORS_DELAY, timer2_ISR);
	MsTimer2::stop();
	
	
	eepromDelayedWriteData.isWriteInitiated = false;
	eepromDelayedWriteData.writeInitiatedTime = millis();
	eepromDelayedWriteData.offset = UNDEFINED;
	eepromDelayedWriteData.offset = UNDEFINED;
//#ifdef DEBUG
	Serial.begin(115200);
	delay(1000);
//#endif
	DEBUG_PRINTLN(F(" "));
	DEBUG_PRINTLN(F("V1.3.0 starting..."));
	DEBUG_PRINTLN(F("Debug mode"));
	DEBUG_MEMORY();
	DEBUG_PRINTLN();

	uint8_t mac[6] = { 0x00,0x01,0x02,0x03,0x04,0x05 };

	mqttClient.setServer("192.168.0.3", 1883);
	mqttClient.setCallback(mqttCallback);
	Ethernet.begin(mac);
	//sleep?
	DEBUG_PRINTLN(Ethernet.localIP());

	//modbus_configure(9600, 1, 0, TOTAL_REGS_SIZE, 0);

	consumptionLimit = DEFAULT_CONSUMPTION_LIMIT;
	initPins();
	initHeaters(); /*will assign heater addresses*/
	detectSensors(); /*will populate connectedSensors*/
	startSensorsRead(); /*will populate temperatures*/
	delay(1000);
	endSensorsRead();
	Timer1.start();
	DEBUG_MEMORY();
}

void loop()
{
	DEBUG_MEMORY();
	if (mqttClient.connected()) {
		mqttClient.loop();
		//mqttClient.publish("ehome/heating/commands/item_000001", "connect");
	}
	else {
		mqttReconnect();
	}

	//holdingRegs[0] = modbus_update(holdingRegs);

	totalConsumption = 1; //cm.getConsumption();
	if ((int)(consumptionLimit - totalConsumption) < 0) { //If measured current consumption is above limit
			if (elapsedSince(emergencyHandled) > 1000UL) {
				DEBUG_PRINTLN(F("\n!!! EMERGENCY !!!"));
				flagEmergency = true;
				processHeaters(totalConsumption, MODE_HEATER_OFF);
				emergencyHandled = millis();
				flagEmergency =false;
			}
			return; //Emergency mode, take no more actions, restart the main loop
	}
	
//processSerial();
	
	if (eepromDelayedWriteData.isWriteInitiated) {
		if (elapsedSince(eepromDelayedWriteData.writeInitiatedTime) >= EEPROM_WRITE_DELAY_TIME) {
			eepromWriteItem(eepromDelayedWriteData.heaterNumber, eepromDelayedWriteData.offset);
		}
	}
	
	/*
	if (serialReadBuffer.commandReceived) {
		processCommand();
	}
	*/
	
	if (flagTimer2) {
		MsTimer2::stop();
		flagTimer2 = false;
		endSensorsRead();
		
		DEBUG_PRINT(F("\nConsumption limit: ")); DEBUG_PRINTLN(consumptionLimit);
		

		processHeaters(totalConsumption, MODE_HEATER_OFF);
		mode = MODE_HEATER_OFF;
		Timer1.setPeriod(OFF_ON_DELAY); //Need time to measure decreased consumption (low consumption means long measurement time). 2s ~ 560 watts.
	}
	
	if (flagTimer1) {
		flagTimer1 = false;
		
		if (mode == MODE_HEATER_ON) { //In this mode we need to read temperatures and switch off some of the heaters based on readings
			startSensorsRead();
			MsTimer2::start();
		} else { //In this mode we need to switch on some heaters based on readings (taken 2 seconds ago in previous branch)
			processHeaters(totalConsumption, MODE_HEATER_ON);
			mode = MODE_HEATER_ON;
			Timer1.setPeriod(PROCESSING_INTERVAL);
		}
	}
}

void stringToByteArray(const char* string, uint8_t len, byte* hex) {
	if (len == 3) {
		sscanf(string, "%2x%2x%2x", &hex[0], &hex[1], &hex[2]);
	}
	else if (len == 6) {
		sscanf(string, "%2x%2x%2x%2x%2x%2x", &hex[0], &hex[1], &hex[2], &hex[3], &hex[4], &hex[5]);
	}
}

void byteArrayToString(const byte* hex, uint8_t len, char* string) {
	char* pos = string;
	for (uint8_t i = 0;i < len;i++) {
		sprintf(pos, "%02X", hex[i]);
		pos = pos + 2;
	}
	string[len * 2] = '\0';
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {

}

void mqttReconnect() {
	DEBUG_PRINTLN(F("Connecting to MQTT server..."));
	if (mqttClient.connect("Arduino")) {
		DEBUG_PRINTLN(F("Connected"));
	}
	else {

	}
}