/******************************************************************************
   Yet Another QLOCKTHREE Firmware
   Eine Firmware der Selbstbau-QLOCKTWO.

   @mc       Arduino/RBBB (ATMEGA328)
   @autor    Christian Aschoff / caschoff _AT_ mac _DOT_ com
   @version
   @created  1.11.2011
******************************************************************************/

#include <avr/pgmspace.h>
#include <Wire.h>
#include <EEPROM.h>
#include "Configuration.h"
#include "LedDriver.h"
#include "LedDriverDefault.h"
#include "LedDriverUeberPixel.h"
#include "LedDriverPowerShiftRegister.h"
#include "LedDriverNeoPixel.h"
#include "LedDriverDotStar.h"
#include "LedDriverLPD8806.h"
#include "IRTranslator.h"
#include "IRTranslatorSparkfun.h"
#include "IRTranslatorMooncandles.h"
#include "IRTranslatorLunartec.h"
#include "IRTranslatorCLT.h"
#include "IRTranslatorApple.h"
#include "IRTranslatorPhilips.h"
#include "IRTranslatorHX1838.h"
#include "MyIRremote.h"
#include "MyRTC.h"
#include "Button.h"
#include "AnalogButton.h"
#include "LDR.h"
#include "Renderer.h"
#include "Staben.h"
#include "Alarm.h"
#include "Settings.h"
#include "Zahlen.h"
#include "Modes.h"
#include "Debug.h"
#include "Boards.h"
#include "MyDCF77.h"
#include "DCF77Helper.h"
#include "Boards.h"
#ifdef EVENTS
#include "Events.h"
#endif

#define FIRMWARE_VERSION "yaqtfw_20170218"

/******************************************************************************
   Init.
******************************************************************************/

#if defined(LED_DRIVER_DEFAULT) && defined (BOARD_DEFAULT)
LedDriverDefault ledDriver(PIN_LEDS_DATA, PIN_LEDS_LOAD, PIN_LEDS_CLOCK, PIN_LEDS_OUTPUT_ENABLE, 10);
#endif
#if defined(LED_DRIVER_POWER_SHIFT_REGISTER) && defined (BOARD_DEFAULT)
LedDriverPowerShiftRegister ledDriver(PIN_LEDS_DATA, PIN_LEDS_LOAD, PIN_LEDS_CLOCK, PIN_LEDS_OUTPUT_ENABLE);
#endif
#if defined(LED_DRIVER_UEBERPIXEL) && defined (BOARD_DEFAULT)
LedDriverUeberPixel ledDriver(PIN_LEDS_DATA, PIN_LEDS_CLOCK, PIN_LEDS_LOAD);
#endif
#ifdef LED_DRIVER_DOTSTAR
LedDriverDotStar ledDriver(PIN_LEDS_DATA, PIN_LEDS_CLOCK);
#endif
#ifdef LED_DRIVER_NEOPIXEL
LedDriverNeoPixel ledDriver(PIN_LEDS_DATA);
#endif
#ifdef LED_DRIVER_LPD8806
LedDriverLPD8806 ledDriver(PIN_LEDS_DATA, PIN_LEDS_CLOCK);
#endif

// Settings from EEPROM.
Settings settings;

// Der Renderer, der die Woerter auf die Matrix ausgibt.
Renderer renderer;

// IR-Remote-Control-Translator.
#ifndef REMOTE_NO_REMOTE
IRrecv irrecv(PIN_IR_RECEIVER);
decode_results irDecodeResults;
#if defined (REMOTE_SPARKFUN)
IRTranslatorSparkfun irTranslator;
#elif defined (REMOTE_MOONCANDLES)
IRTranslatorMooncandles irTranslator;
#elif defined (REMOTE_LUNARTEC)
IRTranslatorLunartec irTranslator;
#elif defined (REMOTE_CLT)
IRTranslatorCLT irTranslator;
#elif defined (REMOTE_APPLE)
IRTranslatorApple irTranslator;
#elif defined (REMOTE_PHILIPS)
IRTranslatorPhilips irTranslator;
#elif defined (REMOTE_HX1838)
IRTranslatorHX1838 irTranslator;
#endif
#endif

// Bluetooth-Remote-Control-Translator.
#ifdef REMOTE_BLUETOOTH
IRTranslatorCLT irTranslatorBT;
#endif

// Real-Time-Clock mit der Status-LED fuer das SQW-Signal.
MyRTC rtc(0x68, PIN_SQW_LED);
volatile byte helperSeconds;

// Funkempfaenger (DCF77-Signal der PTB Braunschweig).
#ifdef DCF77
MyDCF77 dcf77(PIN_DCF77_SIGNAL, PIN_DCF77_LED);
DCF77Helper dcf77Helper;
#endif

#ifdef USE_STD_MODE_ALARM
Alarm alarm(PIN_SPEAKER);
#endif

// Helligkeitssensor.
LDR ldr(PIN_LDR, LDR_INVERTED);
unsigned long lastBrightnessCheck;

// Die Helligkeit zum Anzeigen mit den Balken.
byte brightnessToDisplay;

// Buttons.
Button minutesPlusButton(PIN_M_PLUS, BUTTONS_PRESSING_AGAINST);
Button hoursPlusButton(PIN_H_PLUS, BUTTONS_PRESSING_AGAINST);
Button extModeDoubleButton(PIN_M_PLUS, PIN_H_PLUS, BUTTONS_PRESSING_AGAINST);
Button modeChangeButton(PIN_MODE, BUTTONS_PRESSING_AGAINST);

// Der aktuelle und der letzte Modus.
Mode mode = STD_MODE_NORMAL;
Mode lastMode = mode;

// Die Matrix, eine Art Bildschirmspeicher.
word matrix[16];

// Hilfsvariable, da I2C und Interrupts nicht zusammenspielen.
volatile boolean needsUpdateFromRtc = true;

// Fuer den Bildschirm-Test
uint8_t testColumn;

// For FPS_DEBUG.
#ifdef DEBUG_FPS
word frames = 0;
unsigned long lastFpsCheck = 0;
#endif

// For DCF_DEBUG.
#ifdef USE_EXT_MODE_DCF_SYNC
unsigned int dcf77ErrorMinutes;
#endif

// Zaehler für fall back timer im Menu.
byte fallBackCounter = 0;

// Event active?
bool evtActive = false;

// Temperatur
unsigned long temperature = 0;

/******************************************************************************
   Initialisierung. setup() wird einmal zu Beginn aufgerufen.
******************************************************************************/

void setup() {

	Serial.begin(SERIAL_SPEED);  // Set speed for debug console and bluetooth remote.
	Serial.setTimeout(50);       // Timeout for bluetooth remote.
	Wire.begin();                // Start I2C.

	DEBUG_PRINTLN(F("QLOCKTHREE"));
	DEBUG_PRINT(F("Version: "));
	DEBUG_PRINTLN(FIRMWARE_VERSION);
	DEBUG_PRINT(F("Flags: "));

#ifdef DEBUG_SET_DEFAULTS
	factoryReset();
#endif

#ifdef DCF77
	// DCF77-Empfaenger einschalten...
#ifdef MYDCF77_SIGNAL_IS_INVERTED
	settings.setDcfSignalIsInverted(true);
#else
	settings.setDcfSignalIsInverted(false);
#endif
	pinMode(PIN_DCF77_PON, OUTPUT);
	enableDcf(true);
	DEBUG_PRINT(F("DCF77, "));
	if (settings.getDcfSignalIsInverted()) {
		DEBUG_PRINT(F("inverted, "));
	}
	// DCF77-LED drei Mal als 'Hello' blinken lassen
	// und Speaker piepsen lassen wenn vorhanden.
	for (byte i = 0; i < 3; i++) {
		dcf77.statusLed(true);
#ifdef USE_STD_MODE_ALARM
		alarm.buzz(true);
#endif
		delay(100);
		dcf77.statusLed(false);
#ifdef USE_STD_MODE_ALARM
		alarm.buzz(false);
#endif
		delay(100);
	}
#endif

	delay(500);

	// RTC starten...
	pinMode(PIN_SQW_SIGNAL, INPUT);
	digitalWrite(PIN_SQW_SIGNAL, HIGH);
#ifdef DS3231
	DEBUG_PRINT(F("DS3231, "));
	rtc.enableSQWOnDS3231();
#else
	DEBUG_PRINT(F("DS1307, "));
	rtc.enableSQWOnDS1307();
#endif
	rtc.readTime();
	if ((rtc.getSeconds() >= 60) || (rtc.getMinutes() >= 60) || (rtc.getHours() >= 24) || (rtc.getYear() < 15)) {
		rtc.set(11, 11, 1, 1, 1, 17);
		rtc.setSeconds(11);
	}
	rtc.writeTime();
	helperSeconds = rtc.getSeconds();
	// rtcSQWLed-LED drei Mal als 'Hello' blinken lassen
	// und Speaker piepsen lassen wenn vorhanden.
	for (byte i = 0; i < 3; i++) {
		rtc.statusLed(true);
#ifdef USE_STD_MODE_ALARM
		alarm.buzz(true);
#endif
		delay(100);
		rtc.statusLed(false);
#ifdef USE_STD_MODE_ALARM
		alarm.buzz(false);
#endif
		delay(100);
	}

	// IR-sensor initialisieren.
#ifndef REMOTE_NO_REMOTE
	irTranslator.printSignature();
	irrecv.enableIRIn();
#endif

#ifdef RENDER_CORNERS_CCW
	DEBUG_PRINT(F("CCW, "));
	settings.setRenderCornersCw(false);
#else
	settings.setRenderCornersCw(true);
#endif

#ifdef USE_STD_MODE_ALARM
	DEBUG_PRINT(F("Alarm, "));
#endif

	// LED-Treiber initialisieren und Display einschalten.
	ledDriver.init();
	ledDriver.clearData();
	ledDriver.setLinesToWrite(10);
	ledDriver.wakeUp();
#ifdef LED_TEST_INTRO
	renderer.setAllScreenBuffer(matrix);
	ledDriver.setBrightness(255);
	ledDriver.writeScreenBufferToMatrix(matrix, true, color_white);
	delay(3000);
#endif
	ledDriver.setBrightness(settings.getBrightness());
	ledDriver.printSignature();
	DEBUG_PRINTLN(F("."));
	renderer.clearScreenBuffer(matrix);

	// Den Interrupt konfigurieren. FALLING signalisiert den Sekundenwechsel.
	// Number 0 (for digital pin 2) interrupt.
#ifndef DEBUG_HALT
	attachInterrupt(0, updateFromRtc, FALLING);
#endif

	DEBUG_PRINT(F("Free RAM: "));
	DEBUG_PRINT(freeRam());
	DEBUG_PRINTLN(F(" bytes."));

}

/******************************************************************************
   loop() wird endlos vom Microcontroller durchlaufen.
******************************************************************************/

void loop() {

#ifdef DEBUG_FPS
	frames++;
	if (millis() - lastFpsCheck > 1000) {
		Serial.print("FPS: ");
		Serial.println(frames);
		lastFpsCheck = millis();
		frames = 0;
	}
#endif

	// Dimmung.
	if (settings.getUseLdr()) {
		if (lastBrightnessCheck + LDR_CHECK_RATE < millis()) {
			byte lv = ldr.value();
			if (ledDriver.getBrightness() > lv) {
				ledDriver.setBrightness(ledDriver.getBrightness() - 1);
			}
			else if (ledDriver.getBrightness() < lv) {
				ledDriver.setBrightness(ledDriver.getBrightness() + 1);
			}
			lastBrightnessCheck = millis();
		}
	}

	/******************************************************************************
	   START Render the Matrix depending on mode and needsUpdateFromRtc.
	******************************************************************************/

	// needsUpdateFromRtc wird via Interrupt gesetzt ueber fallende Flanke des SQW-Signals von der RTC.
	// Oder falls eine Tasten-Aktion eine sofortige Aktualisierung des Displays braucht.
	if (needsUpdateFromRtc) {
		needsUpdateFromRtc = false;
		// Zeit einlesen... nur alle 60 Sekunden, um die Minuten und evtl. Stunden zu aktualisieren.
		if (helperSeconds > 59) {
			rtc.readTime();
			helperSeconds = rtc.getSeconds();
		}

		/******************************************************************************
		   Render STD_MODE_*
		******************************************************************************/

		switch (mode) {
		case STD_MODE_NORMAL:
#ifdef EVENTS
			for (byte evtID = 0; evtID < nbrOfEvts; evtID++) {
				if ((rtc.getDate() == events[evtID].getDate()) & (rtc.getMonth() == events[evtID].getMonth())) {
					switch (settings.getEvent()) {
					case 0:
						while (!(rtc.getMinutes() % 5)) {
							evtActive = true;
							events[evtID].show();
							rtc.readTime();
						}
						break;
					case 1:
						while (!(rtc.getMinutes() % 15)) {
							evtActive = true;
							events[evtID].show();
							rtc.readTime();
						}
						break;
					case 2:
						while (!(rtc.getMinutes() % 30)) {
							evtActive = true;
							events[evtID].show();
							rtc.readTime();
						}
						break;
					case 3:
						while (!(rtc.getMinutes() % 60)) {
							evtActive = true;
							events[evtID].show();
							rtc.readTime();
						}
						break;
					default:
						break;
					}
					evtActive = false;
				}
			}
#endif
			renderer.clearScreenBuffer(matrix);
			renderer.setMinutes(rtc.getHours() + settings.getTimeShift(), rtc.getMinutes(), settings.getLanguage(), matrix);
			renderer.setCorners(rtc.getMinutes(), settings.getRenderCornersCw(), matrix);
#ifdef USE_EXT_MODE_IT_IS
			// "ES IST" weg. Zur vollen Stunde und um halb aber anzeigen.
			if (!settings.getEsIst() && ((rtc.getMinutes() / 5) % 6)) {
				renderer.clearEntryWords(settings.getLanguage(), matrix);
			}
#endif
#ifdef DEBUG_TIME
			Serial.print(rtc.getHours() + settings.getTimeShift());
			Serial.print(F(":"));
			Serial.print(rtc.getMinutes());
			Serial.print(F(":"));
			Serial.println(helperSeconds);
#endif
			break;
#ifdef USE_STD_MODE_AMPM
		case STD_MODE_AMPM:
			renderer.clearScreenBuffer(matrix);
			if (rtc.getHours() < 12) {
				renderer.setMenuText("AM", Renderer::TEXT_POS_MIDDLE, matrix);
				DEBUG_PRINTLN(F("AM"));
			}
			else {
				renderer.setMenuText("PM", Renderer::TEXT_POS_MIDDLE, matrix);
				DEBUG_PRINTLN(F("PM"));
			}
			break;
#endif
#ifdef USE_STD_MODE_SECONDS
		case STD_MODE_SECONDS:
			renderer.clearScreenBuffer(matrix);
			renderer.setCorners(rtc.getMinutes(), settings.getRenderCornersCw(), matrix);
			for (byte i = 0; i < 7; i++) {
				matrix[1 + i] |= pgm_read_byte_near(&(ziffern[helperSeconds / 10][i])) << 11;
				matrix[1 + i] |= pgm_read_byte_near(&(ziffern[helperSeconds % 10][i])) << 5;
			}
			DEBUG_PRINTLN(helperSeconds);
			break;
#endif
#ifdef USE_STD_MODE_DATE
		case STD_MODE_DATE:
			renderer.clearScreenBuffer(matrix);
			for (byte i = 0; i < 5; i++) {
				matrix[0 + i] |= pgm_read_byte_near(&(ziffernB[rtc.getDate() / 10][i])) << 11;
				matrix[0 + i] |= pgm_read_byte_near(&(ziffernB[rtc.getDate() % 10][i])) << 6;
				matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[rtc.getMonth() / 10][i])) << 11;
				matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[rtc.getMonth() % 10][i])) << 6;
			}
			ledDriver.setPixelInScreenBuffer(10, 4, matrix);
			ledDriver.setPixelInScreenBuffer(10, 9, matrix);
			DEBUG_PRINT(rtc.getDate());
			DEBUG_PRINT(F("."));
			DEBUG_PRINT(rtc.getMonth());
			DEBUG_PRINT(F("."));
			DEBUG_PRINTLN(rtc.getYear());
			break;
#endif
#ifdef USE_STD_MODE_TEMP
		case STD_MODE_TEMP:
			temperature = 0;
#ifdef TEMP_SENS_LM35
			for (byte i = 0; i < 4; i++) temperature += analogRead(PIN_TEMP_SENS) / 2 + TEMP_OFFSET;
#endif
#ifdef TEMP_SENS_LM335
			for (byte i = 0; i < 4; i++) temperature += analogRead(PIN_TEMP_SENS) / 2 + TEMP_OFFSET - 273;
#endif
#ifdef TEMP_SENS_DS3231
			for (byte i = 0; i < 4; i++) temperature += rtc.getTemperature() + TEMP_OFFSET;
#endif
			renderer.clearScreenBuffer(matrix);
			for (byte i = 0; i < 7; i++) {
				matrix[1 + i] |= pgm_read_byte_near(&(ziffern[(temperature / 5) / 10][i])) << 11;
				matrix[1 + i] |= pgm_read_byte_near(&(ziffern[(temperature / 5) % 10][i])) << 5;
			}
			matrix[0] |= 0b0000000000011111; // LED rechts oben setzen als "Grad".
			DEBUG_PRINTLN(temperature / 5);
			break;
#endif
#ifdef USE_STD_MODE_ALARM
		case STD_MODE_ALARM:
			renderer.clearScreenBuffer(matrix);
			if (alarm.getShowAlarmTimeTimer() == 0) {
				renderer.setMinutes(rtc.getHours() + settings.getTimeShift(), rtc.getMinutes(), settings.getLanguage(), matrix);
				renderer.setCorners(rtc.getMinutes(), settings.getRenderCornersCw(), matrix);
#ifdef USE_EXT_MODE_IT_IS
				// "ES IST" weg. Zur vollen Stunde und um halb aber anzeigen.
				if (!settings.getEsIst() && ((rtc.getMinutes() / 5) % 6)) {
					renderer.clearEntryWords(settings.getLanguage(), matrix);
				}
#endif
				renderer.activateAlarmLed(matrix); // Alarm-LED
				DEBUG_PRINT(rtc.getHours() + settings.getTimeShift());
				DEBUG_PRINT(F(":"));
				DEBUG_PRINT(rtc.getMinutes());
				DEBUG_PRINT(F(":"));
				DEBUG_PRINTLN(helperSeconds);
			}
			else {
				// Alarmzeit blinken lassen
				if (alarm.getShowAlarmTimeTimer() % 2 == 0) {
					renderer.setMinutes(alarm.getHours() + settings.getTimeShift(), alarm.getMinutes(), settings.getLanguage(), matrix);
					renderer.setCorners(alarm.getMinutes(), settings.getRenderCornersCw(), matrix);
					// "ES IST" weg beim Anzeigen der Alarmzeit.
					renderer.clearEntryWords(settings.getLanguage(), matrix);
					renderer.activateAMPM(alarm.getHours() + settings.getTimeShift(), settings.getLanguage(), matrix);
					renderer.activateAlarmLed(matrix); // Alarm-LED
				}
				alarm.decShowAlarmTimeTimer();
			}
			DEBUG_PRINT(F("Alarm set to "));
			DEBUG_PRINTLN(alarm.asString());
			break;
#endif
		case STD_MODE_BRIGHTNESS:
			renderer.clearScreenBuffer(matrix);
			brightnessToDisplay = map(settings.getBrightness(), 1, 100, 0, 9);
			for (byte xb = 0; xb < brightnessToDisplay; xb++) {
				for (byte yb = 0; yb <= xb; yb++) {
					matrix[9 - yb] |= 1 << (14 - xb);
				}
			}
			DEBUG_PRINTLN(settings.getBrightness());
			break;
		case STD_MODE_BLANK:
		case STD_MODE_NIGHT:
			renderer.clearScreenBuffer(matrix);
			break;

			/******************************************************************************
			   Render EXT_MODE_*
			******************************************************************************/

#ifdef USE_EXT_MODE_TITLES
		case EXT_MODE_TEXT_MAIN:
			renderer.clearScreenBuffer(matrix);
			renderer.setMenuText("MA", Renderer::TEXT_POS_TOP, matrix);
			renderer.setMenuText("IN", Renderer::TEXT_POS_BOTTOM, matrix);
			DEBUG_PRINTLN(F("MAIN"));
			break;
#endif
		case EXT_MODE_LDR_MODE:
			renderer.clearScreenBuffer(matrix);
			if (settings.getUseLdr()) {
				renderer.setMenuText("A", Renderer::TEXT_POS_MIDDLE, matrix);
				DEBUG_PRINTLN(F("A"));
			}
			else {
				renderer.setMenuText("M", Renderer::TEXT_POS_MIDDLE, matrix);
				DEBUG_PRINTLN(F("M"));
			}
			break;
		case EXT_MODE_TRANSITION:
			renderer.clearScreenBuffer(matrix);
			renderer.setMenuText("TM", Renderer::TEXT_POS_TOP, matrix);
			switch (settings.getTransitionMode()) {
			case Settings::TRANSITION_MODE_NORMAL:
				renderer.setMenuText("NO", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("TM NO"));
				break;
			case Settings::TRANSITION_MODE_FADE:
				renderer.setMenuText("FD", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("TM FD"));
				break;
			case Settings::TRANSITION_MODE_MATRIX:
				renderer.setMenuText("MX", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("TM MX"));
				break;
			case Settings::TRANSITION_MODE_SLIDE:
				renderer.setMenuText("SD", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("TM SD"));
				break;
			}
			break;
#ifndef LED_DRIVER_DEFAULT
		case EXT_MODE_COLOR:
			renderer.clearScreenBuffer(matrix);
			if (settings.getColor() <= color_single_max) {
				renderer.setMenuText("C", Renderer::TEXT_POS_TOP, matrix);
				for (byte i = 0; i < 5; i++) {
					matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[settings.getColor() / 10][i])) << 10;
					matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[settings.getColor() % 10][i])) << 5;
				}
				DEBUG_PRINT(F("C "));
				DEBUG_PRINTLN(settings.getColor());
			}
			else {
				renderer.setMenuText("CC", Renderer::TEXT_POS_TOP, matrix);
				for (byte i = 0; i < 5; i++) {
					matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[(settings.getColor() - color_single_max) / 10][i])) << 10;
					matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[(settings.getColor() - color_single_max) % 10][i])) << 5;
				}
				DEBUG_PRINT(F("CC "));
				DEBUG_PRINTLN(settings.getColor() - color_single_max);
			}
			break;
		case EXT_MODE_COLOR_CHANGE:
			if (settings.getColor() > color_single_max) { // nur anzeigen wenn CC01 oder CC02
				renderer.clearScreenBuffer(matrix);
				renderer.setMenuText("CR", Renderer::TEXT_POS_TOP, matrix);
				for (byte i = 0; i < 7; i++) {
					matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[settings.getColorChangeRate() / 10][i])) << 10;
					matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[settings.getColorChangeRate() % 10][i])) << 5;
				}
				DEBUG_PRINT(F("CR "));
				DEBUG_PRINTLN(settings.getColorChangeRate());
			}
			else mode++;
			break;
#endif
#ifdef EVENTS
		case EXT_MODE_EVENT:
			renderer.clearScreenBuffer(matrix);
			renderer.setMenuText("EV", Renderer::TEXT_POS_TOP, matrix);
			switch (settings.getEvent()) {
			case 0:
				renderer.setMenuText("5", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("EV 5"));
				break;
			case 1:
				renderer.setMenuText("15", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("EV 15"));
				break;
			case 2:
				renderer.setMenuText("30", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("EV 30"));
				break;
			case 3:
				renderer.setMenuText("60", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("EV 60"));
				break;
			}
			break;
#endif
		case EXT_MODE_LANGUAGE:
			renderer.clearScreenBuffer(matrix);
			switch (settings.getLanguage()) {
#ifdef ENABLE_LANGUAGE_DE
			case LANGUAGE_DE_DE:
				renderer.setMenuText("DE", Renderer::TEXT_POS_MIDDLE, matrix);
				DEBUG_PRINTLN(F("DE"));
				break;
			case LANGUAGE_DE_SW:
				renderer.setMenuText("DE", Renderer::TEXT_POS_TOP, matrix);
				renderer.setMenuText("SW", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("DE SW"));
				break;
			case LANGUAGE_DE_BA:
				renderer.setMenuText("DE", Renderer::TEXT_POS_TOP, matrix);
				renderer.setMenuText("BA", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("DE BA"));
				break;
			case LANGUAGE_DE_SA:
				renderer.setMenuText("DE", Renderer::TEXT_POS_TOP, matrix);
				renderer.setMenuText("SA", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("DE SA"));
				break;
#endif
#ifdef ENABLE_LANGUAGE_DE_MKF
			case LANGUAGE_DE_MKF_DE:
				renderer.setMenuText("MK", Renderer::TEXT_POS_MIDDLE, matrix);
				DEBUG_PRINTLN(F("MK"));
				break;
			case LANGUAGE_DE_MKF_SW:
				renderer.setMenuText("MK", Renderer::TEXT_POS_TOP, matrix);
				renderer.setMenuText("SW", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("MK SW"));
				break;
			case LANGUAGE_DE_MKF_BA:
				renderer.setMenuText("MK", Renderer::TEXT_POS_TOP, matrix);
				renderer.setMenuText("BA", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("MK BA"));
				break;
			case LANGUAGE_DE_MKF_SA:
				renderer.setMenuText("MK", Renderer::TEXT_POS_TOP, matrix);
				renderer.setMenuText("SA", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("MK SA"));
				break;
#endif
#ifdef ENABLE_LANGUAGE_D3
			case LANGUAGE_D3:
				renderer.setMenuText("D3", Renderer::TEXT_POS_MIDDLE, matrix);
				DEBUG_PRINTLN(F("D3"));
				break;
#endif
#ifdef ENABLE_LANGUAGE_CH
			case LANGUAGE_CH:
				renderer.setMenuText("CH", Renderer::TEXT_POS_MIDDLE, matrix);
				DEBUG_PRINTLN(F("CH"));
				break;
			case LANGUAGE_CH_X:
				renderer.setMenuText("CH", Renderer::TEXT_POS_TOP, matrix);
				renderer.setMenuText("GS", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("CH GS"));
				break;
#endif
#ifdef ENABLE_LANGUAGE_EN
			case LANGUAGE_EN:
				renderer.setMenuText("EN", Renderer::TEXT_POS_MIDDLE, matrix);
				DEBUG_PRINTLN(F("EN"));
				break;
#endif
#ifdef ENABLE_LANGUAGE_FR
			case LANGUAGE_FR:
				renderer.setMenuText("FR", Renderer::TEXT_POS_MIDDLE, matrix);
				DEBUG_PRINTLN(F("FR"));
				break;
#endif
#ifdef ENABLE_LANGUAGE_IT
			case LANGUAGE_IT:
				renderer.setMenuText("IT", Renderer::TEXT_POS_MIDDLE, matrix);
				DEBUG_PRINTLN(F("IT"));
				break;
#endif
#ifdef ENABLE_LANGUAGE_NL
			case LANGUAGE_NL:
				renderer.setMenuText("NL", Renderer::TEXT_POS_MIDDLE, matrix);
				DEBUG_PRINTLN(F("NL"));
				break;
#endif
#ifdef ENABLE_LANGUAGE_ES
			case LANGUAGE_ES:
				renderer.setMenuText("ES", Renderer::TEXT_POS_MIDDLE, matrix);
				DEBUG_PRINTLN(F("ES"));
				break;
#endif
			default:
				;
			}
			break;
		case EXT_MODE_JUMP_TIMEOUT:
			renderer.clearScreenBuffer(matrix);
			renderer.setMenuText("FB", Renderer::TEXT_POS_TOP, matrix);
			if (helperSeconds % 2 == 0) {
				for (byte i = 0; i < 5; i++) matrix[5 + i] = 0;
			}
			else {
				for (byte i = 0; i < 7; i++) {
					matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[settings.getJumpToNormalTimeout() / 10][i])) << 10;
					matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[settings.getJumpToNormalTimeout() % 10][i])) << 5;
				}
			}
			DEBUG_PRINT(F("FB "));
			DEBUG_PRINTLN(settings.getJumpToNormalTimeout());
			break;
#ifdef USE_EXT_MODE_TITLES
		case EXT_MODE_TEXT_TIME:
			renderer.clearScreenBuffer(matrix);
			renderer.setMenuText("TI", Renderer::TEXT_POS_TOP, matrix);
			renderer.setMenuText("ME", Renderer::TEXT_POS_BOTTOM, matrix);
			DEBUG_PRINTLN(F("TIME"));
			break;
#endif
#ifdef USE_EXT_MODE_IT_IS
		case EXT_MODE_IT_IS:
			renderer.clearScreenBuffer(matrix);
			renderer.setMenuText("IT", Renderer::TEXT_POS_TOP, matrix);
			if (settings.getEsIst()) {
				renderer.setMenuText("EN", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("IT EN"));
			}
			else {
				renderer.setMenuText("DA", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("IT DA"));
			}
			break;
#endif
		case EXT_MODE_TIMESET:
			if (helperSeconds % 2 == 0) {
				renderer.clearScreenBuffer(matrix);
			}
			else {
				renderer.clearScreenBuffer(matrix);
				renderer.setMinutes(rtc.getHours() + settings.getTimeShift(), rtc.getMinutes(), settings.getLanguage(), matrix);
				renderer.setCorners(rtc.getMinutes(), settings.getRenderCornersCw(), matrix);
				renderer.clearEntryWords(settings.getLanguage(), matrix);
				renderer.activateAMPM(rtc.getHours() + settings.getTimeShift(), settings.getLanguage(), matrix);
			}
			DEBUG_PRINT(rtc.getHours() + settings.getTimeShift());
			DEBUG_PRINT(F(":"));
			DEBUG_PRINT(rtc.getMinutes());
			DEBUG_PRINT(F(":"));
			DEBUG_PRINTLN(helperSeconds);
			break;
#ifdef USE_EXT_MODE_TIME_SHIFT
		case EXT_MODE_TIME_SHIFT:
			renderer.clearScreenBuffer(matrix);
			if (settings.getTimeShift() < 0) {  // Draw minus on screen.
				for (byte x = 0; x < 3; x++) {
					ledDriver.setPixelInScreenBuffer(x, 1, matrix);
				}
			}
			else if (settings.getTimeShift() > 0) {  // Draw plus on screen.
				for (byte x = 0; x < 3; x++) {
					ledDriver.setPixelInScreenBuffer(x, 1, matrix);
				}
				for (byte y = 0; y < 3; y++) {
					ledDriver.setPixelInScreenBuffer(1, y, matrix);
				}
			}
			for (byte i = 0; i < 7; i++) {
				matrix[3 + i] |= pgm_read_byte_near(&(ziffern[abs(settings.getTimeShift()) % 10][i])) << 5;
				if (abs(settings.getTimeShift()) > 9) {
					matrix[3 + i] |= pgm_read_byte_near(&(ziffern[1][i])) << 10;
				}
			}
			DEBUG_PRINTLN(abs(settings.getTimeShift()));
			break;
#endif
#ifdef USE_EXT_MODE_DATE_MANUALLY
		case EXT_MODE_YEARSET: // Einstellung Jahr
			renderer.clearScreenBuffer(matrix);
			renderer.setMenuText("YY", Renderer::TEXT_POS_TOP, matrix);
			if (helperSeconds % 2 == 0) {
				for (byte i = 0; i < 5; i++) matrix[5 + i] = 0;
			}
			else {
				for (byte i = 0; i < 5; i++) {
					matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[rtc.getYear() / 10][i])) << 10;
					matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[rtc.getYear() % 10][i])) << 5;
				}
			}
			DEBUG_PRINT(F("YY "));
			DEBUG_PRINTLN(rtc.getYear());
			break;
		case EXT_MODE_MONTHSET: // Einstellung Monat
			renderer.clearScreenBuffer(matrix);
			renderer.setMenuText("MM", Renderer::TEXT_POS_TOP, matrix);
			if (helperSeconds % 2 == 0) {
				for (byte i = 0; i < 5; i++) matrix[5 + i] = 0;
			}
			else {
				if (rtc.getMonth() > 9) {
					for (byte i = 0; i < 5; i++) {
						matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[rtc.getMonth() / 10][i])) << 10;
						matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[rtc.getMonth() % 10][i])) << 5;
					}
				}
				else {
					for (byte i = 0; i < 5; i++) {
						matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[rtc.getMonth() % 10][i])) << 8;
					}
				}
			}
			DEBUG_PRINT(F("MM "));
			DEBUG_PRINTLN(rtc.getMonth());
			break;
		case EXT_MODE_DAYSET: // Einstellung Tag
			renderer.clearScreenBuffer(matrix);
			renderer.setMenuText("DD", Renderer::TEXT_POS_TOP, matrix);
			if (helperSeconds % 2 == 0) {
				for (byte i = 0; i < 5; i++) matrix[5 + i] = 0;
			}
			else {
				if (rtc.getDate() > 9) {
					for (byte i = 0; i < 5; i++) {
						matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[rtc.getDate() / 10][i])) << 10;
						matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[rtc.getDate() % 10][i])) << 5;
					}
				}
				else {
					for (byte i = 0; i < 5; i++) {
						matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[rtc.getDate() % 10][i])) << 8;
					}
				}
			}
			DEBUG_PRINT(F("DD "));
			DEBUG_PRINTLN(rtc.getDate());
			break;
#endif
#ifdef USE_EXT_MODE_NIGHT_OFF
		case EXT_MODE_NIGHT_OFF:
			renderer.clearScreenBuffer(matrix);
			if (!fallBackCounter) {
				renderer.setMenuText("N", Renderer::TEXT_POS_TOP, matrix);
				renderer.setMenuText("OF", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("N OF"));
			}
			else {
				if (helperSeconds % 2 == 0) {
					renderer.clearScreenBuffer(matrix);
				}
				else {
					renderer.clearScreenBuffer(matrix);
					renderer.setMinutes(settings.getNightModeTime(false)->getHours(), settings.getNightModeTime(false)->getMinutes(), settings.getLanguage(), matrix);
					renderer.clearEntryWords(settings.getLanguage(), matrix);
					renderer.activateAMPM(settings.getNightModeTime(false)->getHours(), settings.getLanguage(), matrix);
				}
				DEBUG_PRINT(settings.getNightModeTime(false)->getHours());
				DEBUG_PRINT(F(":"));
				DEBUG_PRINTLN(settings.getNightModeTime(false)->getMinutes());
			}
			break;
		case EXT_MODE_NIGHT_ON:
			renderer.clearScreenBuffer(matrix);
			if (!fallBackCounter) {
				renderer.setMenuText("N", Renderer::TEXT_POS_TOP, matrix);
				renderer.setMenuText("ON", Renderer::TEXT_POS_BOTTOM, matrix);
				DEBUG_PRINTLN(F("N ON"));
			}
			else {
				if (helperSeconds % 2 == 0) {
					renderer.clearScreenBuffer(matrix);
				}
				else {
					renderer.clearScreenBuffer(matrix);
					renderer.setMinutes(settings.getNightModeTime(true)->getHours(), settings.getNightModeTime(true)->getMinutes(), settings.getLanguage(), matrix);
					renderer.clearEntryWords(settings.getLanguage(), matrix);
					renderer.activateAMPM(settings.getNightModeTime(true)->getHours(), settings.getLanguage(), matrix);
				}
				DEBUG_PRINT(settings.getNightModeTime(true)->getHours());
				DEBUG_PRINT(F(":"));
				DEBUG_PRINTLN(settings.getNightModeTime(true)->getMinutes());
			}
			break;
#endif
#ifdef USE_EXT_MODE_TITLES
		case EXT_MODE_TEXT_TEST:
			renderer.clearScreenBuffer(matrix);
			renderer.setMenuText("TE", Renderer::TEXT_POS_TOP, matrix);
			renderer.setMenuText("ST", Renderer::TEXT_POS_BOTTOM, matrix);
			DEBUG_PRINTLN(F("TEST"));
			break;
#endif
#ifdef USE_EXT_MODE_TEST
		case EXT_MODE_TEST:
			renderer.clearScreenBuffer(matrix);
			renderer.setCorners(helperSeconds % 5, settings.getRenderCornersCw(), matrix);
#ifdef USE_STD_MODE_ALARM
			renderer.activateAlarmLed(matrix); // Alarm-LED
#endif
			testColumn++;
			if (testColumn > 10) {
				testColumn = 0;
			}
			for (byte i = 0; i < 11; i++) {
				ledDriver.setPixelInScreenBuffer(testColumn, i, matrix);
			}
			DEBUG_PRINTLN(F("LED anim."));
			break;
#endif
#if defined(USE_EXT_MODE_DCF_SYNC) && defined(DCF77)
		case EXT_MODE_DCF_SYNC:
			renderer.clearScreenBuffer(matrix);
			dcf77ErrorMinutes = dcf77.getDcf77LastSuccessSyncMinutes();
			for (byte i = 0; i < 5; i++) {
				matrix[0 + i] |= pgm_read_byte_near(&(ziffernB[dcf77ErrorMinutes / 60 / 10][i])) << 11;
				matrix[0 + i] |= pgm_read_byte_near(&(ziffernB[dcf77ErrorMinutes / 60 % 10][i])) << 6;
				matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[dcf77ErrorMinutes % 60 / 10][i])) << 11;
				matrix[5 + i] |= pgm_read_byte_near(&(ziffernB[dcf77ErrorMinutes % 60 % 10][i])) << 6;
			}
			ledDriver.setPixelInScreenBuffer(10, 1, matrix);  // Doppelpunkt
			ledDriver.setPixelInScreenBuffer(10, 3, matrix);
			DEBUG_PRINT(F("DCF77 sync: "));
			DEBUG_PRINTLN(dcf77ErrorMinutes);
			break;
#endif
#if defined(USE_EXT_MODE_DCF_DEBUG) && defined(DCF77)
		case EXT_MODE_DCF_DEBUG:
			renderer.clearScreenBuffer(matrix);
			renderer.setCorners(dcf77.getDcf77ErrorCorner(), settings.getRenderCornersCw(), matrix);
			DEBUG_PRINTLN(F("DCF anim."));
			break;
#endif
		default:
			break;
		}

#if defined(IR_LETTER_OFF_X) && defined(IR_LETTER_OFF_Y)
		// Die LED hinter dem IR-Sensor abschalten.
		ledDriver.unsetPixelInScreenBuffer(IR_LETTER_OFF_X - 1, IR_LETTER_OFF_Y - 1, matrix);
#endif

		/******************************************************************************
		   Matrix gerendert.
		******************************************************************************/

#ifdef DEBUG_MATRIX
		// Matrix auf der Konsole ausgeben.
		debugScreenBuffer(matrix);
#endif

		// Die Matrix auf die LEDs multiplexen mit neuem Inhalt.
		ledDriver.writeScreenBufferToMatrix(matrix, true, settings.getColor());

	}

	/******************************************************************************
	   Button pressed.
	******************************************************************************/

	// Mode gedrueckt.
	if (modeChangeButton.pressed()) {
		modePressed();
	}

	// H+ gedrueckt.
	if (hoursPlusButton.pressed()) {
		hourPlusPressed();
	}

	// M+ gedrueckt.
	if (minutesPlusButton.pressed()) {
		minutePlusPressed();
	}

	// H+ und M+ im STD_MODE_NORMAL gedrueckt.
	if ((mode == STD_MODE_NORMAL) && extModeDoubleButton.pressed()) {
		doubleStdModeNormalPressed();
	}

	// H+ und M+ im STD_MODE_BLANK gedrueckt.
	if ((mode == STD_MODE_BLANK) && extModeDoubleButton.pressed()) {
		doubleExtModePressed();
	}

#ifdef EVENTS
	// H+ und M+ im STD_MODE_SECONDS gedrueckt.
	if ((mode == STD_MODE_SECONDS) && extModeDoubleButton.pressed()) {
		doubleEvtModePressed();
	}
#endif

	/******************************************************************************
	   Button pressed on IR-remote or bluetooth-remote.
	******************************************************************************/

#ifndef REMOTE_NO_REMOTE
	unsigned long lastIrCode = 0;
	if (irrecv.decode(&irDecodeResults)) {
		lastIrCode = irTranslator.buttonForCode(irDecodeResults.value);
		irrecv.resume();
		DEBUG_PRINT("IR signal received: ");
		DEBUG_PRINTLN(irDecodeResults.value);
	}
	if (lastIrCode != 0) {
		remoteAction(lastIrCode, &irTranslator);
	}
#endif

#ifdef REMOTE_BLUETOOTH
	unsigned int lastIrCodeBT = 0;
	while (Serial.available() > 0) {
		lastIrCodeBT = irTranslatorBT.buttonForCode(Serial.parseInt());
		Serial.read();
		DEBUG_PRINT("Bluetooth signal received: ");
		DEBUG_PRINTLN(lastIrCodeBT);
	}
	if (lastIrCodeBT != 0) {
		remoteAction(lastIrCodeBT, &irTranslatorBT);
	}
#endif

	/******************************************************************************
	   misc in loop()
	******************************************************************************/

#ifdef USE_EXT_MODE_NIGHT_OFF
	// Display zeitgesteuert abschalten.
	if ((mode < EXT_MODE_START) && (mode != STD_MODE_NIGHT) && (settings.getNightModeTime(false)->getMinutesOfDay(0) == rtc.getMinutesOfDay(0)) && (helperSeconds == 0)) {
		mode = STD_MODE_NIGHT; // hier nicht setMode() verwenden
		ledDriver.shutDown();
	}
	if ((mode == STD_MODE_NIGHT) && (settings.getNightModeTime(true)->getMinutesOfDay(0) == rtc.getMinutesOfDay(0)) && (helperSeconds == 0)) {
		mode = lastMode;
		ledDriver.wakeUp();
	}
#endif

#ifdef USE_STD_MODE_ALARM
	// Alarm.
	if ((mode == STD_MODE_ALARM) && (alarm.getShowAlarmTimeTimer() == 0) && !alarm.isActive()) {
		if (alarm.getMinutesOfDay(0) == rtc.getMinutesOfDay(0)) {
			alarm.activate();
		}
	}
	if (alarm.isActive()) {
		// Nach MAX_BUZZ_TIME Minuten automatisch abschalten.
		if (alarm.getMinutesOfDay(MAX_BUZZ_TIME_IN_MINUTES) == rtc.getMinutesOfDay(0)) {
			alarm.deactivate(); // Hier funktioniert was nicht.
			setMode(STD_MODE_NORMAL);
		}
		// Krach machen.
		if (helperSeconds % 2 == 0) {
			alarm.buzz(true);
		}
		else {
			alarm.buzz(false);
		}
	}
#endif

#if defined(ENABLE_DCF_LED) && defined(DCF77)
	dcf77.statusLed(dcf77.signal(settings.getDcfSignalIsInverted()));
#endif

#ifdef ENABLE_SQW_LED
	rtc.statusLed(digitalRead(PIN_SQW_SIGNAL) == HIGH);
#endif

#ifdef DCF77
	if (dcf77.poll(settings.getDcfSignalIsInverted())) {
		manageNewDCF77Data();
	}
#endif

#if defined(USE_EXT_MODE_DCF_DEBUG) && defined(DCF77)
	if (mode == EXT_MODE_DCF_DEBUG) {
		byte currentErrorCorner = dcf77.getDcf77ErrorCorner();
		dcf77.updateDcf77ErrorCorner(settings.getDcfSignalIsInverted());
		if (currentErrorCorner != dcf77.getDcf77ErrorCorner()) {
			needsUpdateFromRtc = true;
		}
	}
#endif

	// Die Matrix auf die LEDs multiplexen. Nur 'Refresh' des Inhalts.
	if ((mode != STD_MODE_BLANK) && (mode != STD_MODE_NIGHT)) {
		ledDriver.writeScreenBufferToMatrix(matrix, false, settings.getColor());
	}
}

/******************************************************************************
   ENDE der loop().
******************************************************************************/

/******************************************************************************
   H+ and M+ buttons pressed together.
*******************************************************************************/

void doubleStdModeNormalPressed() {
	needsUpdateFromRtc = true;
	// In STD_MODE_NORMAL press H+ and M+ together to enter STD_MODE_NIGHT when in EXT_MODE_NIGHT_ON range.
	if (isCurrentTimeInNightRange()) {
		setMode(STD_MODE_NIGHT);
		ledDriver.shutDown();
		DEBUG_PRINTLN(F("Entering STD_MODE_NIGHT."));
	}
}

void doubleExtModePressed() {
	needsUpdateFromRtc = true;
	// In STD_MODE_BLANK press H+ and M+ together for 5 seconds to reset clock.
	unsigned long initialMillis = millis();
	while (minutesPlusButton.pressedRaw() && hoursPlusButton.pressedRaw()) {
		if ((millis() - initialMillis) > 5000) {
			factoryReset();
			DEBUG_PRINTLN(F("*** RESET! ***"));
			delay(500);
			asm volatile ("  jmp 0"); // Software RESET!
			return;
		}
	}
	// In STD_MODE_BLANK press H+ and M+ together to enter EXT_MODE_START.
	setMode(EXT_MODE_START);
	ledDriver.wakeUp();
	DEBUG_PRINTLN(F("Entering EXT_MODE_START."));
}

#ifdef EVENTS
// In EVENT_MODE press H+ and M+ together to show event.
void doubleEvtModePressed() {
	static byte i = 0;
	disableFallBackCounter();
	needsUpdateFromRtc = true;
	if (nbrOfEvts > 0) {
		events[i].show();
		i++;
	}
	if (i >= nbrOfEvts) {
		i = 0;
	}
	enableFallBackCounter(settings.getJumpToNormalTimeout());
	DEBUG_PRINTLN(F("Demoevent."));
}
#endif

/******************************************************************************
   "Mode" button pressed.
******************************************************************************/

void modePressed() {
	needsUpdateFromRtc = true;

	// Displaytreiber einschalten, wenn BLANK verlassen wird
	if (mode == STD_MODE_BLANK) {
		DEBUG_PRINTLN(F("LED-Driver: WakeUp"));
		ledDriver.wakeUp();
	}

	switch (mode) {
	case STD_MODE_NIGHT:
		setDisplayToToggle();
		break;
	default:
		mode++;
		break;
	}

	// Brightness ueberspringen, wenn automatische Helligkeit verwendet wird.
	if (settings.getUseLdr() && (mode == STD_MODE_BRIGHTNESS)) {
		mode++;
	}

#ifdef USE_STD_MODE_ALARM
	// Alarm einstellen.
	if (mode == STD_MODE_ALARM) {
		alarm.setShowAlarmTimeTimer(settings.getJumpToNormalTimeout());
	}

	// Alarm abschalten wenn aktiv.
	if (alarm.isActive()) {
		alarm.deactivate();
		setMode(STD_MODE_NORMAL);
	}
#endif

	// Ende. Zurueck zur Zeit.
	if ((mode == STD_MODE_COUNT) || (mode == EXT_MODE_COUNT)) {
		setMode(STD_MODE_NORMAL);
	}

	// Set fallback in standard mode.
	switch (mode) {
#ifdef USE_STD_MODE_AMPM
	case STD_MODE_AMPM:
#endif
#ifdef USE_STD_MODE_SECONDS
	case STD_MODE_SECONDS:
#endif
#ifdef USE_STD_MODE_DATE
	case STD_MODE_DATE:
#endif
#ifdef USE_STD_MODE_TEMP
	case STD_MODE_TEMP:
#endif
	case STD_MODE_BRIGHTNESS:
		enableFallBackCounter(settings.getJumpToNormalTimeout());
		break;
	default:
		disableFallBackCounter();
	}

	// Turn off display.
	if (mode == STD_MODE_BLANK) {
		DEBUG_PRINTLN(F("LED-Driver: ShutDown"));
		ledDriver.shutDown();
	}

	// Save the last mode when going to sleep.
	if ((mode != STD_MODE_BLANK) && (mode != STD_MODE_NIGHT)) {
		lastMode = mode;
	}

	// Save changed settings.
	settings.saveToEEPROM();

	DEBUG_PRINT(F("Mode-button pressed, now: "));
	DEBUG_PRINTLN(mode);

}

/******************************************************************************
   "H+" button pressed.
******************************************************************************/

void hourPlusPressed() {
	needsUpdateFromRtc = true;
	DEBUG_PRINTLN(F("H+ Button pressed."));

	switch (mode) {
	case EXT_MODE_TIMESET:
		incDecHours(true);
		break;
#ifdef USE_EXT_MODE_TIME_SHIFT
	case EXT_MODE_TIME_SHIFT:
		if (settings.getTimeShift() < 13) {
			settings.setTimeShift(settings.getTimeShift() + 1);
		}
		break;
#endif
#ifdef USE_EXT_MODE_DATE_MANUALLY
	case EXT_MODE_YEARSET:
		rtc.incYear();
		rtc.writeTime();
		break;
	case EXT_MODE_MONTHSET:
		rtc.incMonth();
		rtc.writeTime();
		break;
	case EXT_MODE_DAYSET:
		rtc.incDate();
		rtc.writeTime();
		break;
#endif
#ifdef USE_STD_MODE_ALARM
	case STD_MODE_ALARM:
		alarm.incHours();
		alarm.setShowAlarmTimeTimer(settings.getJumpToNormalTimeout());
		break;
#endif
	case STD_MODE_BRIGHTNESS:
		setDisplayBrighter();
		enableFallBackCounter(settings.getJumpToNormalTimeout());
		break;
	case EXT_MODE_LDR_MODE:
		settings.setUseLdr(!settings.getUseLdr());
		if (!settings.getUseLdr()) {
			settings.setBrightness(ledDriver.getBrightness());
		}
		break;
#ifdef EVENTS
	case EXT_MODE_EVENT:
		settings.setEvent(settings.getEvent() + 1);
		if (settings.getEvent() > 3) {
			settings.setEvent(0);
		}
		break;
#endif
	case EXT_MODE_TRANSITION:
		if (settings.getTransitionMode() == Settings::TRANSITION_MODE_MAX - 1) {
			settings.setTransitionMode(0);
		}
		else {
			settings.setTransitionMode(settings.getTransitionMode() + 1);
#ifndef LED_DRIVER_DEFAULT
			if (settings.getTransitionMode() == Settings::TRANSITION_MODE_MATRIX) {
				settings.setTransitionMode(settings.getTransitionMode() + 1);
			}
#endif
		}
		break;
#ifndef LED_DRIVER_DEFAULT
	case EXT_MODE_COLOR:
		if (settings.getColor() == color_max) {
			settings.setColor((eColors)0);
		}
		else {
			settings.setColor((eColors)(settings.getColor() + 1));
		}
		if (settings.getColor() > color_single_max) {
			ledDriver.resetWheelPos();
		}
		break;
	case EXT_MODE_COLOR_CHANGE:
		if (settings.getColorChangeRate() < 10) {
			settings.setColorChangeRate(settings.getColorChangeRate() + 1);
		}
		break;
#endif
	case EXT_MODE_JUMP_TIMEOUT:
		if (settings.getJumpToNormalTimeout() < 99) {
			settings.setJumpToNormalTimeout(settings.getJumpToNormalTimeout() + 1);
		}
		break;
#ifdef USE_EXT_MODE_NIGHT_OFF
	case EXT_MODE_NIGHT_OFF:
		if (fallBackCounter > 0) {
			settings.getNightModeTime(false)->incHours();
		}
		enableFallBackCounter(settings.getJumpToNormalTimeout());
		break;
	case EXT_MODE_NIGHT_ON:
		if (fallBackCounter > 0) {
			settings.getNightModeTime(true)->incHours();
		}
		enableFallBackCounter(settings.getJumpToNormalTimeout());
		break;
#endif
	case EXT_MODE_LANGUAGE:
		settings.setLanguage(settings.getLanguage() + 1);
		if (settings.getLanguage() >= LANGUAGE_COUNT) {
			settings.setLanguage(0);
		}
		break;
#ifdef USE_EXT_MODE_IT_IS
	case EXT_MODE_IT_IS:
		settings.toggleEsIst();
		break;
#endif
#ifdef USE_EXT_MODE_TITLES
	case EXT_MODE_TEXT_MAIN:
		setMode(EXT_MODE_TEXT_TIME);
		break;
	case EXT_MODE_TEXT_TIME:
		setMode(EXT_MODE_TEXT_TEST);
		break;
	case EXT_MODE_TEXT_TEST:
		setMode(EXT_MODE_TEXT_MAIN);
		break;
#endif
	default:
		break;
	}
}

/******************************************************************************
   "M+" button pressed.
******************************************************************************/

void minutePlusPressed() {
	needsUpdateFromRtc = true;
	DEBUG_PRINTLN(F("M+ Button pressed."));

	switch (mode) {
	case EXT_MODE_TIMESET:
		incDecMinutes(true);
		break;
#ifdef USE_EXT_MODE_TIME_SHIFT
	case EXT_MODE_TIME_SHIFT:
		if (settings.getTimeShift() > -13) {
			settings.setTimeShift(settings.getTimeShift() - 1);
		}
		break;
#endif
#ifdef USE_EXT_MODE_DATE_MANUALLY
	case EXT_MODE_YEARSET:
		rtc.incYear(-1);
		rtc.writeTime();
		break;
	case EXT_MODE_MONTHSET:
		rtc.incMonth(-1);
		rtc.writeTime();
		break;
	case EXT_MODE_DAYSET:
		rtc.incDate(-1);
		rtc.writeTime();
		break;
#endif
#ifdef USE_STD_MODE_ALARM
	case STD_MODE_ALARM:
		alarm.incFiveMinutes();
		alarm.setShowAlarmTimeTimer(settings.getJumpToNormalTimeout());
		break;
#endif
	case STD_MODE_BRIGHTNESS:
		setDisplayDarker();
		enableFallBackCounter(settings.getJumpToNormalTimeout());
		break;
	case EXT_MODE_LDR_MODE:
		settings.setUseLdr(!settings.getUseLdr());
		if (!settings.getUseLdr()) {
			settings.setBrightness(ledDriver.getBrightness());
		}
		break;
#ifdef EVENTS
	case EXT_MODE_EVENT:
		if (settings.getEvent() == 0) {
			settings.setEvent(3);
		}
		else {
			settings.setEvent(settings.getEvent() - 1);
		}
		break;
#endif
	case EXT_MODE_TRANSITION:
		if (settings.getTransitionMode() == 0) {
			settings.setTransitionMode(Settings::TRANSITION_MODE_MAX - 1);
		}
		else {
			settings.setTransitionMode(settings.getTransitionMode() - 1);
#ifndef LED_DRIVER_DEFAULT
			if (settings.getTransitionMode() == Settings::TRANSITION_MODE_MATRIX) {
				settings.setTransitionMode(settings.getTransitionMode() - 1);
			}
#endif
		}
		break;
#ifndef LED_DRIVER_DEFAULT
	case EXT_MODE_COLOR:
		if (settings.getColor() == 0)
		{
			settings.setColor(color_max);
		}
		else {
			settings.setColor((eColors)(settings.getColor() - 1));
		}
		break;
	case EXT_MODE_COLOR_CHANGE:
		if (settings.getColorChangeRate() > 0) {
			settings.setColorChangeRate(settings.getColorChangeRate() - 1);
		}
		break;
#endif
	case EXT_MODE_JUMP_TIMEOUT:
		if (settings.getJumpToNormalTimeout() > 0) {
			settings.setJumpToNormalTimeout(settings.getJumpToNormalTimeout() - 1);
		}
		break;
#ifdef USE_EXT_MODE_NIGHT_OFF
	case EXT_MODE_NIGHT_OFF:
		if (fallBackCounter > 0) {
			settings.getNightModeTime(false)->incFiveMinutes();
		}
		enableFallBackCounter(settings.getJumpToNormalTimeout());
		break;
	case EXT_MODE_NIGHT_ON:
		if (fallBackCounter > 0) {
			settings.getNightModeTime(true)->incFiveMinutes();
		}
		enableFallBackCounter(settings.getJumpToNormalTimeout());
		break;
#endif
	case EXT_MODE_LANGUAGE:
		if (settings.getLanguage() == 0) {
			settings.setLanguage(LANGUAGE_COUNT - 1);
		}
		else {
			settings.setLanguage(settings.getLanguage() - 1);
		}
		break;
#ifdef USE_EXT_MODE_IT_IS
	case EXT_MODE_IT_IS:
		settings.toggleEsIst();
		break;
#endif
#ifdef USE_EXT_MODE_TITLES
	case EXT_MODE_TEXT_MAIN:
		setMode(EXT_MODE_TEXT_TEST);
		break;
	case EXT_MODE_TEXT_TIME:
		setMode(EXT_MODE_TEXT_MAIN);
		break;
	case EXT_MODE_TEXT_TEST:
		setMode(EXT_MODE_TEXT_TIME);
		break;
#endif
	default:
		break;
	}
}

/******************************************************************************
   Remote button pressed.
******************************************************************************/

#if defined(REMOTE_BLUETOOTH) || !defined(REMOTE_NO_REMOTE)
void remoteAction(unsigned int irCode, IRTranslator * irTranslatorGeneric) {
	needsUpdateFromRtc = true;

	switch (irCode) {
	case REMOTE_BUTTON_TOGGLEBLANK:
		setDisplayToToggle();
		break;
	case REMOTE_BUTTON_BLANK:
		setDisplayToBlank();
		break;
	case REMOTE_BUTTON_RESUME:
		setDisplayToResume();
		break;
	case REMOTE_BUTTON_MODE:
		modePressed();
		break;
	case REMOTE_BUTTON_SECONDS:
		if (mode < STD_MODE_BLANK) {
			modePressed();
		}
		else {
			setMode(STD_MODE_NORMAL);
		}
		break;
	}

	if ((mode != STD_MODE_BLANK) &&
		(mode != STD_MODE_NIGHT)) {

		switch (irCode) {
		case REMOTE_BUTTON_MINUTE_PLUS:
			minutePlusPressed();
			break;
		case REMOTE_BUTTON_HOUR_PLUS:
			hourPlusPressed();
			break;
		case REMOTE_BUTTON_LDR:
			if (EXT_MODE_LDR_MODE == mode) {
				settings.setUseLdr(!settings.getUseLdr());
				if (!settings.getUseLdr()) {
					settings.setBrightness(ledDriver.getBrightness());
				}
			}
			else {
				setMode(EXT_MODE_LDR_MODE);
			}
			break;
		case REMOTE_BUTTON_BRIGHTER:
			settings.setUseLdr(false);
			if (STD_MODE_BRIGHTNESS == mode) {
				setDisplayBrighter();
			}
			else {
				setMode(STD_MODE_BRIGHTNESS);
			}
			break;
		case REMOTE_BUTTON_DARKER:
			settings.setUseLdr(false);
			if (STD_MODE_BRIGHTNESS == mode) {
				setDisplayDarker();
			}
			else {
				setMode(STD_MODE_BRIGHTNESS);
			}
			break;
		case REMOTE_BUTTON_EXTMODE:
			if (mode < EXT_MODE_START) {
				setMode(EXT_MODE_START);
			}
			else {
				modePressed();
			}
			break;
		case REMOTE_BUTTON_SETCOLOR:
			if ((irTranslatorGeneric->getColor() == color_rgb_continuous) && (settings.getColor() == color_rgb_continuous)) {
				settings.setColor(eColors::color_rgb_step);
			}
			else {
				settings.setColor(irTranslatorGeneric->getColor());
			}
#ifndef LED_DRIVER_DEFAULT
			if (settings.getColor() > color_single_max) {
				setMode(EXT_MODE_COLOR);
			}
#endif
			ledDriver.resetWheelPos();
			break;
		case REMOTE_BUTTON_SETMODE:
			setMode(STD_MODE_NORMAL);
			break;
		case REMOTE_BUTTON_REGION:
			if (EXT_MODE_LANGUAGE == mode) {
				hourPlusPressed();
			}
			else {
				setMode(EXT_MODE_LANGUAGE);
			}
			break;
		case REMOTE_BUTTON_TIME_H_PLUS:
			if (mode == EXT_MODE_TIMESET) {
				incDecHours(true);
			}
			else
				hourPlusPressed();
			break;
		case REMOTE_BUTTON_TIME_H_MINUS:
			if (mode == EXT_MODE_TIMESET) {
				incDecHours(false);
			}
			else
				minutePlusPressed();
			break;
		case REMOTE_BUTTON_TIME_M_PLUS:
			if (mode == EXT_MODE_TIMESET) {
				incDecMinutes(true);
			}
			else
				hourPlusPressed();
			break;
		case REMOTE_BUTTON_TIME_M_MINUS:
			if (mode == EXT_MODE_TIMESET) {
				incDecMinutes(false);
			}
			else
				minutePlusPressed();
			break;
		case REMOTE_BUTTON_TRANSITION:
			settings.setTransitionMode(irTranslatorGeneric->getTransition());
			ledDriver.demoTransition();
			break;
		default:
			break;
		}
	}

	// Fallback stellen fuer Funktionen welche eine eigene Taste auf der Fernbedienung haben.
	if ((irCode != REMOTE_BUTTON_TIME_H_PLUS) &&
		(irCode != REMOTE_BUTTON_TIME_M_PLUS) &&
		(irCode != REMOTE_BUTTON_TIME_H_MINUS) &&
		(irCode != REMOTE_BUTTON_TIME_M_MINUS) &&
		(irCode != REMOTE_BUTTON_HOUR_PLUS) &&
		(irCode != REMOTE_BUTTON_MINUTE_PLUS) &&
		(irCode != REMOTE_BUTTON_SECONDS) &&
		(irCode != REMOTE_BUTTON_MODE) &&
		(irCode != REMOTE_BUTTON_EXTMODE)) {

		switch (mode) {
#ifdef USE_STD_MODE_AMPM
		case STD_MODE_AMPM:
#endif
#ifdef USE_STD_MODE_SECONDS
		case STD_MODE_SECONDS:
#endif
#ifdef USE_STD_MODE_DATE
		case STD_MODE_DATE:
#endif
#ifdef USE_STD_MODE_TEMP
		case STD_MODE_TEMP:
#endif
		case STD_MODE_BRIGHTNESS:
		case EXT_MODE_LANGUAGE:
		case EXT_MODE_LDR_MODE:
			enableFallBackCounter(settings.getJumpToNormalTimeout());
			break;
#ifndef LED_DRIVER_DEFAULT
		case EXT_MODE_COLOR:
			enableFallBackCounter(2);
			break;
#endif
		default:
			disableFallBackCounter();
			break;
		}
	}

	// Werte speichern (die Funktion speichert nur bei geaenderten Werten)...
	settings.saveToEEPROM();
}
#endif

/******************************************************************************
   Den DCF77-Empfaenger ein-/ausschalten.
******************************************************************************/

#ifdef DCF77
void enableDcf(boolean enable) {
	if (enable) {
#ifdef DEBUG_DCF77
		Serial.println(F("DCF77: on"));
#endif
		digitalWrite(PIN_DCF77_PON, LOW);
	}
	else {
#ifdef DEBUG_DCF77
		Serial.println(F("DCF77: off"));
#endif
		digitalWrite(PIN_DCF77_PON, HIGH);
	}
}
#endif

/******************************************************************************
   Korrekte Daten (auf Basis der Pruefbits) vom DCF-Empfaenger
   bekommen. Sicherheitshalber gegen Zeitabstaende der RTC pruefen.
******************************************************************************/

#ifdef DCF77
void manageNewDCF77Data() {
	rtc.readTime();
	dcf77Helper.addSample(&dcf77, &rtc);
	if (dcf77Helper.samplesOk()) {
		rtc.set(&dcf77);
		resetSeconds();
		DEBUG_PRINTLN(F("DCF77-Time written to RTC."));
#ifdef USE_EXT_MODE_DCF_SYNC
		dcf77.setDcf77SuccessSync();
#endif
#ifdef AUTO_JUMP_BLANK
		if ((mode == STD_MODE_BLANK) || ((mode == STD_MODE_NIGHT) && !isCurrentTimeInNightRange())) {
			setMode(STD_MODE_NORMAL);
			ledDriver.wakeUp();
		}
#endif
	}
}
#endif

/******************************************************************************
   Display aus- bzw. einschalten.
******************************************************************************/

void setDisplayToBlank() {
	mode = STD_MODE_BLANK;
	ledDriver.shutDown();
	DEBUG_PRINTLN(F("LED-Driver: ShutDown"));
}

void setDisplayToResume() {
	mode = lastMode;
	ledDriver.wakeUp();
	DEBUG_PRINTLN(F("LED-Driver: WakeUp"));
}

void setDisplayToToggle() {
	if ((mode != STD_MODE_BLANK) && (mode != STD_MODE_NIGHT)) {
		setDisplayToBlank();
	}
	else {
		setDisplayToResume();
	}
}

/******************************************************************************
   Helligkeit des Display einstellen.
******************************************************************************/

void setDisplayBrighter() {
	if ((!settings.getUseLdr()) && (settings.getBrightness() < LDR_MAX_PERCENT)) {
		int8_t b = settings.getBrightness() + (LDR_MAX_PERCENT - LDR_MIN_PERCENT) / 9;
		if (b > LDR_MAX_PERCENT) {
			b = LDR_MAX_PERCENT;
		}
		setDisplayBrightness(b);
	}
}

void setDisplayDarker() {
	if (!settings.getUseLdr() && (settings.getBrightness() > LDR_MIN_PERCENT)) {
		int8_t i = settings.getBrightness() - (LDR_MAX_PERCENT - LDR_MIN_PERCENT) / 9;
		if (i < LDR_MIN_PERCENT) {
			i = LDR_MIN_PERCENT;
		}
		setDisplayBrightness(i);
	}
}

void setDisplayBrightness(byte brightness) {
	settings.setBrightness(brightness);
	settings.saveToEEPROM();
	ledDriver.setBrightness(brightness);
}

/******************************************************************************
   Timeout to switch back to time.
******************************************************************************/

void enableFallBackCounter(byte timeoutSec) {
	fallBackCounter = timeoutSec;
}

void disableFallBackCounter() {
	fallBackCounter = 0;
}

void updateFallBackCounter() {
	fallBackCounter--;
	if (!fallBackCounter) {
#ifdef USE_EXT_MODE_NIGHT_OFF
		if ((mode != EXT_MODE_NIGHT_OFF) && (mode != EXT_MODE_NIGHT_ON)) {
			setMode(STD_MODE_NORMAL);
		}
#else
		setMode(STD_MODE_NORMAL);
#endif
	}
}

/******************************************************************************
   Set time.
******************************************************************************/

void incDecMinutes(boolean inc) {
	if (inc) {
		rtc.incMinutes();
	}
	else {
		rtc.decMinutes();
	}
	resetSeconds();
	DEBUG_PRINT(F("Minute is now: "));
	DEBUG_PRINTLN(rtc.getMinutes());
}

void incDecHours(boolean inc) {
	if (inc) {
		rtc.incHours();
	}
	else {
		rtc.decHours();
	}
	resetSeconds();
	DEBUG_PRINT(F("Hour is now: "));
	DEBUG_PRINTLN(rtc.getHours());
}

void resetSeconds() {
	rtc.setSeconds(0);
	rtc.writeTime();
	rtc.readTime();
	helperSeconds = rtc.getSeconds();
}

/******************************************************************************
   Set mode.
******************************************************************************/

void setMode(Mode a_mode) {
	lastMode = mode;
	mode = a_mode;
}

/******************************************************************************
   isCurrentTimeInNightRange.
******************************************************************************/

bool isCurrentTimeInNightRange() {
	return (((settings.getNightModeTime(false)->getMinutesOfDay(0) < settings.getNightModeTime(true)->getMinutesOfDay(0)) &&
		((rtc.getMinutesOfDay(0) > settings.getNightModeTime(false)->getMinutesOfDay(0)) &&
		(rtc.getMinutesOfDay(0) < settings.getNightModeTime(true)->getMinutesOfDay(0)))) ||
			((settings.getNightModeTime(false)->getMinutesOfDay(0) > settings.getNightModeTime(true)->getMinutesOfDay(0)) &&
		((rtc.getMinutesOfDay(0) > settings.getNightModeTime(false)->getMinutesOfDay(0)) ||
				(rtc.getMinutesOfDay(0) < settings.getNightModeTime(true)->getMinutesOfDay(0)))));
}

/******************************************************************************
   Aenderung der Anzeige als Funktion fuer den Interrupt, der ueber das SQW-Signal
   der Real-Time-Clock gesetzt wird. Da die Wire-Bibliothek benutzt wird, kann man
   den Interrupt nicht direkt benutzen, sondern muss eine Hilfsvariable setzen, die
   dann in loop() ausgewertet wird.
******************************************************************************/

void updateFromRtc() {
	needsUpdateFromRtc = true;
	helperSeconds++;
	if (fallBackCounter > 0) {
		if (mode != STD_MODE_BLANK) {
			updateFallBackCounter();
		}
	}
}

/******************************************************************************
   DEBUG: Den freien Specher abschaetzen.
******************************************************************************/

#ifdef DEBUG
int freeRam() {
	extern int __heap_start, *__brkval;
	int v;
	return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}
#endif

/******************************************************************************
   Set all defaults in EEPROM.
******************************************************************************/

void factoryReset() {
	Serial.println(F("*** Set all defaults! *** "));
	settings.resetToDefault();
	settings.saveToEEPROM();
}

/******************************************************************************
   DEBUG: Render the Matrix to console.
******************************************************************************/

#ifdef DEBUG_MATRIX
void debugScreenBuffer(word ScreenBuffer[]) {
	const char buchstabensalat[][17] PROGMEM = {
	  {"ESKISTAFUNF2"},
	  {"ZEHNZWANZIG1"},
	  {"DREIVIERTEL4"},
	  {"VORFUNKNACH3"},
	  {"HALBAELFUNFA"},
	  {"EINSXAMZWEI_"},
	  {"DREIPMJVIER_"},
	  {"SECHSNLACHT_"},
	  {"SIEBENZWOLF_"},
	  {"ZEHNEUNKUHR_"}
	};
	Serial.println(F("\033[0;0H")); // Set cursor to 0, 0 position in console.
	Serial.println(F(" -----------"));
	for (byte zeile = 0; zeile < 10; zeile++) {
		word leds = ScreenBuffer[zeile];
		char spalte[16];
		for (int i = 15; i >= 0; i--) {
			spalte[i] = ((leds & 1) ? buchstabensalat[zeile][i] : ' ');
			leds = leds >> 1;
		}
		Serial.print('|');
		for (byte i = 0; i < 11; i++) {
			Serial.print(spalte[i]);
		}
		Serial.print('|');
		Serial.println(spalte[11]); // Corner LEDs
	}
	Serial.println(F(" -----------"));
}
#endif

