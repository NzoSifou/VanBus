/*
 * VanBus: PacketParser - try to parse the packets, received on the VAN comfort bus, and print the result on the
 *   serial port.
 *
 * Written by Erik Tromp
 *
 * Version 0.2.1 - April, 2021
 *
 * MIT license, all text above must be included in any redistribution.
 *
 * -----
 * Description
 *
 * Parse the packets as received on the VAN comfort bus. These packets contain information about:
 * - Instrument panel
 * - Power train messages, sounds and warning lamps
 * - Lighting-signalling messages, sounds and warning lamps
 * - Driver information messages, sounds and warning lamps
 * - Wash/wipe messages, sounds and warning lamps
 * - Doors and protections messages, sounds and warning lamps
 * - Instruments and controls messages, sounds and warning lamps
 * - Driving assistance messages, sounds and warning lamps
 * - Comfort and convenience messages, sounds and warning lamps
 * - Accessory messages, sounds and indicator lamps
 * - Display systems  
 * - Audio / telematics / telephone
 * - Audio / multimedia
 * - Navigation
 * - Heating / air conditioning
 * - Additional heating
 * - Heating equipment
 * - and probably more
 *
 * Notes:
 * - All parsing done here is highly experimental, gathered together from various sources and from doing experiments
 *   on my own vehicle (Peugeot 406 Estate with DAM number 9586). Please be suspicious to any interpretation that is
 *   done inside this file. Your mileage may vary.
 *
 * -----
 * Wiring
 *
 * See paragraph 'Wiring' in the other example sketch, 'VanBusDump.ino'.
 *
 */

#include <ESP8266WiFi.h>
#include <VanBusRx.h>

#if defined ARDUINO_ESP8266_GENERIC || defined ARDUINO_ESP8266_ESP01
// For ESP-01 board we use GPIO 2 (internal pull-up, keep disconnected or high at boot time)
#define D2 (2)
#endif
int RX_PIN = D2; // Set to GPIO pin connected to VAN bus transceiver output

// Packet parsing
enum VanPacketParseResult_t
{
    VAN_PACKET_DUPLICATE  = 1, // Packet was the same as the last with this IDEN field
    VAN_PACKET_PARSE_OK = 0,  // Packet was parsed OK
    VAN_PACKET_PARSE_CRC_ERROR = -1,  // Packet had a CRC error
    VAN_PACKET_PARSE_UNEXPECTED_LENGTH = -2,  // Packet had unexpected length
    VAN_PACKET_PARSE_UNRECOGNIZED_IDEN = -3,  // Packet had unrecognized IDEN field
    VAN_PACKET_PARSE_TO_BE_DECODED = -4  // IDEN recognized but the correct parsing of this packet is not yet known
}; // enum VanPacketParseResult_t

// VAN IDENtifiers
#define VIN_IDEN 0xE24
#define ENGINE_IDEN 0x8A4
#define HEAD_UNIT_STALK_IDEN 0x9C4
#define LIGHTS_STATUS_IDEN 0x4FC
#define DEVICE_REPORT 0x8C4
#define CAR_STATUS1_IDEN 0x564
#define CAR_STATUS2_IDEN 0x524
#define DASHBOARD_IDEN 0x824
#define DASHBOARD_BUTTONS_IDEN 0x664
#define HEAD_UNIT_IDEN 0x554
#define TIME_IDEN 0x984
#define AUDIO_SETTINGS_IDEN 0x4D4
#define MFD_STATUS_IDEN 0x5E4
#define AIRCON1_IDEN 0x464
#define AIRCON2_IDEN 0x4DC
#define CDCHANGER_IDEN 0x4EC
#define SATNAV_STATUS_1_IDEN 0x54E
#define SATNAV_STATUS_2_IDEN 0x7CE
#define SATNAV_STATUS_3_IDEN 0x8CE
#define SATNAV_GUIDANCE_DATA_IDEN 0x9CE
#define SATNAV_GUIDANCE_IDEN 0x64E
#define SATNAV_REPORT_IDEN 0x6CE
#define MFD_TO_SATNAV_IDEN 0x94E
#define SATNAV_TO_MFD_IDEN 0x74E
#define SATNAV_DOWNLOADING_IDEN 0x6F4
#define SATNAV_DOWNLOADED1_IDEN 0xA44
#define SATNAV_DOWNLOADED2_IDEN 0xAC4
#define WHEEL_SPEED_IDEN 0x744
#define ODOMETER_IDEN 0x8FC
#define COM2000_IDEN 0x450
#define CDCHANGER_COMMAND_IDEN 0x8EC
#define MFD_TO_HEAD_UNIT_IDEN 0x8D4
#define AIR_CONDITIONER_DIAG_IDEN 0xADC
#define AIR_CONDITIONER_DIAG_COMMAND_IDEN 0xA5C
#define ECU_IDEN 0xB0E

#define SERIAL Serial

// Often used string constants
static const char PROGMEM emptyStr[] = "";
static const char PROGMEM indentStr[] = "    ";
static const char PROGMEM onStr[] = "ON";
static const char PROGMEM offStr[] = "OFF";
static const char PROGMEM yesStr[] = "YES";
static const char PROGMEM noStr[] = "NO";
static const char PROGMEM presentStr[] = "PRESENT";
static const char PROGMEM notPresentStr[] = "NOT_PRESENT";
static const char PROGMEM noneStr[] = "NONE";
static const char PROGMEM updatedStr[] = "(UPD)";
static const char PROGMEM notApplicable2Str[] = "--";
static const char PROGMEM notApplicable3Str[] = "---";
static const char PROGMEM toBeDecodedStr[] = "[to be decoded]";
static const char PROGMEM unexpectedPacketLengthStr[] = "[unexpected packet length]";

// Uses statically allocated buffer, so don't call twice within the same printf invocation 
char* ToStr(uint8_t data)
{
    #define MAX_UINT8_STR_SIZE 4
    static char buffer[MAX_UINT8_STR_SIZE];
    sprintf_P(buffer, PSTR("%u"), data);

    return buffer;
} // ToStr

// Uses statically allocated buffer, so don't call twice within the same printf invocation 
char* ToHexStr(uint8_t data)
{
    #define MAX_UINT8_HEX_STR_SIZE 5
    static char buffer[MAX_UINT8_HEX_STR_SIZE];
    sprintf_P(buffer, PSTR("0x%02X"), data);

    return buffer;
} // ToHexStr

// Uses statically allocated buffer, so don't call twice within the same printf invocation 
char* ToHexStr(uint16_t data)
{
    #define MAX_UINT16_HEX_STR_SIZE 7
    static char buffer[MAX_UINT16_HEX_STR_SIZE];
    sprintf_P(buffer, PSTR("0x%04X"), data);

    return buffer;
} // ToHexStr

// Uses statically allocated buffer, so don't call twice within the same printf invocation 
char* ToHexStr(uint8_t data1, uint8_t data2)
{
    #define MAX_2_UINT8_HEX_STR_SIZE 10
    static char buffer[MAX_2_UINT8_HEX_STR_SIZE];
    sprintf_P(buffer, PSTR("0x%02X-0x%02X"), data1, data2);

    return buffer;
} // ToHexStr

// Tuner band
enum TunerBand_t
{
    TB_NONE = 0,
    TB_FM1,
    TB_FM2,
    TB_FM3,  // Never seen, just guessing
    TB_FMAST,  // Auto-station
    TB_AM,
    TB_PTY_SELECT = 7
}; // enum TunerBand_t

// Returns a PSTR (allocated in flash, saves RAM). In printf formatter use "%S" (capital S) instead of "%s".
const char* TunerBandStr(uint8_t data)
{
    return
        data == TB_NONE ? noneStr :
        data == TB_FM1 ? PSTR("FM1") :
        data == TB_FM2 ? PSTR("FM2") :
        data == TB_FM3 ? PSTR("FM3") :  // Never seen, just guessing
        data == TB_FMAST ? PSTR("FMAST") :
        data == TB_AM ? PSTR("AM") :
        data == TB_PTY_SELECT ? PSTR("PTY_SELECT") :  // When selecting PTY to search for
        ToHexStr(data);
} // TunerBandStr

// Tuner search mode
// Bits:
//  2  1  0
// ---+--+---
//  0  0  0 : Not searching
//  0  0  1 : Manual tuning
//  0  1  0 : Searching by frequency
//  0  1  1 : 
//  1  0  0 : Searching for station with matching PTY
//  1  0  1 : 
//  1  1  0 : 
//  1  1  1 : Auto-station search in the FMAST band (long-press "Radio Band" button)
enum TunerSearchMode_t
{
    TS_NOT_SEARCHING = 0,
    TS_MANUAL = 1,
    TS_BY_FREQUENCY = 2,
    TS_BY_MATCHING_PTY = 4,
    TS_FM_AST = 7
}; // enum TunerSearchMode_t

// Returns a PSTR (allocated in flash, saves RAM). In printf formatter use "%S" (capital S) instead of "%s".
const char* TunerSearchModeStr(uint8_t data)
{
    return
        data == TS_NOT_SEARCHING ? PSTR("NOT_SEARCHING") :
        data == TS_MANUAL ? PSTR("MANUAL_TUNING") :
        data == TS_BY_FREQUENCY ? PSTR("SEARCHING_BY_FREQUENCY") :
        data == TS_BY_MATCHING_PTY ? PSTR("SEARCHING_MATCHING_PTY") : // Searching for station with matching PTY
        data == TS_FM_AST ? PSTR("FM_AST_SCAN") : // Auto-station scan in the FMAST band (long-press "Radio Band" button)
        ToHexStr(data);
} // TunerSearchModeStr

// Returns a PSTR (allocated in flash, saves RAM). In printf formatter use "%S" (capital S) instead of "%s".
const char* PtyStr(uint8_t ptyCode)
{
    // See also:
    // https://www.electronics-notes.com/articles/audio-video/broadcast-audio/rds-radio-data-system-pty-codes.php
    return
        ptyCode == 0 ? PSTR("Not defined") :
        ptyCode == 1 ? PSTR("News") :
        ptyCode == 2 ? PSTR("Current affairs") :
        ptyCode == 3 ? PSTR("Information") :
        ptyCode == 4 ? PSTR("Sport") :
        ptyCode == 5 ? PSTR("Education") :
        ptyCode == 6 ? PSTR("Drama") :
        ptyCode == 7 ? PSTR("Culture") :
        ptyCode == 8 ? PSTR("Science") :
        ptyCode == 9 ? PSTR("Varied") :
        ptyCode == 10 ? PSTR("Pop Music") :
        ptyCode == 11 ? PSTR("Rock Music") :
        ptyCode == 12 ? PSTR("Easy Listening") :  // also: "Middle of the road music"
        ptyCode == 13 ? PSTR("Light Classical") :
        ptyCode == 14 ? PSTR("Serious Classical") :
        ptyCode == 15 ? PSTR("Other Music") :
        ptyCode == 16 ? PSTR("Weather") :
        ptyCode == 17 ? PSTR("Finance") :
        ptyCode == 18 ? PSTR("Children's Programmes") :
        ptyCode == 19 ? PSTR("Social Affairs") :
        ptyCode == 20 ? PSTR("Religion") :
        ptyCode == 21 ? PSTR("Phone-in") :
        ptyCode == 22 ? PSTR("Travel") :
        ptyCode == 23 ? PSTR("Leisure") :
        ptyCode == 24 ? PSTR("Jazz Music") :
        ptyCode == 25 ? PSTR("Country Music") :
        ptyCode == 26 ? PSTR("National Music") :
        ptyCode == 27 ? PSTR("Oldies Music") :
        ptyCode == 28 ? PSTR("Folk Music") :
        ptyCode == 29 ? PSTR("Documentary") :
        ptyCode == 30 ? PSTR("Alarm Test") :
        ptyCode == 31 ? PSTR("Alarm") :
        ToHexStr(ptyCode);
} // PtyStr

// Returns a PSTR (allocated in flash, saves RAM). In printf formatter use "%S" (capital S) instead of "%s".
const char* RadioPiCountry(uint8_t countryCode)
{
    // https://radio-tv-nederland.nl/rds/PI%20codes%20Europe.jpg
    // More than one country is assigned to the same code, just listing the most likely.
    return
        countryCode == 0x01 || countryCode == 0x0D ? PSTR("Germany") :
        countryCode == 0x02 ? PSTR("Ireland") :
        countryCode == 0x03 ? PSTR("Poland") :
        countryCode == 0x04 ? PSTR("Switzerland") :
        countryCode == 0x05 ? PSTR("Italy") :
        countryCode == 0x06 ? PSTR("Belgium") :
        countryCode == 0x07 ? PSTR("Luxemburg") :
        countryCode == 0x08 ? PSTR("Netherlands") :
        countryCode == 0x09 ? PSTR("Denmark") :
        countryCode == 0x0A ? PSTR("Austria") :
        countryCode == 0x0B ? PSTR("Hungary") :
        countryCode == 0x0C ? PSTR("United Kingdom") :
        countryCode == 0x0E ? PSTR("Spain") :
        countryCode == 0x0F ? PSTR("France") :
        ToHexStr(countryCode);
} // RadioPiCountry

// Returns a PSTR (allocated in flash, saves RAM). In printf formatter use "%S" (capital S) instead of "%s".
const char* RadioPiAreaCoverage(uint8_t coverageCode)
{
    // https://www.pira.cz/rds/show.asp?art=rds_encoder_support
    return
        coverageCode == 0x00 ? PSTR("local") :
        coverageCode == 0x01 ? PSTR("international") :
        coverageCode == 0x02 ? PSTR("national") :
        coverageCode == 0x03 ? PSTR("supra-regional") :
        PSTR("regional");
} // RadioPiAreaCoverage

// Seems to be used in bus packets with IDEN:
//
// - SATNAV_REPORT_IDEN (0x6CE): data[1]. Following values of data[1] seen:
//   0x02, 0x05, 0x08, 0x09, 0x0E, 0x0F, 0x10, 0x11, 0x13, 0x1B, 0x1D
// - SATNAV_TO_MFD_IDEN (0x74E): data[1]. Following values of data[1] seen:
//   0x02, 0x05, 0x08, 0x09, 0x1B, 0x1C
// - MFD_TO_SATNAV_IDEN (0x94E): data[0]. Following values of data[1] seen:
//   0x02, 0x05, 0x06, 0x08, 0x09, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x1B, 0x1C, 0x1D
//
enum SatNavRequest_t
{
    SR_ENTER_COUNTRY = 0x00,  // Never seen, just guessing
    SR_ENTER_PROVINCE = 0x01,  // Never seen, just guessing
    SR_ENTER_CITY = 0x02,
    SR_ENTER_DISTRICT = 0x03,  // Never seen, just guessing
    SR_ENTER_NEIGHBORHOOD = 0x04,  // Never seen, just guessing
    SR_ENTER_STREET = 0x05,
    SR_ENTER_HOUSE_NUMBER = 0x06,  // Range of house numbers to choose from
    SR_ENTER_HOUSE_NUMBER_LETTER = 0x07,  // Never seen, just guessing
    SR_PLACE_OF_INTEREST_CATEGORY = 0x08,
    SR_PLACE_OF_INTEREST = 0x09,
    SR_LAST_DESTINATION = 0x0E,
    SR_NEXT_STREET = 0x0F,  // Shown during SatNav guidance in the (solid line) top box
    SR_CURRENT_STREET = 0x10,  // Shown during SatNav guidance in the (dashed line) bottom box
    SR_PRIVATE_ADDRESS = 0x11,
    SR_BUSINESS_ADDRESS = 0x12,
    SR_SOFTWARE_MODULE_VERSIONS = 0x13,
    SR_PRIVATE_ADDRESS_LIST = 0x1B,
    SR_BUSINESS_ADDRESS_LIST = 0x1C,
    SR_DESTINATION = 0x1D
}; // enum SatNavRequest_t

// Returns a PSTR (allocated in flash, saves RAM). In printf formatter use "%S" (capital S) instead of "%s".
const char* SatNavRequestStr(uint8_t data)
{
    return
        data == SR_ENTER_COUNTRY ? PSTR("ENTER_COUNTRY") :
        data == SR_ENTER_PROVINCE ? PSTR("ENTER_PROVINCE") :
        data == SR_ENTER_CITY ? PSTR("ENTER_CITY") :
        data == SR_ENTER_DISTRICT ? PSTR("ENTER_DISTRICT") :
        data == SR_ENTER_NEIGHBORHOOD ? PSTR("ENTER_NEIGHBORHOOD") :
        data == SR_ENTER_STREET ? PSTR("ENTER_STREET") :
        data == SR_ENTER_HOUSE_NUMBER ? PSTR("ENTER_HOUSE_NUMBER") :
        data == SR_ENTER_HOUSE_NUMBER_LETTER ? PSTR("ENTER_HOUSE_NUMBER_LETTER") :
        data == SR_PLACE_OF_INTEREST_CATEGORY ? PSTR("PLACE_OF_INTEREST_CATEGORY") :
        data == SR_PLACE_OF_INTEREST ? PSTR("PLACE_OF_INTEREST") :
        data == SR_LAST_DESTINATION ? PSTR("LAST_DESTINATION") :
        data == SR_NEXT_STREET ? PSTR("NEXT_STREET") :
        data == SR_CURRENT_STREET ? PSTR("CURRENT_STREET") :
        data == SR_PRIVATE_ADDRESS ? PSTR("PRIVATE_ADDRESS") :
        data == SR_BUSINESS_ADDRESS ? PSTR("BUSINESS_ADDRESS") :
        data == SR_SOFTWARE_MODULE_VERSIONS ? PSTR("SOFTWARE_MODULE_VERSIONS") :
        data == SR_PRIVATE_ADDRESS_LIST ? PSTR("PRIVATE_ADDRESS_LIST") :
        data == SR_BUSINESS_ADDRESS_LIST ? PSTR("BUSINESS_ADDRESS_LIST") :
        data == SR_DESTINATION ? PSTR("DESTINATION") :
        ToHexStr(data);
} // SatNavRequestStr

// Attempt to show a detailed SatNav guidance instruction in "Ascii art"
//
// A detailed SatNav guidance instruction consists of 8 bytes:
// * 0   : turn angle in increments of 22.5 degrees, measured clockwise, starting with 0 at 6 o-clock.
//         E.g.: 0x4 == 90 deg left, 0x8 = 180 deg = straight ahead, 0xC = 270 deg = 90 deg right.
//         Turn angle is shown here as (vertical) (d|sl)ash ('\', '|', '/', or '-').
// * 1   : always 0x00 ??
// * 2, 3: bit pattern indicating which legs are present in the junction or roundabout. Each bit set is for one leg.
//         Lowest bit of byte 3 corresponds to the leg of 0 degrees (straight down, which is
//         always there, because that is where we are currently driving), running clockwise up to the
//         highest bit of byte 2, which corresponds to a leg of 337.5 degrees (very sharp right).
//         A leg is shown here as a '.'.
// * 4, 5: bit pattern indicating which legs in the junction are "no entry". The coding of the bits is the same
//         as for bytes 2 and 3.
//         A "no-entry" is shown here as "(-)".
// * 6   : always 0x00 ??
// * 7   : always 0x00 ??
//
void PrintGuidanceInstruction(const uint8_t data[8])
{
    // String constants
    static const char PROGMEM entryStr[] = "   ";
    static const char PROGMEM noEntryStr[] = "(-)";
    static const char PROGMEM legStr[] = ".";
    static const char PROGMEM noLegStr[] = " ";
    static const char PROGMEM goStraightAheadStr[] = "|";
    static const char PROGMEM turnLeftStr[] = "-";
    static const char PROGMEM turnRightStr[] = "-";
    static const char PROGMEM turnHalfLeftStr[] = "\\";
    static const char PROGMEM turnSharpRightStr[] = "\\";
    static const char PROGMEM turnHalfRightStr[] = "/";
    static const char PROGMEM turnSharpLeftStr[] = "/";

    SERIAL.printf_P(PSTR("      %S%S%S%S%S\n"),
        data[5] & 0x40 ? noEntryStr : entryStr,
        data[5] & 0x80 ? noEntryStr : entryStr,
        data[4] & 0x01 ? noEntryStr : entryStr,
        data[4] & 0x02 ? noEntryStr : entryStr,
        data[4] & 0x04 ? noEntryStr : entryStr
    );
    SERIAL.printf_P(PSTR("       %S  %S  %S  %S  %S\n"),
        data[0] == 6 ? turnHalfLeftStr : data[3] & 0x40 ? legStr : noLegStr,
        data[0] == 7 ? turnHalfLeftStr : data[3] & 0x80 ? legStr : noLegStr,
        data[0] == 8 ? goStraightAheadStr : data[2] & 0x01 ? legStr : noLegStr,
        data[0] == 9 ? turnHalfRightStr : data[2] & 0x02 ? legStr : noLegStr,
        data[0] == 10 ? turnHalfRightStr : data[2] & 0x04 ? legStr : noLegStr
    );
    SERIAL.printf_P(PSTR("    %S%S           %S%S\n"),
        data[5] & 0x20 ? noEntryStr : entryStr,
        data[0] == 5 ? turnLeftStr : data[3] & 0x20 ? legStr : noLegStr,
        data[0] == 11 ? turnRightStr : data[2] & 0x08 ? legStr : noLegStr,
        data[4] & 0x08 ? noEntryStr : entryStr
    );
    SERIAL.printf_P(PSTR("    %S%S     +     %S%S\n"),
        data[5] & 0x10 ? noEntryStr : entryStr,
        data[0] == 4 ? turnLeftStr : data[3] & 0x10 ? legStr : noLegStr,
        data[0] == 12 ? turnRightStr : data[2] & 0x10 ? legStr : noLegStr,
        data[4] & 0x10 ? noEntryStr : entryStr
    );
    SERIAL.printf_P(PSTR("    %S%S     |     %S%S\n"),
        data[5] & 0x08 ? noEntryStr : entryStr,
        data[0] == 3 ? turnLeftStr : data[3] & 0x08 ? legStr : noLegStr,
        data[0] == 13 ? turnRightStr : data[2] & 0x20 ? legStr : noLegStr,
        data[4] & 0x20 ? noEntryStr : entryStr
    );
    SERIAL.printf_P(PSTR("       %S  %S  |  %S  %S\n"),
        data[0] == 2 ? turnSharpLeftStr : data[3] & 0x04 ? legStr : noLegStr,
        data[0] == 1 ? turnSharpLeftStr : data[3] & 0x02 ? legStr : noLegStr,
        data[0] == 14 ? turnSharpRightStr : data[3] & 0x40 ? legStr : noLegStr,
        data[0] == 15 ? turnSharpRightStr : data[3] & 0x80 ? legStr : noLegStr
    );
    SERIAL.printf_P(PSTR("      %S%S%S%S%S\n"),
        data[5] & 0x04 ? noEntryStr : entryStr,
        data[5] & 0x02 ? noEntryStr : entryStr,
        data[5] & 0x01 ? noEntryStr : entryStr,
        data[4] & 0x40 ? noEntryStr : entryStr,
        data[4] & 0x80 ? noEntryStr : entryStr
    );
} // PrintGuidanceInstruction

// Parse a VAN packet
VanPacketParseResult_t ParseVanPacket(TVanPacketRxDesc* pkt)
{
    if (! pkt->CheckCrc()) return VAN_PACKET_PARSE_CRC_ERROR;

    int dataLen = pkt->DataLen();
    if (dataLen < 0 || dataLen > 28) return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;

    const uint8_t* data = pkt->Data();
    uint16_t iden = pkt->Iden();

    switch (iden)
    {
        case VIN_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#E24
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#E24

            // Print only if not duplicate of previous packet
            static uint8_t packetData[VAN_MAX_DATA_BYTES] = "";  // Previous packet data
            if (memcmp(data, packetData, dataLen) == 0) return VAN_PACKET_DUPLICATE;
            memcpy(packetData, data, dataLen);

            SERIAL.print(F("--> VIN: "));

            if (dataLen != 17)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            char vinTxt[18];
            memcpy(vinTxt, data, 17);
            vinTxt[17] = 0;

            SERIAL.printf_P(PSTR("%s\n"), vinTxt);
        }
        break;

        case ENGINE_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#8A4
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#8A4

            // Print only if not duplicate of previous packet
            static uint8_t packetData[VAN_MAX_DATA_BYTES] = "";  // Previous packet data
            if (memcmp(data, packetData, dataLen) == 0) return VAN_PACKET_DUPLICATE;
            memcpy(packetData, data, dataLen);

            SERIAL.print(F("--> Engine: "));

            if (dataLen != 7)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            // TODO - Always "CLOSE", even if open. Actual status of "door open" icon on instrument cluster is found in
            // packet with IDEN 0x4FC (LIGHTS_STATUS_IDEN)
            //data[1] & 0x08 ? "door=OPEN" : "door=CLOSE",

            char floatBuf[3][MAX_FLOAT_SIZE];
            SERIAL.printf_P(
                PSTR(
                    "dash_light=%S, dash_actual_brightness=%u; contact_key_position=%S; engine=%S;\n"
                    "    economy_mode=%S; in_reverse=%S; trailer=%S; water_temp=%S; odometer=%s; exterior_temperature=%s\n"
                ),
                data[0] & 0x80 ? PSTR("FULL") : PSTR("DIMMED (LIGHTS ON)"),
                data[0] & 0x0F,

                (data[1] & 0x03) == 0x00 ? offStr :
                (data[1] & 0x03) == 0x01 ? PSTR("ACC") :
                (data[1] & 0x03) == 0x03 ? onStr :
                (data[1] & 0x03) == 0x02 ? PSTR("START_ENGINE") :
                ToHexStr((uint8_t)(data[1] & 0x03)),

                data[1] & 0x04 ? PSTR("RUNNING") : offStr,
                data[1] & 0x10 ? onStr : offStr,
                data[1] & 0x20 ? yesStr : noStr,
                data[1] & 0x40 ? presentStr : notPresentStr,
                data[2] == 0xFF ? notApplicable3Str : FloatToStr(floatBuf[0], data[2] - 39, 0),  // TODO - or: data[2] / 2
                FloatToStr(floatBuf[1], ((uint32_t)data[3] << 16 | (uint32_t)data[4] << 8 | data[5]) / 10.0, 1),
                FloatToStr(floatBuf[2], (data[6] - 0x50) / 2.0, 1)
            );
        }
        break;

        case HEAD_UNIT_STALK_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#9C4
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#9C4

            // Print only if not duplicate of previous packet
            static uint8_t packetData[VAN_MAX_DATA_BYTES] = "";  // Previous packet data
            if (memcmp(data, packetData, dataLen) == 0) return VAN_PACKET_DUPLICATE;
            memcpy(packetData, data, dataLen);

            SERIAL.print(F("--> Head unit stalk: "));

            if (dataLen != 2)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            SERIAL.printf_P(
                PSTR("button=%S%S%S%S%S, wheel=%d, wheel_rollover=%u\n"),
                data[0] & 0x80 ? PSTR("NEXT ") : emptyStr,
                data[0] & 0x40 ? PSTR("PREV ") : emptyStr,
                data[0] & 0x08 ? PSTR("VOL_UP ") : emptyStr,
                data[0] & 0x04 ? PSTR("VOL_DOWN ") : emptyStr,
                data[0] & 0x02 ? PSTR("SOURCE ") : emptyStr,
                data[1] - 0x80,
                data[0] >> 4 & 0x03
            );
        }
        break;

        case LIGHTS_STATUS_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#4FC
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#4FC_1
            // https://github.com/morcibacsi/PSAVanCanBridge/blob/master/src/Van/Structs/VanInstrumentClusterV1Structs.h

            // Print only if not duplicate of previous packet
            static uint8_t packetData[VAN_MAX_DATA_BYTES] = "";  // Previous packet data
            if (memcmp(data, packetData, dataLen) == 0) return VAN_PACKET_DUPLICATE;
            memcpy(packetData, data, dataLen);

            // Examples:
            // Raw: #1987 (12/15) 16 0E 4FC WA0 90-00-01-AE-F0-00-FF-FF-22-48-FF-7A-56 ACK OK 7A56 CRC_OK

            SERIAL.print(F("--> Lights status: "));

            if (dataLen != 11 && dataLen != 14)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            // Vehicles made until 2002?

            SERIAL.printf_P(PSTR("\n    - Instrument cluster: %SENABLED\n"), data[0] & 0x80 ? emptyStr : PSTR("NOT "));
            SERIAL.printf_P(PSTR("    - Speed regulator wheel: %S\n"), data[0] & 0x40 ? onStr : offStr);
            SERIAL.printf_P(PSTR("%S"), data[0] & 0x20 ? PSTR("    - Warning LED ON\n") : emptyStr);
            SERIAL.printf_P(PSTR("%S"), data[0] & 0x04 ? PSTR("    - Diesel glow plugs ON\n") : emptyStr);  // TODO - check
            SERIAL.printf_P(PSTR("%S"), data[1] & 0x01 ? PSTR("    - Door OPEN\n") : emptyStr);
            SERIAL.printf_P(
                PSTR("    - Remaing km to service: %u (dashboard shows: %u)\n"),
                ((uint16_t)data[2] << 8 | data[3]) * 20,
                (((uint16_t)data[2] << 8 | data[3]) * 20) / 100 * 100
            );

            if (data[5] & 0x02)
            {
                SERIAL.printf_P(
                    PSTR("    - Automatic gearbox: %S%S%S%S\n"),

                    (data[4] & 0x70) == 0x00 ? PSTR("P") :
                    (data[4] & 0x70) == 0x10 ? PSTR("R") :
                    (data[4] & 0x70) == 0x20 ? PSTR("N") :
                    (data[4] & 0x70) == 0x30 ? PSTR("D") :
                    (data[4] & 0x70) == 0x40 ? PSTR("4") :
                    (data[4] & 0x70) == 0x50 ? PSTR("3") :
                    (data[4] & 0x70) == 0x60 ? PSTR("2") :
                    (data[4] & 0x70) == 0x70 ? PSTR("1") :
                    ToHexStr((uint8_t)(data[4] & 0x70)),

                    data[4] & 0x08 ? PSTR(" - Snow") : emptyStr,
                    data[4] & 0x04 ? PSTR(" - Sport") : emptyStr,
                    data[4] & 0x80 ? PSTR(" (blinking)") : emptyStr
                );
            } // if

            SERIAL.printf_P(
                PSTR("    - Lights: %S%S%S%S%S%S\n"),
                data[5] & 0x80 ? PSTR("DIPPED_BEAM ") : emptyStr,
                data[5] & 0x40 ? PSTR("HIGH_BEAM ") : emptyStr,
                data[5] & 0x20 ? PSTR("FOG_FRONT ") : emptyStr,
                data[5] & 0x10 ? PSTR("FOG_REAR ") : emptyStr,
                data[5] & 0x08 ? PSTR("INDICATOR_RIGHT ") : emptyStr,
                data[5] & 0x04 ? PSTR("INDICATOR_LEFT ") : emptyStr
            );

            if (data[6] != 0xFF)
            {
                // If you see "29.2 Â°C", then set 'Remote character set' to 'UTF-8' in
                // PuTTY setting 'Window' --> 'Translation'
                SERIAL.printf_P(PSTR("    - Oil temperature: %d °C\n"), (int)data[6] - 40);  // Never seen this
            } // if
            //SERIAL.printf("    - Oil temperature (2): %d °C\n", (int)data[9] - 50);  // Other possibility?

            if (data[7] != 0xFF)
            {
                SERIAL.printf_P(PSTR("Fuel level: %u %%\n"), data[7]);  // Never seen this
            } // if

            SERIAL.printf_P(PSTR("    - Oil level: raw=%u,dash=%S\n"),
                data[8],
                data[8] <= 0x0B ? PSTR("------") :
                data[8] <= 0x19 ? PSTR("O-----") :
                data[8] <= 0x27 ? PSTR("OO----") :
                data[8] <= 0x35 ? PSTR("OOO---") :
                data[8] <= 0x43 ? PSTR("OOOO--") :
                data[8] <= 0x51 ? PSTR("OOOOO-") :
                PSTR("OOOOOO")
            );

            if (data[10] != 0xFF)
            {
                // Never seen this; I don't have LPG
                SERIAL.printf_P(PSTR("LPG fuel level: %S\n"),
                    data[10] <= 0x08 ? PSTR("1") :
                    data[10] <= 0x11 ? PSTR("2") :
                    data[10] <= 0x21 ? PSTR("3") :
                    data[10] <= 0x32 ? PSTR("4") :
                    data[10] <= 0x43 ? PSTR("5") :
                    data[10] <= 0x53 ? PSTR("6") :
                    PSTR("7")
                );
            } // if

            if (dataLen == 14)
            {
                // Vehicles made in/after 2004?

                // http://pinterpeti.hu/psavanbus/PSA-VAN.html#4FC_2

                SERIAL.printf_P(PSTR("Cruise control: %S\n"),
                    data[11] == 0x41 ? offStr :
                    data[11] == 0x49 ? PSTR("Cruise") :
                    data[11] == 0x59 ? PSTR("Cruise - speed") :
                    data[11] == 0x81 ? PSTR("Limiter") :
                    data[11] == 0x89 ? PSTR("Limiter - speed") :
                    ToHexStr(data[11])
                );

                SERIAL.printf_P(PSTR("Cruise control speed: %u\n"), data[12]);
            } // if
        }
        break;

        case DEVICE_REPORT:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#8C4
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#8C4

            if (dataLen < 1 || dataLen > 3)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            SERIAL.print(F("--> Device report: "));

            if (data[0] == 0x8A)
            {
                // I'm sure that these messages are sent by the head unit: when no other device than the head unit is
                // on the bus, these packets are seen (unACKed; ACKs appear when the MFD is plugged back in to the bus).

                // Examples:
                // Raw: #xxxx (xx/15)  8 0E 8C4 WA0 8A-24-40-9B-32 ACK OK 9B32 CRC_OK
                // Raw: #7820 (10/15)  8 0E 8C4 WA0 8A-21-40-3D-54 ACK OK 3D54 CRC_OK
                // Raw: #0345 ( 1/15)  8 0E 8C4 WA0 8A-28-40-F9-96 ACK OK F996 CRC_OK

                SERIAL.print("Head unit: ");

                if (dataLen != 3)
                {
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                // data[1] values seen:
                // 0x20: Tuner info - reply to 8D4 WA0 D1 (any source)
                // 0x21: Audio settings announcement (source = satnav, tuner or CD changer)
                // 0x22: Button press announcement (source = satnav, tuner or CD changer)
                // 0x24: Tuner info announcement (any source)
                // 0x28: Cassette tape presence announcement (any source)
                // 0x30: Internal CD presence announcement (any source)
                // 0x40: Tuner presets available - reply to 8D4 WA0 27-11
                // 0x60: Cassette tape data available - reply to 8D4 WA0 D2
                // 0x61: Audio settings announcement (source = cassette tape)
                // 0x62: Button press announcement (source = cassette tape)
                // 0x64: Start tape announcement (source = cassette tape)
                // 0x68: Info announcement (source = cassette tape)
                // 0xC0: Internal CD track data available - reply to 8D4 WA0 D6
                // 0xC1: Audio settings announcement (source = internal CD)
                // 0xC2: Button press announcement (source = internal CD)
                // 0xC4: Searching announcement (source = internal CD)
                // 0xD0: Track info announcement (source = internal CD)
                //
                SERIAL.printf_P(
                    PSTR("%S\n"),
                    data[1] == 0x20 ? PSTR("TUNER_REPLY") :
                    data[1] == 0x21 ? PSTR("AUDIO_SETTINGS_ANNOUNCE") :
                    data[1] == 0x22 ? PSTR("BUTTON_PRESS_ANNOUNCE") :
                    data[1] == 0x24 ? PSTR("TUNER_ANNOUNCEMENT") :
                    data[1] == 0x28 ? PSTR("TAPE_PRESENCE_ANNOUNCEMENT") :
                    data[1] == 0x30 ? PSTR("CD_PRESENT") :
                    data[1] == 0x40 ? PSTR("TUNER_PRESETS_REPLY") :
                    data[1] == 0x42 ? PSTR("TUNER_PRESET_MEMORIZE_BUTTON_PRESS_ANNOUNCE") :
                    data[1] == 0x60 ? PSTR("TAPE_INFO_REPLY") :
                    data[1] == 0x61 ? PSTR("TAPE_PLAYING_AUDIO_SETTINGS_ANNOUNCE") :
                    data[1] == 0x62 ? PSTR("TAPE_PLAYING_BUTTON_PRESS_ANNOUNCE") :
                    data[1] == 0x64 ? PSTR("TAPE_PLAYING_STARTING") :
                    data[1] == 0x68 ? PSTR("TAPE_PLAYING_INFO") :
                    data[1] == 0xC0 ? PSTR("INTERNAL_CD_TRACK_INFO_REPLY") :
                    data[1] == 0xC1 ? PSTR("INTERNAL_CD_PLAYING_AUDIO_SETTINGS_ANNOUNCE") :
                    data[1] == 0xC2 ? PSTR("INTERNAL_CD_PLAYING_BUTTON_PRESS_ANNOUNCE") :
                    data[1] == 0xC4 ? PSTR("INTERNAL_CD_PLAYING_SEARCHING") :
                    data[1] == 0xD0 ? PSTR("INTERNAL_CD_PLAYING_TRACK_INFO") :
                    ToHexStr(data[1])
                );

                // Possible bits in data[1]:
                // 0x01 - Audio settings announcement
                // 0x02 - Button press announcement
                // 0x04 - Status update (CD track or tuner info)
                // 0x08 - 
                // 0xF0 - 0x20 = Tuner (radio)
                //      - 0x30 = CD track found
                //      - 0x40 = Tuner preset
                //      - 0x60 = Cassette tape playing
                //      - 0xC0 = CD playing
                //      - 0xD0 = CD track info
                //
                // SERIAL.printf(
                    // "%s - %s\n",
                    // data[1] == 0x24 ? "TUNER" : // TUNER - STATUS_UPDATE_ANNOUNCE
                        // data[1] == 0x20 ? "TUNER" :  // TUNER - REPLY
                        // (data[1] & 0x0F) == 0x01 ? "HEAD_UNIT" :  // HEAD_UNIT - AUDIO_SETTINGS_ANNOUNCE
                        // (data[1] & 0x0F) == 0x02 ? "HEAD_UNIT" :  // HEAD_UNIT - BUTTON_PRESS_ANNOUNCE
                        // (data[1] & 0xF0) == 0x30 ? "CD_PLAYING" :
                        // (data[1] & 0xF0) == 0x40 ? "TUNER_PRESET" :
                        // (data[1] & 0xF0) == 0xC0 ? "CD_OR_TAPE" :
                        // (data[1] & 0xF0) == 0xD0 ? "CD_TRACK" :
                        // "??",
                    // data[1] & 0x0F) == 0x00 ? "REPLY" :

                        // // When the following messages are ACK'ed, a report will follow, e.g.
                        // // 0x4D4 (AUDIO_SETTINGS_IDEN) or 0x554 (HEAD_UNIT_IDEN). When not ACK'ed, will retry a few
                        // // times.
                        // (data[1] & 0x0F) == 0x01 ? "AUDIO_SETTINGS_ANNOUNCE" :  // max 3 retries
                        // (data[1] & 0x0F) == 0x02 ? "BUTTON_PRESS_ANNOUNCE" :  // max 6 retries
                        // (data[1] & 0x0F) == 0x04 ? "STATUS_UPDATE_ANNOUNCE" :  // max 3 retries
                        // "??"
                // );

                // Button-press announcement?
                if ((data[1] & 0x0F) == 0x02)
                {
                    SERIAL.printf_P(
                        PSTR("    Head unit button pressed: %S%S\n"),

                        (data[2] & 0x1F) == 0x01 ? PSTR("'1'") :
                        (data[2] & 0x1F) == 0x02 ? PSTR("'2'") :
                        (data[2] & 0x1F) == 0x03 ? PSTR("'3'") :
                        (data[2] & 0x1F) == 0x04 ? PSTR("'4'") :
                        (data[2] & 0x1F) == 0x05 ? PSTR("'5'") :
                        (data[2] & 0x1F) == 0x06 ? PSTR("'6'") :
                        (data[2] & 0x1F) == 0x11 ? PSTR("AUDIO_DOWN") :
                        (data[2] & 0x1F) == 0x12 ? PSTR("AUDIO_UP") :
                        (data[2] & 0x1F) == 0x13 ? PSTR("SEEK_BACKWARD") :
                        (data[2] & 0x1F) == 0x14 ? PSTR("SEEK_FORWARD") :
                        (data[2] & 0x1F) == 0x16 ? PSTR("AUDIO") :
                        (data[2] & 0x1F) == 0x17 ? PSTR("MAN") :
                        (data[2] & 0x1F) == 0x1B ? PSTR("TUNER") :
                        (data[2] & 0x1F) == 0x1C ? PSTR("TAPE") :
                        (data[2] & 0x1F) == 0x1D ? PSTR("INTERNAL_CD") :
                        (data[2] & 0x1F) == 0x1E ? PSTR("CD_CHANGER") :
                        ToHexStr(data[2]),

                        (data[2] & 0xC0) == 0xC0 ? PSTR(" (held)") :
                        data[2] & 0x40 ? PSTR(" (released)") :
                        data[2] & 0x80 ? PSTR(" (repeat)") :
                        emptyStr
                    );
                } // if
            }
            else if (data[0] == 0x96)
            {
                // Examples:
                // Raw: #7819 ( 9/15)  6 0E 8C4 WA0 96-D8-48 ACK OK D848 CRC_OK

                if (dataLen != 1)
                {
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                SERIAL.print(F("CD-changer: STATUS_UPDATE_ANNOUNCE\n"));
            }
            else if (data[0] == 0x07)
            {
                if (dataLen != 3)
                {
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                // TODO - Unknown what this is. Surely not the head unit. Could be:
                // - MFD unit - this type of packet is showing often when the MFD is changing to another screen
                // - Aircon panel
                // - CD changer (seems unlikely)
                // - SatNav system - this type of packet is showing often when using the SatNav
                // - Instrument panel

                // Examples:
                // Raw: #0408 ( 3/15)  8 0E 8C4 WA0 07-40-00-E6-2C ACK OK E62C CRC_OK
                // Raw: #1857 (12/15)  8 0E 8C4 WA0 07-41-00-94-C0 ACK OK 94C0 CRC_OK
                // Raw: #2152 ( 7/15)  8 0E 8C4 WA0 07-10-00-47-E8 ACK OK 47E8 CRC_OK
                // Raw: #2155 (10/15)  8 0E 8C4 WA0 07-01-00-46-62 ACK OK 4662 CRC_OK
                // Raw: #3026 (11/15)  8 0E 8C4 WA0 07-20-00-D2-42 ACK OK D242 CRC_OK
                // Raw: #4199 (14/15)  8 0E 8C4 WA0 07-21-01-BF-94 ACK OK BF94 CRC_OK
                // Raw: #5039 (14/15)  8 0E 8C4 WA0 07-01-01-59-58 ACK OK 5958 CRC_OK
                // Raw: #5919 ( 9/15)  8 0E 8C4 WA0 07-00-01-2B-B4 ACK OK 2BB4 CRC_OK
                // Raw: #7644 ( 9/15)  8 0E 8C4 WA0 07-47-00-A5-92 ACK OK A592 CRC_OK
                // Raw: #7677 (12/15)  8 0E 8C4 WA0 07-60-00-00-E0 ACK OK 00E0 CRC_OK
                // Raw: #9471 ( 6/15)  8 0E 8C4 WA0 07-00-02-0A-FA ACK OK 0AFA CRC_OK
                // Raw: #7386 ( 1/15)  8 0E 8C4 WA0 07-40-02-D8-58 ACK OK D858 CRC_OK

                // data[1] seems a bit pattern. Bits seen:
                //  & 0x01 - MFD shows SatNav disclaimer screen, Entering new destination in SatNav
                //  & 0x02
                //  & 0x04
                //  & 0x10 - Entering new destination in SatNav
                //  & 0x20 - Entering new destination in SatNav, SatNav showing direction
                //  & 0x40 - Contact key in "ACC" position?, SatNav showing direction
                //
                // data[2] is usually 0x00, sometimes 0x01 or 0x02

                SERIAL.printf_P(PSTR("%s %s [to be decoded]\n"), ToHexStr(data[0]), ToHexStr(data[1], data[2]));
                return VAN_PACKET_PARSE_TO_BE_DECODED;
            }
            else if (data[0] == 0x52)
            {
                if (dataLen != 2)
                {
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                // TODO - Unknown what this is. Surely not the MFD and not the head unit.
                // Seems to be sent more often when engine is running. Could be:
                // - Aircon panel
                // - CD changer (seems unlikely)
                // - SatNav system
                // - Instrument panel

                // Examples:
                // Raw: #2641 ( 1/15)  7 0E 8C4 WA0 52-08-97-D0 ACK OK 97D0 CRC_OK
                // Raw: #7970 (10/15)  7 0E 8C4 WA0 52-20-A8-0E ACK OK A80E CRC_OK

                // data[1] is usually 0x08, sometimes 0x20
                //  & 0x08 - Contact key in "ON" position?
                //  & 0x20 - Economy mode ON?

                SERIAL.printf_P(PSTR("%s [to be decoded]\n"), ToHexStr(data[0], data[1]));
                return VAN_PACKET_PARSE_TO_BE_DECODED;
            }
            else
            {
                SERIAL.printf_P(PSTR("%s [to be decoded]\n"), ToHexStr(data[0]));
                return VAN_PACKET_PARSE_TO_BE_DECODED;
            } // if
        }
        break;

        case CAR_STATUS1_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#564
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#564

            // Print only if not duplicate of previous packet; ignore different sequence numbers
            static uint8_t packetData[VAN_MAX_DATA_BYTES] = "";  // Previous packet data
            if (memcmp(data + 1, packetData, dataLen - 2) == 0) return VAN_PACKET_DUPLICATE;
            memcpy(packetData, data + 1, dataLen - 2);

            SERIAL.print(F("--> Car status 1: "));

            if (dataLen != 27)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            char floatBuf[3][MAX_FLOAT_SIZE];
            SERIAL.printf_P(
                PSTR(
                    "seq=%u; doors=%S%S%S%S%S; right_stalk_button=%S; avg_speed_1=%u; avg_speed_2=%u; "
                    "exp_moving_avg_speed=%u;\n"
                    "    range_1=%u; avg_consumption_1=%s; range_2=%u; avg_consumption_2=%s; inst_consumption=%S; "
                    "mileage=%u\n"
                ),
                data[0] & 0x07,
                data[7] & 0x80 ? PSTR("FRONT_RIGHT ") : emptyStr,
                data[7] & 0x40 ? PSTR("FRONT_LEFT ") : emptyStr,
                data[7] & 0x20 ? PSTR("REAR_RIGHT ") : emptyStr,
                data[7] & 0x10 ? PSTR("REAR_LEFT ") : emptyStr,
                data[7] & 0x08 ? PSTR("BOOT ") : emptyStr,
                data[10] & 0x01 ? PSTR("PRESSED") : PSTR("RELEASED"),
                data[11],
                data[12],

                // When engine running but stopped (actual vehicle speed is 0), this value counts down by 1 every
                // 10 - 20 seconds or so. When driving, this goes up and down slowly toward the current speed.
                // Looking at the time stamps when this value changes, it looks like this is an exponential moving
                // average (EMA) of the recent vehicle speed. When the actual speed is 0, the value is seen to decrease
                // about 12% per minute. If the actual vehicle speed is sampled every second, then, in the
                // following formula, K would be around 12% / 60 = 0.2% = 0.002 :
                //
                //   exp_moving_avg_speed := exp_moving_avg_speed * (1 − K) + actual_vehicle_speed * K
                //
                // Often used in EMA is the constant N, where K = 2 / (N + 1). That means N would be around 1000 (given
                // a sampling time of 1 second).
                //
                data[13],

                (uint16_t)data[14] << 8 | data[15],
                FloatToStr(floatBuf[0], ((uint16_t)data[16] << 8 | data[17]) / 10.0, 1),
                (uint16_t)data[18] << 8 | data[19],
                FloatToStr(floatBuf[1], ((uint16_t)data[20] << 8 | data[21]) / 10.0, 1),
                (uint16_t)data[22] << 8 | data[23] == 0xFFFF
                    ? notApplicable3Str
                    : FloatToStr(floatBuf[2], ((uint16_t)data[22] << 8 | data[23]) / 10.0, 1),
                (uint16_t)data[24] << 8 | data[25]
            );
        }
        break;

        case CAR_STATUS2_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#524
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#524
            // https://github.com/morcibacsi/PSAVanCanBridge/blob/master/src/Van/Structs/VanDisplayStructsV1.h
            // https://github.com/morcibacsi/PSAVanCanBridge/blob/master/src/Van/Structs/VanDisplayStructsV2.h

            // Print only if not duplicate of previous packet
            static uint8_t packetData[VAN_MAX_DATA_BYTES] = "";  // Previous packet data
            if (memcmp(data, packetData, dataLen) == 0) return VAN_PACKET_DUPLICATE;
            memcpy(packetData, data, dataLen);

            SERIAL.print(F("--> Car status 2: "));

            if (dataLen != 14 && dataLen != 16)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            // PROGMEM array of PROGMEM strings, to save RAM bytes
            // See also: https://arduino-esp8266.readthedocs.io/en/latest/PROGMEM.html

            // TODO - translate into all languages

            // Byte 0
            static const char msg_0_0[] PROGMEM = "Tyre pressure low";
            static const char msg_0_1[] PROGMEM = "Door open";
            static const char msg_0_2[] PROGMEM = "Auto gearbox temperature too high";
            static const char msg_0_3[] PROGMEM = "Brake system fault"; // and exclamation mark on instrument cluster
            static const char msg_0_4[] PROGMEM = "Hydraulic suspension fault";
            static const char msg_0_5[] PROGMEM = "Suspension fault";
            static const char msg_0_6[] PROGMEM = "Engine oil temperature too high"; // and oil can on instrument cluster
            static const char msg_0_7[] PROGMEM = "Water temperature too high";

            // Byte 1
            static const char msg_1_0[] PROGMEM = "Unblock diesel filter"; // and mil icon on instrument cluster
            static const char msg_1_1[] PROGMEM = "Stop car icon";
            static const char msg_1_2[] PROGMEM = "Diesel additive too low";
            static const char msg_1_3[] PROGMEM = "Fuel cap open";
            static const char msg_1_4[] PROGMEM = "Tyres punctured";
            static const char msg_1_5[] PROGMEM = "Coolant level low"; // and icon on instrument cluster
            static const char msg_1_6[] PROGMEM = "Oil pressure too low";
            static const char msg_1_7[] PROGMEM = "Oil level too low";

            // Byte 2
            static const char msg_2_0[] PROGMEM = "Noooo.... the famous \"Antipollution fault\" :-(";
            static const char msg_2_1[] PROGMEM = "Brake pads worn";
            static const char msg_2_2[] PROGMEM = "Diagnosis ok";
            static const char msg_2_3[] PROGMEM = "Auto gearbox faulty";
            static const char msg_2_4[] PROGMEM = "ESP"; // and icon on instrument cluster
            static const char msg_2_5[] PROGMEM = "ABS";
            static const char msg_2_6[] PROGMEM = "Suspension or steering fault";
            static const char msg_2_7[] PROGMEM = "Braking system faulty";

            // Byte 3
            static const char msg_3_0[] PROGMEM = "Side airbag faulty";
            static const char msg_3_1[] PROGMEM = "Airbags faulty";
            static const char msg_3_2[] PROGMEM = "Cruise control faulty";
            static const char msg_3_3[] PROGMEM = "Engine temperature too high";
            static const char msg_3_4[] PROGMEM = "Fault: Load shedding in progress"; // RT3 mono
            static const char msg_3_5[] PROGMEM = "Ambient brightness sensor fault";
            static const char msg_3_6[] PROGMEM = "Rain sensor fault";
            static const char msg_3_7[] PROGMEM = "Water in diesel fuel filter"; // and icon on instrument cluster

            // Byte 4
            static const char msg_4_0[] PROGMEM = "Left rear sliding door faulty";
            static const char msg_4_1[] PROGMEM = "Headlight corrector fault";
            static const char msg_4_2[] PROGMEM = "Right rear sliding door faulty";
            static const char msg_4_3[] PROGMEM = "No broken lamp"; // RT3 mono
            static const char msg_4_4[] PROGMEM = "Battery low";
            static const char msg_4_5[] PROGMEM = "Battery charge fault"; // and battery icon on instrument cluster
            static const char msg_4_6[] PROGMEM = "Diesel particle filter faulty";
            static const char msg_4_7[] PROGMEM = "Catalytic converter fault"; // MIL icon flashing on instrument cluster

            // Byte 5
            static const char msg_5_0[] PROGMEM = "Handbrake on";
            static const char msg_5_1[] PROGMEM = "Seatbelt warning";
            static const char msg_5_2[] PROGMEM = "Passenger airbag deactivated"; // and icon on instrument cluster
            static const char msg_5_3[] PROGMEM = "Screen washer liquid level too low";
            static const char msg_5_4[] PROGMEM = "Current speed too high";
            static const char msg_5_5[] PROGMEM = "Ignition key left in";
            static const char msg_5_6[] PROGMEM = "Sidelights left on";
            static const char msg_5_7[] PROGMEM = "Hill holder active"; // On RT3 mono: Driver's seatbelt not fastened

            // Byte 6
            static const char msg_6_0[] PROGMEM = "Shock sensor faulty";
            static const char msg_6_1[] PROGMEM = "Seatbelt warning"; // not sure; there is no message with 0x31 so could not check
            static const char msg_6_2[] PROGMEM = "Check and re-init tyre pressure";
            static const char msg_6_3[] PROGMEM = "Remote control battery low";
            static const char msg_6_4[] PROGMEM = "Left stick button pressed";
            static const char msg_6_5[] PROGMEM = "Put automatic gearbox in P position";
            static const char msg_6_6[] PROGMEM = "Stop lights test: brake gently";
            static const char msg_6_7[] PROGMEM = "Fuel level low";

            // Byte 7
            static const char msg_7_0[] PROGMEM = "Automatic headlamp lighting deactivated";
            static const char msg_7_1[] PROGMEM = "Rear LH passenger seatbelt not fastened";
            static const char msg_7_2[] PROGMEM = "Rear RH passenger seatbelt not fastened";
            static const char msg_7_3[] PROGMEM = "Front passenger seatbelt not fastened";
            static const char msg_7_4[] PROGMEM = "Driving school pedals indication";
            static const char msg_7_5[] PROGMEM = "Tyre pressure monitor sensors X missing";
            static const char msg_7_6[] PROGMEM = "Tyre pressure monitor sensors Y missing";
            static const char msg_7_7[] PROGMEM = "Tyre pressure monitor sensors Z missing";

            // Byte 8
            static const char msg_8_0[] PROGMEM = "Doors locked";
            static const char msg_8_1[] PROGMEM = "ESP/ASR deactivated";
            static const char msg_8_2[] PROGMEM = "Child safety activated";
            static const char msg_8_3[] PROGMEM = "Deadlocking active";
            static const char msg_8_4[] PROGMEM = "Automatic lighting active";
            static const char msg_8_5[] PROGMEM = "Automatic wiping active";
            static const char msg_8_6[] PROGMEM = "Engine immobiliser fault";
            static const char msg_8_7[] PROGMEM = "Sport suspension mode active";

            // Byte 9 is the index of the current message

            // Byte 10
            static const char msg_10_0[] PROGMEM = "Unknown";
            static const char msg_10_1[] PROGMEM = "Unknown";
            static const char msg_10_2[] PROGMEM = "Unknown";
            static const char msg_10_3[] PROGMEM = "Change of fuel used in progress";
            static const char msg_10_4[] PROGMEM = "LPG fuel refused";
            static const char msg_10_5[] PROGMEM = "LPG system faulty";
            static const char msg_10_6[] PROGMEM = "LPG in use";
            static const char msg_10_7[] PROGMEM = "Min level LPG";

            // Byte 11
            static const char msg_11_0[] PROGMEM = "ADIN fault";
            static const char msg_11_1[] PROGMEM = "User stop & start";
            static const char msg_11_2[] PROGMEM = "Stop & start available";
            static const char msg_11_3[] PROGMEM = "Stop & start activated";
            static const char msg_11_4[] PROGMEM = "Stop & start deactivated";
            static const char msg_11_5[] PROGMEM = "Stop & start deferred";
            static const char msg_11_6[] PROGMEM = "XSARA DYNALTO";
            static const char msg_11_7[] PROGMEM = "307 DYNALTO";

            // Byte 12
            static const char msg_12_0[] PROGMEM = "Unknown";
            static const char msg_12_1[] PROGMEM = "Unknown";
            static const char msg_12_2[] PROGMEM = "Unknown";
            static const char msg_12_3[] PROGMEM = "Unknown";
            static const char msg_12_4[] PROGMEM = "Unknown";
            static const char msg_12_5[] PROGMEM = "Unknown";
            static const char msg_12_6[] PROGMEM = "Unknown";
            static const char msg_12_7[] PROGMEM = "Change to neutral";

            // Byte 13
            static const char msg_13_0[] PROGMEM = "Unknown";
            static const char msg_13_1[] PROGMEM = "Unknown";
            static const char msg_13_2[] PROGMEM = "Unknown";
            static const char msg_13_3[] PROGMEM = "Unknown";
            static const char msg_13_4[] PROGMEM = "Unknown";
            static const char msg_13_5[] PROGMEM = "Unknown";
            static const char msg_13_6[] PROGMEM = "Unknown";
            static const char msg_13_7[] PROGMEM = "Unknown";

            // On vehicles made after 2004

            // Byte 14
            static const char msg_14_0[] PROGMEM = "Roof operation complete";
            static const char msg_14_1[] PROGMEM = "Operation impossible screen not in place";
            static const char msg_14_2[] PROGMEM = "Roof mechanism not locked!";
            static const char msg_14_3[] PROGMEM = "Operation impossible boot open";
            static const char msg_14_4[] PROGMEM = "Operation impossible speed too high";
            static const char msg_14_5[] PROGMEM = "Operation impossible ext temp too low";
            static const char msg_14_6[] PROGMEM = "Roof mechanism faulty";
            static const char msg_14_7[] PROGMEM = "Boot mechanism not locked!";

            // Byte 15
            static const char msg_15_0[] PROGMEM = "Unknown";
            static const char msg_15_1[] PROGMEM = "Unknown";
            static const char msg_15_2[] PROGMEM = "Unknown";
            static const char msg_15_3[] PROGMEM = "Unknown";
            static const char msg_15_4[] PROGMEM = "Bow fault";
            static const char msg_15_5[] PROGMEM = "Operation impossible roof not unlocked";
            static const char msg_15_6[] PROGMEM = "Unknown";
            static const char msg_15_7[] PROGMEM = "Roof operation incomplete";

            static const char *const msgTable[] PROGMEM =
            {
                msg_0_0, msg_0_1, msg_0_2, msg_0_3, msg_0_4, msg_0_5, msg_0_6, msg_0_7,
                msg_1_0, msg_1_1, msg_1_2, msg_1_3, msg_1_4, msg_1_5, msg_1_6, msg_1_7,
                msg_2_0, msg_2_1, msg_2_2, msg_2_3, msg_2_4, msg_2_5, msg_2_6, msg_2_7,
                msg_3_0, msg_3_1, msg_3_2, msg_3_3, msg_3_4, msg_3_5, msg_3_6, msg_3_7,
                msg_4_0, msg_4_1, msg_4_2, msg_4_3, msg_4_4, msg_4_5, msg_4_6, msg_4_7,
                msg_5_0, msg_5_1, msg_5_2, msg_5_3, msg_5_4, msg_5_5, msg_5_6, msg_5_7,
                msg_6_0, msg_6_1, msg_6_2, msg_6_3, msg_6_4, msg_6_5, msg_6_6, msg_6_7,
                msg_7_0, msg_7_1, msg_7_2, msg_7_3, msg_7_4, msg_7_5, msg_7_6, msg_7_7,
                msg_8_0, msg_8_1, msg_8_2, msg_8_3, msg_8_4, msg_8_5, msg_8_6, msg_8_7,
                0, 0, 0, 0, 0, 0, 0, 0,
                msg_10_0, msg_10_1, msg_10_2, msg_10_3, msg_10_4, msg_10_5, msg_10_6, msg_10_7,
                msg_11_0, msg_11_1, msg_11_2, msg_11_3, msg_11_4, msg_11_5, msg_11_6, msg_11_7,
                msg_12_0, msg_12_1, msg_12_2, msg_12_3, msg_12_4, msg_12_5, msg_12_6, msg_12_7,
                msg_13_0, msg_13_1, msg_13_2, msg_13_3, msg_13_4, msg_13_5, msg_13_6, msg_13_7,
                msg_14_0, msg_14_1, msg_14_2, msg_14_3, msg_14_4, msg_14_5, msg_14_6, msg_14_7,
                msg_15_0, msg_15_1, msg_15_2, msg_15_3, msg_15_4, msg_15_5, msg_15_6, msg_15_7
            };

            SERIAL.print(F("Message bits present:\n"));
            for (int byte = 0; byte < 16; byte++)
            {
                if (byte == 9) byte++;
                for (int bit = 0; bit < 8; bit++)
                {
                    if (data[byte] >> bit & 0x01)
                    {
                        char alarmText[80];  // Make sure this is large enough for the largest string it must hold
                        strncpy_P(alarmText, (char *)pgm_read_dword(&(msgTable[byte * 8 + bit])), sizeof(alarmText) - 1);
                        alarmText[sizeof(alarmText) - 1] = 0;
                        SERIAL.print("    - ");
                        SERIAL.println(alarmText);
                    } // if
                } // for
            } // for

            uint8_t currentMsg = data[9];
            SERIAL.printf_P(PSTR("Message displayed on MFD: \"%S\"\n"), msgTable[currentMsg]);
        }
        break;

        case DASHBOARD_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#824
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#824

            // Print only if not duplicate of previous packet
            static uint8_t packetData[VAN_MAX_DATA_BYTES] = "";  // Previous packet data
            if (memcmp(data, packetData, dataLen) == 0) return VAN_PACKET_DUPLICATE;
            memcpy(packetData, data, dataLen);

            SERIAL.print(F("--> Dashboard: "));

            if (dataLen != 7)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            char floatBuf[2][MAX_FLOAT_SIZE];
            SERIAL.printf_P(
                PSTR("rpm=%S /min; speed=%S km/h; seq=%lu\n"),
                data[0] == 0xFF && data[1] == 0xFF ?
                    PSTR("---.-") :
                    FloatToStr(floatBuf[0], ((uint16_t)data[0] << 8 | data[1]) / 8.0, 1),
                data[2] == 0xFF && data[3] == 0xFF ?
                    PSTR("---.--") :
                    FloatToStr(floatBuf[1], ((uint16_t)data[2] << 8 | data[3]) / 100.0, 2),
                (uint32_t)data[4] << 16 | (uint32_t)data[5] << 8 | data[6]
            );
        }
        break;

        case DASHBOARD_BUTTONS_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#664
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#664

            // Print only if not duplicate of previous packet
            static uint8_t packetData[VAN_MAX_DATA_BYTES] = "";  // Previous packet data
            if (memcmp(data, packetData, dataLen) == 0) return VAN_PACKET_DUPLICATE;
            memcpy(packetData, data, dataLen);

            SERIAL.print(F("--> Dashboard buttons: "));

            if (dataLen != 11 && dataLen != 12)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            // data[6..10] - always 00-FF-00-FF-00

            char floatBuf[2][MAX_FLOAT_SIZE];
            SERIAL.printf_P(
                PSTR(
                    "hazard_lights=%S; door=%S; dashboard_programmed_brightness=%u, esp=%S,\n"
                    "    fuel_level_filtered=%s litre, fuel_level_raw=%s litre\n"
                ),
                data[0] & 0x02 ? onStr : offStr,
                data[2] & 0x40 ? PSTR("LOCKED") : PSTR("UNLOCKED"),
                data[2] & 0x0F,
                data[3] & 0x02 ? onStr : offStr,

                // Surely fuel level. Test with tank full shows definitely level is in litres.
                FloatToStr(floatBuf[0], data[4] / 2.0, 1),
                FloatToStr(floatBuf[1], data[5] / 2.0, 1)
            );
        }
        break;

        case HEAD_UNIT_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#554

            // These packets are sent by the head unit

            uint8_t seq = data[0];
            uint8_t infoType = data[1];

            // Head Unit info types
            enum HeatUnitInfoType_t
            {
                INFO_TYPE_TUNER = 0xD1,
                INFO_TYPE_TAPE,
                INFO_TYPE_PRESET,
                INFO_TYPE_CDCHANGER = 0xD5, // TODO - Not sure
                INFO_TYPE_CD,
            }; // enum HeatUnitInfoType_t

            switch (infoType)
            {
                case INFO_TYPE_TUNER:
                {
                    // Message when the HeadUnit is in "tuner" (radio) mode

                    // http://pinterpeti.hu/psavanbus/PSA-VAN.html#554_1

                    // Examples:
                    // 0E554E 80D1019206030F60FFFFA10000000000000000000080 9368
                    // 0E554E 82D1011242040F60FFFFA10000000000000000000082 3680
                    // 0E554E 87D10110CA030F60FFFFA10000000000000000000080 62E6

                    // Print only if not duplicate of previous packet
                    static uint8_t packetData[VAN_MAX_DATA_BYTES] = "";  // Previous packet data
                    if (memcmp(data, packetData, dataLen) == 0) return VAN_PACKET_DUPLICATE;
                    memcpy(packetData, data, dataLen);

                    SERIAL.print(F("--> Tuner info: "));

                    // TODO - some web pages show 22 bytes data, some 23
                    if (dataLen != 22)
                    {
                        SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                        return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                    } // if

                    // data[2]: radio band and preset position
                    uint8_t band = data[2] & 0x07;
                    uint8_t presetPos = data[2] >> 3 & 0x0F;
                    char presetPosBuffer[12];
                    sprintf_P(presetPosBuffer, PSTR(", memory=%u"), presetPos);

                    // data[3]: search bits
                    bool dxSensitivity = data[3] & 0x02;  // Tuner sensitivity: distant (Dx) or local (Lo)
                    bool ptyStandbyMode = data[3] & 0x04;

                    uint8_t searchMode = data[3] >> 3 & 0x07;
                    bool searchDirectionUp = data[3] & 0x80;
                    bool anySearchBusy = (searchMode != TS_NOT_SEARCHING);

                    // data[4] and data[5]: frequency being scanned or tuned in to
                    uint16_t frequency = (uint16_t)data[5] << 8 | data[4];

                    // data[6] - Reception status? Like: receiving station? Stereo? RDS bits like MS, TP, TA, AF?
                    // & 0xF0:
                    //   - Usually 0x00 when tuned in to a "normal" station.
                    //   - One or more bits stay '1' when tuned in to a "crappy" or weak station (e.g. pirate).
                    //   - During the process of tuning in to another station, switches to e.g. 0x20, 0x60, but
                    //     (usually) ends up 0x00.
                    //   Just guessing for the possible meaning of the bits:
                    //   - Mono (not stereo) bit
                    //   - Music/Speech (MS) bit
                    //   - No AF (Alternative Frequencies) available
                    //   - Number (0..15) indicating the quality of the RDS stream
                    //   & 0x10:
                    //   & 0x20:
                    //   & 0x40:
                    //   & 0x80:
                    //
                    // & 0x0F = signal strength: increases with antenna plugged in and decreases with antenna plugged
                    //          out. Updated when a station is being tuned in to, or when the MAN button is pressed.
                    uint8_t signalStrength = data[6] & 0x0F;
                    char signalStrengthBuffer[3];
                    sprintf_P(signalStrengthBuffer, PSTR("%u"), signalStrength);

                    char floatBuf[MAX_FLOAT_SIZE];
                    SERIAL.printf_P(
                        PSTR(
                            "band=%S%S, %S %S, signal_strength=%S,\n"
                            "    search_mode=%S%S%S,\n"
                        ),
                        TunerBandStr(band),
                        presetPos == 0 ? emptyStr : presetPosBuffer,
                        frequency == 0x07FF ? notApplicable3Str :
                            band == TB_AM
                                ? FloatToStr(floatBuf, frequency, 0)  // AM and LW bands
                                : FloatToStr(floatBuf, frequency / 20.0 + 50.0, 2),  // FM bands
                        band == TB_AM ? PSTR("KHz") : PSTR("MHz"),

                        // Also applicable in AM mode
                        signalStrength == 15 && (searchMode == TS_BY_FREQUENCY || searchMode == TS_BY_MATCHING_PTY)
                            ? notApplicable2Str
                            : signalStrengthBuffer,

                        TunerSearchModeStr(searchMode),

                        // Search sensitivity: distant (Dx) or local (Lo)
                        // TODO - not sure if this bit is applicable for the various values of 'searchMode'
                        // ! anySearchBusy ? emptyStr : dxSensitivity ? PSTR(", sensitivity=Dx") : PSTR(", sensitivity=Lo"),
                        dxSensitivity ? PSTR(", sensitivity=Dx") : PSTR(", sensitivity=Lo"),

                        ! anySearchBusy ? emptyStr : 
                            searchDirectionUp ? PSTR(", search_direction=UP") : PSTR(", search_direction=DOWN")
                    );

                    if (band != TB_AM)
                    {
                        // FM bands only

                        // data[7]: TA, RDS and REG (regional) bits
                        bool rdsSelected = data[7] & 0x01;
                        bool taSelected = data[7] & 0x02;
                        bool regional = data[7] & 0x04;  // Long-press "RDS" button
                        bool rdsAvailable = data[7] & 0x20;
                        bool taAvailable = data[7] & 0x40;
                        bool taAnnounce = data[7] & 0x80;

                        // data[8] and data[9]: PI code
                        // See also:
                        // - https://en.wikipedia.org/wiki/Radio_Data_System#Program_Identification_Code_(PI_Code)
                        // - https://radio-tv-nederland.nl/rds/rds.html
                        // - https://people.uta.fi/~jk54415/dx/pi-codes.html
                        // - http://poupa.cz/rds/countrycodes.htm
                        // - https://www.pira.cz/rds/p232man.pdf
                        uint16_t piCode = (uint16_t)data[8] << 8 | data[9];
                        uint8_t countryCode = piCode >> 12 & 0x0F;
                        uint8_t coverageCode = piCode >> 8 & 0x0F;
                        char piBuffer[40];
                        sprintf_P(piBuffer, PSTR("%04X(%S, %S)"),
                            piCode,
                            RadioPiCountry(countryCode),
                            RadioPiAreaCoverage(coverageCode)
                        );

                        // data[10]: for PTY-based search mode
                        // & 0x1F: PTY code to search
                        // & 0x20: 0 = PTY of station matches selected; 1 = no match
                        // & 0x40: 1 = "Select PTY" dialog visible (long-press "TA" button; press "<<" or ">>" to change)
                        uint8_t selectedPty = data[10] & 0x1F;
                        bool ptyMatch = (data[10] & 0x20) == 0;  // PTY of station matches selected PTY
                        bool ptySelectionMenu = data[10] & 0x40; 
                        char selectedPtyBuffer[40];
                        sprintf_P(selectedPtyBuffer, PSTR("%u(%S)"), selectedPty, PtyStr(selectedPty));

                        // data[11]: PTY code of current station
                        uint8_t currPty = data[11] & 0x1F;
                        char currPtyBuffer[40];
                        sprintf_P(currPtyBuffer, PSTR("%u(%S)"), currPty, PtyStr(currPty));

                        // data[12]...data[20]: RDS text
                        char rdsTxt[9];
                        strncpy(rdsTxt, (const char*) data + 12, 8);
                        rdsTxt[8] = 0;

                        SERIAL.printf_P(
                            PSTR(
                                "    pty_selection_menu=%S, selected_pty=%s, pty_standby_mode=%S, pty_match=%S, pty=%S,\n"
                                "    pi=%S, regional=%S, ta=%S %S, rds=%S %S, rds_text=\"%s\"%S\n"
                            ),
                            ptySelectionMenu ? onStr : offStr,
                            selectedPtyBuffer,
                            ptyStandbyMode ? yesStr : noStr,
                            ptyMatch ? yesStr : noStr,
                            currPty == 0x00 ? notApplicable3Str : currPtyBuffer,

                            piCode == 0xFFFF ? notApplicable3Str : piBuffer,
                            regional ? onStr : offStr,
                            taSelected ? onStr : offStr,
                            taAvailable ? PSTR("(AVAILABLE)") : PSTR("(NOT_AVAILABLE)"),
                            rdsSelected ? onStr : offStr,
                            rdsAvailable ? PSTR("(AVAILABLE)") : PSTR("(NOT_AVAILABLE)"),
                            rdsTxt,

                            taAnnounce ? PSTR("\n    --> Info Trafic!") : emptyStr
                        );
                    } // if
                }
                break;
                
                case INFO_TYPE_TAPE:
                {
                    // http://pinterpeti.hu/psavanbus/PSA-VAN.html#554_2

                    SERIAL.print(F("--> Cassette tape info: "));

                    if (dataLen != 5)
                    {
                        SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                        return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                    } // if

                    uint8_t status = data[2] & 0x3C;

                    SERIAL.printf_P(PSTR("status=%S, side=%S\n"),
                        status == 0x00 ? PSTR("STOPPED") :
                        status == 0x04 ? PSTR("LOADING") :
                        status == 0x0C ? PSTR("PLAYING") :
                        status == 0x10 ? PSTR("FAST_FORWARD") :
                        status == 0x14 ? PSTR("NEXT_TRACK") :
                        status == 0x30 ? PSTR("REWIND") :
                        status == 0x34 ? PSTR("PREVIOUS_TRACK") :
                        ToHexStr(status),
                        data[2] & 0x01 ? PSTR("2") : PSTR("1")
                    );
                }
                break;

                case INFO_TYPE_PRESET:
                {
                    // http://pinterpeti.hu/psavanbus/PSA-VAN.html#554_3

                    SERIAL.print(F("--> Tuner preset info: "));

                    if (dataLen != 12)
                    {
                        SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                        return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                    } // if

                    uint8_t tunerBand = data[2] >> 4 & 0x07;
                    uint8_t tunerMemory = data[2] & 0x0F;

                    char rdsOrFreqTxt[9];
                    strncpy(rdsOrFreqTxt, (const char*) data + 3, 8);
                    rdsOrFreqTxt[8] = 0;

                    SERIAL.printf_P(PSTR("band=%S, memory=%u, \"%s%S\"\n"),
                        TunerBandStr(tunerBand),
                        tunerMemory,
                        rdsOrFreqTxt,
                        tunerBand == TB_AM ? PSTR(" KHz") : data[2] & 0x80 ? emptyStr : PSTR(" MHz")
                    );
                }
                break;

                case INFO_TYPE_CD:
                {
                    // http://pinterpeti.hu/psavanbus/PSA-VAN.html#554_6

                    SERIAL.print(F("--> CD track info: "));

                    // TODO - do we know the fixed numbers? Seems like this can only be 10 or 12.
                    if (dataLen < 10)
                    {
                        SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                        return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                    } // if

                    bool searching = data[3] & 0x10;

                    char trackTimeStr[7];
                    sprintf_P(trackTimeStr, PSTR("%X:%02X"), data[5], data[6]);

                    uint8_t totalTracks = data[8];
                    bool totalTracksInvalid = totalTracks == 0xFF;
                    char totalTracksStr[3];
                    if (! totalTracksInvalid) sprintf_P(totalTracksStr, PSTR("%X"), totalTracks);

                    char totalTimeStr[7];
                    bool totalTimeInvalid = dataLen < 12;
                    if (! totalTimeInvalid)
                    {
                        uint8_t totalTimeMin = data[9];
                        uint8_t totalTimeSec = data[10];
                        totalTimeInvalid = totalTimeMin == 0xFF || totalTimeSec == 0xFF;
                        if (! totalTimeInvalid) sprintf_P(totalTimeStr, PSTR("%X:%02X"), totalTimeMin, totalTimeSec);
                    } // if

                    SERIAL.printf_P(
                        PSTR(
                            "status=%S; cd_loading_changing_to_cd_source=%S; pause=%S; play=%S; fast_forward=%S; "
                            "rewind=%S;\n"
                            "    searching=%S; track_time=%S; current_track=%X; total_tracks=%S; total_time=%S\n"
                        ),

                        data[3] == 0x11 ? PSTR("LOADING_CD_AND_CHANGING_TO_CD_AUDIO_SOURCE") :
                        data[3] == 0x12 ? PSTR("PAUSE-SEARCHING") :
                        data[3] == 0x13 ? PSTR("PLAY-SEARCHING") :
                        data[3] == 0x02 ? PSTR("PAUSE") :
                        data[3] == 0x03 ? PSTR("PLAY") :
                        data[3] == 0x04 ? PSTR("FAST_FORWARD") :
                        data[3] == 0x05 ? PSTR("REWIND") :
                        ToHexStr(data[3]),

                        (data[3] & 0x0F) == 0x01 ? yesStr : noStr,
                        (data[3] & 0x0F) == 0x02 && ! searching ? yesStr : noStr,
                        (data[3] & 0x0F) == 0x03 && ! searching ? yesStr : noStr,
                        (data[3] & 0x0F) == 0x04 ? yesStr : noStr,
                        (data[3] & 0x0F) == 0x05 ? yesStr : noStr,

                        searching ? yesStr : noStr,

                        searching ? PSTR("--:--") : trackTimeStr,
                        data[7],
                        totalTracksInvalid ? notApplicable2Str : totalTracksStr,
                        totalTimeInvalid ? PSTR("--:--") : totalTimeStr
                    );
                }
                break;

                case INFO_TYPE_CDCHANGER:
                {
                    SERIAL.print(F("--> CD changer info: "));
                    SERIAL.println(FPSTR(toBeDecodedStr));
                    return VAN_PACKET_PARSE_TO_BE_DECODED;
                }
                break;

                default:
                {
                    SERIAL.printf_P(PSTR("--> Unknown head unit info type 0x%02X: "), infoType);
                    SERIAL.println(FPSTR(toBeDecodedStr));
                    return VAN_PACKET_PARSE_TO_BE_DECODED;
                }
                break;
            } // switch
        }
        break;

        case TIME_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#984
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#984

            // TODO - seems to have nothing to do with time. Mine is always the same:
            //   Raw: #2692 ( 7/15) 10 0E 984 W-0 00-00-00-06-08-D0-C8 NO_ACK OK D0C8 CRC_OK
            // regardless of the time.

            SERIAL.print(F("--> Time: "));

            if (dataLen != 5)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            SERIAL.printf_P(PSTR("uptime battery=%u days, time=%uh%um\n"),
                (uint16_t)data[0] << 8 | data[1],
                data[3],
                data[4]
            );

        }
        break;

        case AUDIO_SETTINGS_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#4D4
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#4D4
            // https://github.com/morcibacsi/PSAVanCanBridge/blob/master/src/Van/Structs/VanRadioInfoStructs.h

            // These packets are sent by the head unit

            // Examples:
            // 0E4D4E 800C010011003F3F3F3F80 0118
            // 0E4D4E 850D000010003F3F3F3F85 6678
            // 0E4D4E 840C010011823F3F3F3F84 AD50
            // 0E4D4E 863C010011013E3E404086 70C4
            // 0E 4D4 RA0 84-0C-01-02-11-11-3F-3F-3F-3F-84-1E-2E ACK OK 1E2E CRC_OK

            // Most of these packets are the same. So print only if not duplicate of previous packet.
            static uint8_t packetData[VAN_MAX_DATA_BYTES] = "";  // Previous packet data
            if (memcmp(data, packetData, dataLen) == 0) return VAN_PACKET_DUPLICATE;
            memcpy(packetData, data, dataLen);

            SERIAL.print(F("--> Audio settings: "));

            if (dataLen != 11)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            // data[0] & 0x07: sequence number
            // data[4] & 0x10: TODO

            SERIAL.printf_P(
                PSTR(
                    "power=%S, tape=%S, cd=%S, source=%S, ext_mute=%S, mute=%S,\n"
                    "    volume=%u%S, audio_menu=%S, bass=%d%S, treble=%d%S, loudness=%S, fader=%d%S, balance=%d%S, "
                    "auto_volume=%S\n"
                ),
                data[2] & 0x01 ? onStr : offStr,  // power
                data[4] & 0x20 ? presentStr : notPresentStr,  // tape
                data[4] & 0x40 ? presentStr : notPresentStr,  // cd

                (data[4] & 0x0F) == 0x00 ? noneStr :  // source
                (data[4] & 0x0F) == 0x01 ? PSTR("TUNER") :
                (data[4] & 0x0F) == 0x02 ?
                    data[4] & 0x20 ? PSTR("TAPE") : 
                    data[4] & 0x40 ? PSTR("INTERNAL_CD") : 
                    PSTR("INTERNAL_CD_OR_TAPE") :
                (data[4] & 0x0F) == 0x03 ? PSTR("CD_CHANGER") :

                // This is the "default" mode for the head unit, to sit there and listen to the navigation
                // audio. The navigation audio volume is also always set (usually a lot higher than the radio)
                // whenever this source is chosen.
                (data[4] & 0x0F) == 0x05 ? PSTR("NAVIGATION_AUDIO") :

                ToHexStr((uint8_t)(data[4] & 0x0F)),

                // ext_mute. Activated when head unit ISO connector A pin 1 ("Phone mute") is pulled LOW (to Ground).
                data[1] & 0x02 ? onStr : offStr,

                // mute. To activate: press both VOL_UP and VOL_DOWN buttons on stalk.
                data[1] & 0x01 ? onStr : offStr,

                data[5] & 0x7F,  // volume
                data[5] & 0x80 ? updatedStr : emptyStr,

                // audio_menu. Bug: if CD changer is playing, this one is always "OPEN" (even if it isn't).
                data[1] & 0x20 ? PSTR("OPEN") : PSTR("CLOSED"),

                (sint8_t)(data[8] & 0x7F) - 0x3F,  // bass
                data[8] & 0x80 ? updatedStr : emptyStr,
                (sint8_t)(data[9] & 0x7F) - 0x3F,  // treble
                data[9] & 0x80 ? updatedStr : emptyStr,
                data[1] & 0x10 ? onStr : offStr,  // loudness
                (sint8_t)(0x3F) - (data[7] & 0x7F),  // fader
                data[7] & 0x80 ? updatedStr : emptyStr,
                (sint8_t)(0x3F) - (data[6] & 0x7F),  // balance
                data[6] & 0x80 ? updatedStr : emptyStr,
                data[1] & 0x04 ? onStr : offStr  // auto_volume
            );
        }
        break;

        case MFD_STATUS_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#5E4
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#5E4

            // Example: 0E5E4C00FF1FF8

            // Most of these packets are the same. So print only if not duplicate of previous packet.
            static uint8_t packetData[VAN_MAX_DATA_BYTES] = "";  // Previous packet data
            if (memcmp(data, packetData, dataLen) == 0) return VAN_PACKET_DUPLICATE;
            memcpy(packetData, data, dataLen);

            SERIAL.print(F("--> MFD status: "));

            if (dataLen != 2)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            uint16_t mfdStatus = (uint16_t)data[0] << 8 | data[1];

            // TODO - seems like data[0] & 0x20 is some kind of status bit, showing MFD connectivity status? There
            // must also be a specific packet that triggers this bit to be set to '0', because this happens e.g. when
            // the contact key is removed.

            SERIAL.printf(
                "MFD_SCREEN_%S\n",

                // hmmm... MFD can also be ON if this is reported; this happens e.g. in the "minimal VAN network" test
                // setup with only the head unit (radio) and MFD. Maybe this is a status report: the MFD indicates that
                // it has received packets showing connectivity to e.g. the BSI?
                data[0] == 0x00 && data[1] == 0xFF ? offStr :

                data[0] == 0x20 && data[1] == 0xFF ? onStr :
                ToHexStr(mfdStatus)
            );
        }
        break;

        case AIRCON1_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#464
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#464
            // https://github.com/morcibacsi/PSAVanCanBridge/blob/master/src/Van/Structs/VanAirConditioner1Structs.h

            // Examples:
            // Raw: #3147 ( 7/15) 10 0E 464 WA0 00-00-00-00-07-D1-7C ACK OK D17C CRC_OK
            // Raw: #3510 (10/15) 10 0E 464 WA0 10-00-00-00-07-AE-8A ACK OK AE8A CRC_OK
            // Raw: #3542 (12/15) 10 0E 464 WA0 10-00-00-00-09-15-C6 ACK OK 15C6 CRC_OK
            // Raw: #1978 ( 3/15) 10 0E 464 WA0 11-00-00-00-15-81-DC ACK OK 81DC CRC_OK
            // Raw: #2780 (10/15) 10 0E 464 WA0 10-00-00-00-09-15-C6 ACK OK 15C6 CRC_OK
            // Raw: #3994 ( 9/15) 10 0E 464 WA0 10-00-00-00-08-0A-FC ACK OK 0AFC CRC_OK
            // Raw: #5630 (10/15) 10 0E 464 WA0 10-00-00-00-06-B1-B0 ACK OK B1B0 CRC_OK
            // Raw: #7808 (13/15) 10 0E 464 WA0 00-00-00-00-00-8C-DA ACK OK 8CDA CRC_OK

            // Most of these packets are the same. So print only if not duplicate of previous packet.
            static uint8_t packetData[VAN_MAX_DATA_BYTES] = "";  // Previous packet data
            if (memcmp(data, packetData, dataLen) == 0) return VAN_PACKET_DUPLICATE;
            memcpy(packetData, data, dataLen);

            SERIAL.print(F("--> Aircon 1: "));

            if (dataLen != 5)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            // data[0] - bits:
            // - & 0x01: Rear window heater ON
            // - & 0x04: Air recirculation ON
            // - & 0x10: A/C icon?
            // - & 0x20: A/C icon? "YES" : "NO",  // TODO - what is this? This bit is always 0
            // - & 0x40: mode ? "AUTO" : "MANUAL",  // TODO - what is this? This bit is always 0
            // - & 0x0A: demist ? "ON" : "OFF",  // TODO - what is this? These bits are always 0

            // data[4] - reported fan speed
            //
            // Real-world tests: reported fan_speed values with various settings of the fan speed icon (0 = fan icon
            // not visible at all ... 7 = all four blades visible), under varying conditions:
            //
            // 1.) Recirculation = OFF, rear window heater = OFF, A/C = OFF:
            //     Fan icon not visible at all = 0
            //     Fan icon with all empty blades = 4 
            //     One blade visible = 4 (same as previous!)
            //     Two blades - low = 6
            //     Two blades - high = 7
            //     Three blades - low = 9
            //     Three blades - high = 10
            //     Four blades = 19
            //     
            // 2.) Recirculation = ON, rear window heater = OFF, A/C = OFF:
            //     Fan icon not visible at all = 0
            //     Fan icon with all empty blades = 4
            //     One blade visible = 4 (same as previous!)
            //     Two blades - low = 6
            //     Two blades - high = 7
            //     Three blades - low = 9
            //     Three blades - high = 10
            //     Four blades = 19
            //
            // 3.) Recirculation = OFF, rear window heater = OFF, A/C = ON (compressor running):
            //     Fan icon not visible at all = 0 (A/C icon will turn off)
            //     Fan icon with all empty blades = 4
            //     One blade visible = 4 (same as previous!)
            //     Two blades - low = 8
            //     Two blades - high = 9
            //     Three blades - low = 11
            //     Three blades - high = 12
            //     Four blades = 21
            //
            // 4.) Recirculation = OFF, rear window heater = ON, A/C = OFF:
            //     Fan icon not visible at all = 12
            //     Fan icon with all empty blades = 15
            //     One blade visible = 16
            //     Two blades - low = 17
            //     Two blades - high = 19
            //     Three blades - low = 20
            //     Three blades - high = 22
            //     Four blades = 30
            //
            // 5.) Recirculation = OFF, rear window heater = ON, = A/C ON:
            //     Fan icon not visible at all = 12 (A/C icon will turn off)
            //     Fan icon with all empty blades = 17
            //     One blade visible = 18
            //     Two blades - low = 19
            //     Two blades - high = 21
            //     Three blades - low = 22
            //     Three blades - high = 24
            //     Four blades = 32
            //
            // All above with demist ON --> makes no difference
            //
            // In "AUTO" mode, the fan speed varies gradually over time in increments or decrements of 1.

            bool rear_heater = data[0] & 0x01;
            bool ac_icon = data[0] & 0x10;
            uint8_t setFanSpeed = data[4];
            if (rear_heater) setFanSpeed -= 12;
            if (ac_icon) setFanSpeed -= 2;
            setFanSpeed =
                setFanSpeed >= 18 ? 7 : // Four blades
                setFanSpeed >= 10 ? 6 : // Three blades - high
                setFanSpeed >= 8 ? 5 : // Three blades - low
                setFanSpeed == 7 ? 4 : // Two blades - high
                setFanSpeed >= 5 ? 3 : // Two blades - low
                setFanSpeed == 4 ? 2 : // One blade (2) or all empty blades (1)
                setFanSpeed == 3 ? 1 : // All empty blades (1)
                0; // Fan icon not visible at all

            SERIAL.printf_P(
                PSTR("ac_icon=%S; recirc=%S, rear_heater=%S, reported_fan_speed=%u, set_fan_speed=%u\n"),
                ac_icon ? onStr : offStr,
                data[0] & 0x04 ? onStr : offStr,
                rear_heater ? yesStr : noStr,
                data[4],
                setFanSpeed
            );
        }
        break;

        case AIRCON2_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#4DC
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#4DC
            // https://github.com/morcibacsi/PSAVanCanBridge/blob/master/src/Van/Structs/VanAirConditioner2Structs.h

            // Evaporator temperature is constantly toggling between 2 values, while the rest of the data is the same.
            // So print only if not duplicate of previous 2 packets.
            static uint8_t packetData[2][VAN_MAX_DATA_BYTES];  // Previous packet data

            if (memcmp(data, packetData[0], dataLen) == 0) return VAN_PACKET_DUPLICATE;
            if (memcmp(data, packetData[1], dataLen) == 0) return VAN_PACKET_DUPLICATE;

            memcpy(packetData[0], packetData[1], dataLen);
            memcpy(packetData[1], data, dataLen);

            SERIAL.print(F("--> Aircon 2: "));

            if (dataLen != 7)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            char floatBuf[2][MAX_FLOAT_SIZE];
            SERIAL.printf_P(
                PSTR(
                    "contact_key_on=%S; enabled=%S; rear_heater=%S; aircon_compressor=%S; contact_key_position=%S;\n"
                    "    condenser_temperature=%S, evaporator_temperature=%s\n"
                ),
                data[0] & 0x80 ? yesStr : noStr,
                data[0] & 0x40 ? yesStr : noStr,
                data[0] & 0x20 ? onStr : offStr,
                data[0] & 0x01 ? onStr : offStr,

                data[1] == 0x1C ? PSTR("ACC_OR_OFF") :
                data[1] == 0x18 ? PSTR("ACC-->OFF") :
                data[1] == 0x04 ? PSTR("ON-->ACC") :
                data[1] == 0x00 ? onStr :
                ToHexStr(data[1]),

                // This is not interior temperature. This rises quite rapidly if the aircon compressor is
                // running, and drops again when the aircon compressor is off. So I think this is the condenser
                // temperature.
                data[2] == 0xFF ? notApplicable2Str : FloatToStr(floatBuf[0], data[2], 0),

                FloatToStr(floatBuf[1], ((uint16_t)data[3] << 8 | data[4]) / 10.0 - 40.0, 1)
            );
        }
        break;

        case CDCHANGER_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#4EC
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#4EC
            // https://github.com/morcibacsi/PSAVanCanBridge/blob/master/src/Van/Structs/VanCdChangerStructs.h

            // Example: 0E4ECF9768

            if (dataLen != 0)
            {
                // Most of these packets are the same. So print only if not duplicate of previous packet.
                static uint8_t packetData[VAN_MAX_DATA_BYTES] = "";  // Previous packet data
                if (memcmp(data, packetData, dataLen) == 0) return VAN_PACKET_DUPLICATE;
                memcpy(packetData, data, dataLen);
            } // if

            static const char PROGMEM intro[] = "--> CD Changer: ";

            if (dataLen != 0 && dataLen != 12)
            {
                SERIAL.printf_P(PSTR("%S[unexpected packet length]\n"), intro);
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            if (dataLen == 0)
            {
                // This is simply a "read" frame that did not get an in-frame response
                // If the CD changer is not there, this request is sent by the MFD time and time again. Print only
                // the first occurrence.
                static bool seen = false;
                if (seen) return VAN_PACKET_DUPLICATE;
                seen = true;
                SERIAL.printf_P(PSTR("%Srequest\n"), intro);
                break;
            } // if

            SERIAL.printf_P(intro);

            bool searching = data[2] & 0x10;

            SERIAL.printf_P(
                PSTR(
                    "random=%S; operational=%S; present=%S; searching=%S; loading=%S; state=%S;\n"
                    "    pause=%S; play=%S; fast_forward=%S; rewind=%S;\n"
                ),

                data[1] == 0x01 ? onStr : offStr,

                // Head unit powered on; CD changer operational (either standby or selected as audio source)
                data[2] & 0x80 ? yesStr : noStr,

                data[2] & 0x40 ? yesStr : noStr,  // CD changer device present
                searching ? yesStr : noStr,
                data[2] & 0x08 ? yesStr : noStr,  // Loading disc? Not sure

                // TODO - remove this field?
                data[2] == 0x40 ? PSTR("POWER_OFF") :  // Not sure
                data[2] == 0x41 ? PSTR("POWER_ON") : // Not sure
                data[2] == 0x49 ? PSTR("INITIALIZE") :  // Not sure
                data[2] == 0xC0 ? PSTR("POWER_ON_READY") :  // Not sure
                data[2] == 0xC1 ? PSTR("PAUSE") :
                data[2] == 0xC3 ? PSTR("PLAY") :
                data[2] == 0xC4 ? PSTR("FAST_FORWARD") :
                data[2] == 0xC5 ? PSTR("REWIND") :
                data[2] == 0xD3 ? PSTR("PLAY-SEARCHING") :
                ToHexStr(data[2]),

                (data[2] & 0x07) == 0x01 ? yesStr : noStr,
                (data[2] & 0x07) == 0x03 && ! searching ? yesStr : noStr,
                (data[2] & 0x07) == 0x04 ? yesStr : noStr,
                (data[2] & 0x07) == 0x05 ? yesStr : noStr
            );

            uint8_t trackTimeMin = data[4];
            uint8_t trackTimeSec = data[5];
            bool trackTimeInvalid = trackTimeMin == 0xFF || trackTimeSec == 0xFF;
            char trackTimeStr[7];
            if (! trackTimeInvalid) sprintf_P(trackTimeStr, PSTR("%X:%02X"), trackTimeMin, trackTimeSec);

            uint8_t totalTracks = data[8];
            bool totalTracksInvalid = totalTracks == 0xFF;
            char totalTracksStr[3];
            if (! totalTracksInvalid) sprintf_P(totalTracksStr, PSTR("%X"), totalTracks);

            SERIAL.printf_P(
                PSTR(
                    "    cartridge=%S; %S in track %X/%S on disc %X; presence=%S-%S-%S-%S-%S-%S\n"
                ),
                data[3] == 0x16 ? PSTR("IN") :
                data[3] == 0x06 ? PSTR("OUT") :
                ToHexStr(data[3]),

                trackTimeInvalid ? PSTR("--:--") : trackTimeStr,
                data[6],
                totalTracksInvalid ? notApplicable2Str : totalTracksStr,
                data[7],

                data[10] & 0x01 ? PSTR("1") : PSTR(" "),
                data[10] & 0x02 ? PSTR("2") : PSTR(" "),
                data[10] & 0x04 ? PSTR("3") : PSTR(" "),
                data[10] & 0x08 ? PSTR("4") : PSTR(" "),
                data[10] & 0x10 ? PSTR("5") : PSTR(" "),
                data[10] & 0x20 ? PSTR("6") : PSTR(" ")
            );
        }
        break;

        case SATNAV_STATUS_1_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#54E
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#54E

            // Examples:

            // Raw: #0974 (14/15) 11 0E 54E RA0 80-00-80-00-00-80-95-06 ACK OK 9506 CRC_OK
            // Raw: #1058 ( 8/15) 11 0E 54E RA0 81-02-00-00-00-81-B2-6C ACK OK B26C CRC_OK

            SERIAL.print(F("--> SatNav status 1: "));

            if (dataLen != 6)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            // Possible meanings of data
            //
            // data[0] & 0xF0, data[dataLen - 1] & 0xF0: always 0x80
            // data[0] & 0x07, data[dataLen - 1] & 0x07: sequence number
            //
            // data[1...2]: status ?? See below
            //
            // data[3]: usually 0x00, sometimes 0x02
            //
            // data[4]: usually 0x00, sometimes 0x0B, 0x0C, 0x0E, 0x7A
            //
            uint16_t status = (uint16_t)data[1] << 8 | data[2];

            SERIAL.printf_P(
                PSTR("status=%S%S\n"),

                // TODO - check; total guess
                status == 0x0000 ? PSTR("NOT_OPERATING") :
                status == 0x0001 ? ToHexStr(status) :  // Seen this but what is it??
                status == 0x0020 ? ToHexStr(status) :  // Seen this but what is it?? Nearly at destination ??
                status == 0x0080 ? PSTR("READY") :
                status == 0x0101 ? ToHexStr(status) :  // Seen this but what is it??
                status == 0x0200 ? PSTR("READING_DISC_1") :
                status == 0x0220 ? ToHexStr(status) :  // Seen this but what is it??
                status == 0x0300 ? PSTR("IN_GUIDANCE_MODE_1") :
                status == 0x0301 ? PSTR("IN_GUIDANCE_MODE_2") :
                status == 0x0320 ? PSTR("STOPPING_GUIDANCE") :
                status == 0x0400 ? PSTR("START_OF_AUDIO_MESSAGE") :
                status == 0x0410 ? PSTR("ARRIVED_AT_DESTINATION_1") :
                status == 0x0600 ? ToHexStr(status) :  // Seen this but what is it??
                status == 0x0700 ? PSTR("INSTRUCTION_AUDIO_MESSAGE_START_1") :
                status == 0x0701 ? PSTR("INSTRUCTION_AUDIO_MESSAGE_START_2") :
                status == 0x0800 ? PSTR("END_OF_AUDIO_MESSAGE") :  // Follows 0x0400, 0x0700, 0x0701
                status == 0x4000 ? PSTR("GUIDANCE_STOPPED") :
                status == 0x4001 ? ToHexStr(status) :  // Seen this but what is it??
                status == 0x4200 ? PSTR("ARRIVED_AT_DESTINATION_2") :
                status == 0x9000 ? PSTR("READING_DISC_2") :
                status == 0x9080 ? ToHexStr(status) :  // Seen this but what is it??
                ToHexStr(data[1], data[2]),

                data[4] == 0x0B ? PSTR(" reason=0x0B") :  // Seen with status == 0x4001
                data[4] == 0x0C ? PSTR(" reason=NO_DISC") :
                data[4] == 0x0E ? PSTR(" reason=NO_DISC") :
                emptyStr
            );

            return VAN_PACKET_PARSE_TO_BE_DECODED;
        }
        break;

        case SATNAV_STATUS_2_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#7CE
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#7CE

            // Examples:

            // 0E7CEF2114

            // Raw: #0517 ( 7/15) 25 0E 7CE RA0 80-20-38-00-07-06-01-00-00-00-00-00-00-00-00-00-00-00-00-80-C4-18 ACK OK C418 CRC_OK
            // Raw: #0973 (13/15) 25 0E 7CE RA0 81-20-38-00-07-06-01-00-00-00-00-00-00-00-00-00-00-20-00-81-3D-68 ACK OK 3D68 CRC_OK
            // Raw: #1057 ( 7/15) 25 0E 7CE RA0 82-20-3C-00-07-06-01-00-00-00-00-00-00-00-00-00-00-28-00-82-1E-A0 ACK OK 1EA0 CRC_OK
            // Raw: #1635 ( 0/15) 25 0E 7CE RA0 87-21-3C-00-07-06-01-00-00-00-00-00-00-00-00-00-00-28-00-87-1A-78 ACK OK 1A78 CRC_OK

            if (dataLen != 0)
            {
                // Most of these packets are the same. So print only if not duplicate of previous packet.
                static uint8_t packetData[VAN_MAX_DATA_BYTES] = "";  // Previous packet data
                if (memcmp(data, packetData, dataLen) == 0) return VAN_PACKET_DUPLICATE;
                memcpy(packetData, data, dataLen);
            } // if

            static const char PROGMEM intro[] = "--> SatNav status 2: ";

            if (dataLen != 0 && dataLen != 20)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            if (dataLen == 0)
            {
                // This is simply a "read" frame that did not get an in-frame response
                // If the SatNav is not there, this request is sent by the MFD time and time again. Print only
                // the first occurrence.
                static bool seen = false;
                if (seen) return VAN_PACKET_DUPLICATE;
                seen = true;
                SERIAL.printf_P(PSTR("%Srequest\n"), intro);
                break;
            } // if

            SERIAL.printf_P(intro);

            // Possible meanings of data:

            // data[0] & 0x07 - sequence number
            //
            // data[1] values:
            // - 0x11: Stopping guidance??
            // - 0x15: In guidance mode
            // - 0x20: Idle, not ready??
            // - 0x21: Idle, ready??
            // - 0x25: Busy calculating route??
            // - 0x41: Finished downloading?? Audio volume dialog??
            // - 0xC1: Finished downloading??
            // Or bits:
            // - & 0x01: ??
            // - & 0x04: ??
            // - & 0x10: ??
            // - & 0x20: ??
            // - & 0x40: ??
            // - & 0x80: ??
            //
            // data[2] values: 0x38, 0x39, 0x3A, 0x3C, 0x78, 0x79, 0x7C
            // Bits:
            // - & 0x01: GPS fix
            // - & 0x02: GPS signal lost
            // - & 0x04: Scanning for GPS signal (at startup, or when driving under bridge on in tunnel)
            // - & 0x08: Always 1
            // - & 0x70:
            //   0x30 = Disc recognized / valid
            //   0x70 = No Disc present
            //
            // data[3] - either 0x00 or 0x02
            // 
            // data[4] - always 0x07
            //
            // data[5] - either 0x01 or 0x06
            //
            // data[6] - either 0x01, 0x02, or 0x04
            //
            // data[7...8] - always 0x00
            //
            // data[9] << 8 | data[10] - always either 0x0000 or 0x01F4 (500)
            //
            // data[11...15] - always 0x00
            //
            // data[16] - vehicle speed (as measured by GPS?) in km/h. Can be negative (e.g. 0xFC) when reversing.
            //
            // data[17] values: 0x00, 0x20, 0x21, 0x22, 0x28, 0x29, 0x2A, 0x2C, 0x2D, 0x30, 0x38, 0xA1
            // Bits:
            // - & 0x01 - Loading audio fragment
            // - & 0x02 - Audio output
            // - & 0x04 - New guidance instruction??
            // - & 0x08 - Reading disc??
            // - & 0x10 - Calculating route??
            // - & 0x20 - Disc present??
            // - & 0x80 - Reached destination (sure!)
            //
            // data[18] - always 0x00
            //
            // data[19] - same as data[0]: sequence number

            // uint16_t status = (uint16_t)data[1] << 8 | data[2];

            // char buffer[12];
            // sprintf_P(buffer, PSTR("0x%04X-0x%02X"), status, data[17]);

            // SERIAL.printf(
                // "status=%s",
                // status == 0x2038 ? "CD_ROM_FOUND" :
                    // status == 0x203C ? "INITIALIZING" :  // data[17] == 0x28
                    // status == 0x213C ? "READING_CDROM" :
                    // status == 0x2139 && data[17] == 0x20 ? "DISC_PRESENT" :
                    // status == 0x2139 && data[17] == 0x28 ? "DISC_IDENTIFIED" :
                    // status == 0x2139 && data[17] == 0x29 ? "SHOWING_TERMS_AND_CONDITIONS" : 
                    // status == 0x2139 && data[17] == 0x2A ? "READ_WELCOME_MESSAGE" : 
                    // status == 0x2539 ? "CALCULATING_ROUTE" :
                    // status == 0x1539 ? "GUIDANCE_ACTIVE" :
                    // status == 0x1139 ? "GUIDANCE_STOPPED" :
                    // status == 0x2039 ? "POWER_OFF" :
                    // buffer
            // );

            SERIAL.printf_P(PSTR("status=%S"),

                data[1] == 0x11 ? PSTR("STOPPING_GUIDANCE") :
                data[1] == 0x15 ? PSTR("IN_GUIDANCE_MODE") :
                data[1] == 0x20 ? PSTR("IDLE_NOT_READY") :
                data[1] == 0x21 ? PSTR("IDLE_READY") :
                data[1] == 0x25 ? PSTR("CALCULATING_ROUTE") :
                data[1] == 0x41 ? PSTR("NOT_ON_MAP") :  // TODO - check if this is correct
                data[1] == 0xC1 ? PSTR("FINISHED_DOWNLOADING") :
                ToHexStr(data[1])
            );

            SERIAL.printf_P(PSTR(", disc=%S, gps_fix=%S, gps_fix_lost=%S, gps_scanning=%S"),

                (data[2] & 0x70) == 0x70 ? PSTR("NONE_PRESENT") :
                (data[2] & 0x70) == 0x30 ? PSTR("RECOGNIZED") :
                ToHexStr((uint8_t)(data[2] & 0x70)),

                data[2] & 0x01 ? yesStr : noStr,
                data[2] & 0x02 ? yesStr : noStr,
                data[2] & 0x04 ? yesStr : noStr
            );

            if (data[17] != 0x00)
            {
                SERIAL.printf_P(PSTR(", disc_status=%S%S%S%S%S%S%S"),
                    data[17] & 0x01 ? PSTR("LOADING_AUDIO_FRAGMENT ") : emptyStr,
                    data[17] & 0x02 ? PSTR("AUDIO_OUTPUT ") : emptyStr,
                    data[17] & 0x04 ? PSTR("NEW_GUIDANCE_INSTRUCTION ") : emptyStr,
                    data[17] & 0x08 ? PSTR("READING_DISC ") : emptyStr,
                    data[17] & 0x10 ? PSTR("CALCULATING_ROUTE ") : emptyStr,
                    data[17] & 0x20 ? PSTR("DISC_PRESENT ") : emptyStr,
                    data[17] & 0x80 ? PSTR("REACHED_DESTINATION ") : emptyStr
                );
            } // if

            // TODO - what is this?
            uint16_t zzz = (uint16_t)data[9] << 8 | data[10];
            if (zzz != 0x00) SERIAL.printf_P(PSTR(", zzz=%u"), zzz);

            SERIAL.printf_P(PSTR(", gps_speed=%u km/h%S"),

                // 0xE0 as boundary for "reverse": just guessing. Do we ever drive faster than 224 km/h?
                data[16] < 0xE0 ? data[16] : 0xFF - data[16] + 1,

                data[16] >= 0xE0 ? PSTR(" (reverse)") : emptyStr
            );

            SERIAL.println();
        }
        break;

        case SATNAV_STATUS_3_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#8CE
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#8CE

            // Examples:
            // Raw: #2733 ( 8/15)  7 0E 8CE WA0 00-00-62-98 ACK OK 6298 CRC_OK
            // Raw: #2820 ( 5/15)  7 0E 8CE WA0 00-01-7D-A2 ACK OK 7DA2 CRC_OK
            // Raw: #5252 ( 2/15)  7 0E 8CE WA0 0C-02-3E-48 ACK OK 3E48 CRC_OK
            // Raw: #2041 (11/15)  7 0E 8CE WA0 0C-01-1F-06 ACK OK 1F06 CRC_OK
            // Raw: #2103 (13/15) 22 0E 8CE WA0 20-50-41-34-42-32-35-30-30-53-42-20-00-30-30-31-41-14-E6 ACK OK 14E6 CRC_OK
            // Raw: #2108 ( 3/15)  7 0E 8CE WA0 01-40-83-52 ACK OK 8352 CRC_OK
            // Raw: #5129 ( 9/15)  8 0E 8CE WA0 04-04-00-B2-B8 ACK OK B2B8 CRC_OK
            // Raw: #1803 ( 8/15)  8 0E 8CE WA0 04-02-00-83-EA ACK OK 83EA CRC_OK

            // Possible meanings of data:

            SERIAL.print(F("--> SatNav status 3: "));

            if (dataLen != 2 && dataLen != 3 && dataLen != 17)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            if (dataLen == 2)
            {
                uint16_t status = (uint16_t)data[0] << 8 | data[1];

                // TODO - check; total guess
                SERIAL.printf_P(
                    PSTR("status=%S\n"),
                    status == 0x0000 ? PSTR("CALCULATING_ROUTE") :
                    status == 0x0001 ? PSTR("STOPPING_NAVIGATION") :
                    status == 0x0C01 ? PSTR("CD_ROM_FOUND") :
                    status == 0x0C02 ? PSTR("POWERING_OFF") :
                    status == 0x0140 ? PSTR("GPS_POS_FOUND") :
                    status == 0x0120 ? PSTR("ACCEPTED_TERMS_AND_CONDITIONS") :

                    // This starts when the nag screen is accepted and is seen repeatedly when selecting a destination
                    // and during guidance. It stops after a "STOPPING_NAVIGATION" status message.
                    status == 0x0108 ? PSTR("SATNAV_IN_OPERATION") :

                    ToHexStr(status)
                );
            }
            else if (dataLen == 3)
            {
                // Navigation preference

                SERIAL.printf_P(
                    PSTR("satnav_navigation_preference=%S\n"),
                    data[1] == 0x01 ? PSTR("FASTEST_ROUTE") :
                    data[1] == 0x04 ? PSTR("SHORTEST_DISTANCE") :
                    data[1] == 0x12 ? PSTR("AVOID_HIGHWAY") :
                    data[1] == 0x02 ? PSTR("COMPROMISE_FAST_SHORT") :
                    ToHexStr(data[1])
                );
            }
            else if (dataLen == 17 && data[0] == 0x20)
            {
                // Some set of ID strings. Stays the same even when the navigation CD is changed.
                SERIAL.print(F("system_id="));

                char txt[VAN_MAX_DATA_BYTES - 1 + 1];  // Max 28 data bytes, minus header (1), plus terminating '\0'

                int at = 1;
                while (at < dataLen)
                {
                    strncpy(txt, (const char*) data + at, dataLen - at);
                    txt[dataLen - at] = 0;
                    SERIAL.printf_P(PSTR("'%s' - "), txt);
                    at += strlen(txt) + 1;
                } // while

                SERIAL.println();
            } // if
        }
        break;

        case SATNAV_GUIDANCE_DATA_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#9CE
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#9CE

            SERIAL.print(F("--> SatNav guidance data: "));

            if (dataLen != 16)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            // This packet precedes a packet with IDEN value 0x64E (SATNAV_GUIDANCE_IDEN).
            // This packet contains direction data during guidance.
            //
            // Meanings of data:
            //
            // - data[0] & 0x0F , data[15] & 0x0F: sequence number
            //
            // - data[1], [2]: current heading in compass degrees (0...359)
            //
            // - data[3], [4]: heading to destination in compass degrees (0...359)
            //
            // - (data[5] & 0x7F) << 8 | data[6]: remaining distance to destination
            //   Unit (kilometers or meters) is encoded in most significant bit:
            //   & 0x80: 1 = in kilometers (>= 10 km), 0 = in meters (< 10 km)
            //
            // - (data[7] & 0x7F) << 8 | data[8]: distance to destination in straight line
            //   Unit (kilometers or meters) is encoded in most significant bit:
            //   & 0x80: 1 = in kilometers (>= 10 km), 0 = in meters (< 10 km)
            //
            // - (data[9] & 0x7F) << 8 | data[10]: distance to next guidance instruction
            //   Unit (kilometers or meters) is encoded in most significant bit:
            //   & 0x80: 1 = in kilometers (>= 10 km), 0 = in meters (< 10 km)
            //
            // - data [11] << 8 & data[12]: Usually 0x7FFF. If not, low values (maximum value seen is 0x0167). Some
            //   kind of heading indication? Seen only when driving on a roundabout. For indicating when to take the
            //   exit of a roundabout?
            //
            // - data [13] << 8 & data[14]: Some distance value?? If so, always in km. Or: remaining time in minutes
            //   (TTG)? Or: the number of remaining guidance instructions until destination?
            //   Decreases steadily while driving, but can jump up after a route has been recalculated. When
            //   decreasing, sometimes just skips a value.
            //
            uint16_t currHeading = (uint16_t)data[1] << 8 | data[2];
            uint16_t headingToDestination = (uint16_t)data[3] << 8 | data[4];
            uint16_t distanceToDestination = (uint16_t)(data[5] & 0x7F) << 8 | data[6];
            uint16_t gpsDistanceToDestination = (uint16_t)(data[7] & 0x7F) << 8 | data[8];
            uint16_t distanceToNextTurn = (uint16_t)(data[9] & 0x7F) << 8 | data[10];
            uint16_t headingOnRoundabout = (uint16_t)data[11] << 8 | data[12];
            uint16_t minutesToTravel = (uint16_t)data[13] << 8 | data[14];

            char floatBuf[MAX_FLOAT_SIZE];
            SERIAL.printf_P(
                PSTR(
                    "curr_heading=%u deg, heading_to_dest=%u deg, distance_to_dest=%u %S,"
                    " distance_to_dest_straight_line=%u %S, turn_at=%u %S,\n"
                    " heading_on_roundabout=%S deg, minutes_to_travel=%u\n"
                ),
                currHeading,
                headingToDestination,
                distanceToDestination,
                data[5] & 0x80 ? PSTR("Km") : PSTR("m") ,
                gpsDistanceToDestination,
                data[7] & 0x80 ? PSTR("Km") : PSTR("m") ,
                distanceToNextTurn,
                data[9] & 0x80 ? PSTR("Km") : PSTR("m"),
                headingOnRoundabout == 0x7FFF ? notApplicable3Str : FloatToStr(floatBuf, headingOnRoundabout, 0),
                minutesToTravel
            );
        }
        break;

        case SATNAV_GUIDANCE_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#64E
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#64E

            SERIAL.print(F("--> SatNav guidance: "));

            if (dataLen != 3 && dataLen != 4 && dataLen != 6 && dataLen != 13 && dataLen != 23)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            // Packet appears as soon as the first guidance instruction is given.
            // Just before this packet there is always a packet with IDEN value 0x9CE (SATNAV_GUIDANCE_DATA_IDEN).
            //
            // Meanings of data:
            //
            // - data[0] & 0x0F , data[dataLen - 1] & 0x0F: sequence number
            //
            // - data[1]: current instruction (large icon in left of MFD)
            //   0x01: Single turn instruction ("Turn left",  dataLen = 6 or 13)
            //   0x03: Double turn instruction ("Turn left, then turn right", dataLen = 23)
            //   0x04: Turn around if possible (dataLen = 3)
            //   0x05: Follow current road until next instruction (dataLen = 4)
            //   0x06: Not on map; follow heading (dataLen = 4)
            //
            // If data[1] == 0x01 or data[1] == 0x03, then the remaining bytes (data[4], data[4...11] or
            // data[6...21]) describe the detailed shape of the large navigation icon as shown in the left of the MFD.
            // Note: the basic shape (junction or roundabout) is determined by the data[2] value as found in the
            // last received packet with data[1] == 0x05 ("Follow current road until next instruction").
            //
            // - If data[1] == 0x01 and data[2] == 0x02: fork or exit instruction; dataLen = 6
            //   * data[4]:
            //     0x41: keep left on fork
            //     0x14: keep right on fork
            //     0x12: take right exit
            //
            // - If data[1] == 0x01 and (data[2] == 0x00 || data[2] == 0x01): one "detailed instruction"; dataLen = 13
            //   * data[4...11]: current instruction ("turn left")
            //
            // - If data[1] == 0x03: two "detailed instructions"; dataLen = 23
            //   * data[6...13]: current instruction ("turn left ...")
            //   * data[14...21]: next instruction ("... then turn right")
            //
            //   A "detailed instruction" consists of 8 bytes:
            //   * 0   : turn angle in increments of 22.5 degrees, measured clockwise, starting with 0 at 6 o-clock.
            //           E.g.: 0x4 == 90 deg left, 0x8 = 180 deg = straight ahead, 0xC = 270 deg = 90 deg right.
            //   * 1   : always 0x00 ??
            //   * 2, 3: bit pattern indicating which legs are present in the junction or roundabout. Each bit set is
            //           for one leg.
            //           Lowest bit of byte 3 corresponds to the leg of 0 degrees (straight down, which is
            //           always there, because that is where we are currently driving), running clockwise up to the
            //           highest bit of byte 2, which corresponds to a leg of 337.5 degrees (very sharp right).
            //   * 4, 5: bit pattern indicating which legs in the junction are "no entry". The coding of the bits is
            //           the same as for bytes 2 and 3.
            //   * 6   : always 0x00 ??
            //   * 7   : always 0x00 ??
            //
            // - If data[1] == 0x05: wait for next instruction; follow current road (dataLen = 4)
            //   data[2]: small icon (between current street and next street, indicating next instruction)
            //     Note: the icon indicated here is shown in the large icon as soon as the "detailed instruction"
            //       (see above) is received.
            //     0x00: No icon
            //     0x01: Junction: turn right
            //     0x02: Junction: turn left
            //     0x04: Roundabout
            //     0x08: Go straight
            //     0x10: Retrieving next instruction
            //
            // - If data[1] == 0x06: not on map; follow heading (dataLen = 4)
            //   data[2]: angle of heading to maintain, in increments of 22.5 degrees, measured clockwise, starting
            //      with 0 at 6 o-clock.
            //      E.g.: 0x4 is 90 deg left, 0x8 is 180 deg (straight ahead), 0xC is 270 deg (90 deg right).
            //
            // - data[3]: ?? Seen values: 0x00, 0x01, 0x20, 0x42, 0x53
            //
            // =====
            // Just some examples:
            //
            //     01-02-53-41 is shown as:
            //           ^  |
            //           || |
            //           \\ /
            //            ++
            //            ||
            //            ||
            //       (keep left on fork)
            //
            //     01-02-53-14 is shown as:
            //           |  ^
            //           | ||
            //           \ //
            //            ++
            //            ||
            //            ||
            //       (keep right on fork)
            //
            //     01-02-53-12 is shown as:
            //            |  ^
            //            | ||
            //            |//
            //            ++
            //            ||
            //            ||
            //       (take right exit)
            //
            //     01-00-53-04-00-12-11-00-00-00-00 is shown as:
            //               /
            //              /
            //         <===+---
            //            ||
            //            ||
            //       (turn left 90 deg; ahead = +202.5 deg; right = 270 deg)
            //
            //     01-00-53-04-00-01-11-00-00-00-00 is shown as:
            //             |
            //             |
            //         <===+
            //            ||
            //            ||
            //       (turn left 90 deg; ahead = 180 deg; no right)
            //
            //     01-00-53-03-00-11-09-10-00-00-00 is:
            //                |
            //                |
            //             /==+--- (-)
            //         <==/  ||
            //               ||
            //       (turn left -67.5 deg; ahead = 180 deg; right = 270 deg: no entry)
            //
            //     01-00-53-05-00-02-21-00-00-00-00 is:
            //                  /
            //         <==\    /
            //             \==+
            //               ||
            //               ||
            //       (turn left 112.5 deg; ahead = +202.5 deg; no right)
            //
            //     01-00-53-0C-00-10-21-00-00-00-00
            //
            //         --\
            //            \--+===>
            //              ||
            //              ||
            //       (turn right 270 deg; no ahead; left = 112.5 deg)
            //
            //     01-01-53-09-00-22-21-00-00-00-00 is:
            //                /
            //               /
            //             --+
            //            /   \\
            //     --\   /     \\
            //        \--+     ++--\
            //           \    //    \--
            //            \++//
            //             ||
            //             ||
            //
            //       (take second exit on roundabout @ 202.5 degrees, exits on 112.5, 202.5 and 290.5 degrees)
            //
            //     01-00-01-0C-00-11-11-00-10-00-00 is:
            //             |
            //             |
            //      (-) ---+===>
            //            ||
            //            ||
            //       (turn right 270 deg; ahead = 180 deg; left = 90: no entry)
            //
            //     01-00-01-04-00-10-13-00-03-00-00 is:
            //
            //         <===+---
            //            /||
            //           / ||
            //          (-)
            //       (turn left 90 deg; no ahead; right = 270 deg, sharp left = 22.5 deg: no entry)
            //
            //     03-00-53-00-41-0C-00-11-01-00-00-00-00-0B-00-08-11-00-00-00-00 is:
            //             |
            //             |
            //             +===>
            //            ||
            //            ||
            //       (current instruction: turn right 270 deg; ahead = 180 deg; no left)
            //
            //     03-00-53-00-41-0C-00-10-11-00-10-00-00-04-00-12-11-00-00-00-00 is:
            //
            //      (-) ---+===>
            //            ||
            //            ||
            //       (current instruction: turn right 270 deg; no ahead; left = 90 deg: no entry)
            //
            //     03-00-53-00-63-04-00-01-11-00-00-00-00-03-00-08-09-00-00-00-00 and
            //     03-00-53-00-32-04-00-01-11-00-00-00-00-05-00-02-21-00-00-00-00 is:
            //             |
            //             |
            //         <===+
            //            ||
            //            ||
            //       (current instruction: turn left 90 deg; ahead = 180 deg; no right)
            //
            //     03-00-53-00-3A-0B-00-08-11-00-00-00-00-04-00-10-11-00-00-00-00 is:
            //
            //                 /==>
            //      (-) ---+==/
            //            ||
            //            ||
            //       (current instruction: turn right 247.2 deg; no ahead; left = 90 deg)
            //       (next instruction: turn left 90 deg; no ahead; right = 270 deg)
            //

            SERIAL.printf_P(PSTR("guidance_instruction=%S\n"),
                data[1] == 0x01 ? PSTR("SINGLE_TURN") :
                data[1] == 0x03 ? PSTR("DOUBLE_TURN") :
                data[1] == 0x04 ? PSTR("TURN_AROUND_IF_POSSIBLE") :
                data[1] == 0x05 ? PSTR("FOLLOW_ROAD") :
                data[1] == 0x06 ? PSTR("NOT_ON_MAP") :
                ToHexStr(data[1], data[2])
            );

            if (data[1] == 0x01)  // Single turn
            {
                if (data[2] == 0x00 || data[2] == 0x01)
                {
                    if (dataLen != 13)
                    {
                        SERIAL.print(FPSTR(indentStr));
                        SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                        return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                    } // if

                    // One "detailed instruction" in data[4...11]
                    SERIAL.print(F("    current_instruction=\n"));
                    PrintGuidanceInstruction(data + 4);
                }
                else if (data[2] == 0x02)
                {
                    if (dataLen != 6)
                    {
                        SERIAL.print(FPSTR(indentStr));
                        SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                        return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                    } // if

                    // Fork or exit instruction
                    SERIAL.printf_P(PSTR("    current_instruction=%S\n"),

                        // Pretty sure there are more values
                        data[4] == 0x41 ? PSTR("KEEP_LEFT_ON_FORK") :
                        data[4] == 0x14 ? PSTR("KEEP_RIGHT_ON_FORK") :
                        data[4] == 0x12 ? PSTR("TAKE_RIGHT_EXIT") :

                        ToHexStr(data[4])
                    );
                }
                else
                {
                    SERIAL.printf("    unknown(%s)\n", ToHexStr(data[2]));
                } // if
            }
            else if (data[1] == 0x03)  // Double turn
            {
                if (dataLen != 23)
                {
                    SERIAL.print(FPSTR(indentStr));
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                // Two "detailed instructions": current in data[6...13], next in data[14...21]
                SERIAL.print(F("    current_instruction=\n"));
                PrintGuidanceInstruction(data + 6);
                SERIAL.print(F("    next_instruction=\n"));
                PrintGuidanceInstruction(data + 14);
            }
            else if (data[1] == 0x04)  // Turn around if possible
            {
                if (dataLen != 3)
                {
                    SERIAL.print(FPSTR(indentStr));
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                SERIAL.print(F("    current_instruction=TURN_AROUND_IF_POSSIBLE\n"));
            } // if
            else if (data[1] == 0x05)  // Follow road
            {
                if (dataLen != 4)
                {
                    SERIAL.print(FPSTR(indentStr));
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                SERIAL.printf_P(PSTR("    follow_road_next_instruction=%S\n"),
                    data[2] == 0x00 ? noneStr :
                    data[2] == 0x01 ? PSTR("TURN_RIGHT") :
                    data[2] == 0x02 ? PSTR("TURN_LEFT") :
                    data[2] == 0x04 ? PSTR("ROUNDABOUT") :
                    data[2] == 0x08 ? PSTR("GO_STRAIGHT_AHEAD") :
                    data[2] == 0x10 ? PSTR("RETRIEVING_NEXT_INSTRUCTION") :
                    ToHexStr(data[2])
                );
            }
            else if (data[1] == 0x06)  // Not on map
            {
                if (dataLen != 4)
                {
                    SERIAL.print(FPSTR(indentStr));
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                SERIAL.printf_P(PSTR("    not_on_map_follow_heading=%u\n"), data[2]);
            } // if
        }
        break;

        case SATNAV_REPORT_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#6CE
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#6CE

            SERIAL.print(F("--> SatNav report: "));

            if (dataLen < 3)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            // Meanings of data:
            //
            // data[0]: starts low for the first packet in the sequence (0x02, 0x05), then increments with each
            //   subsequent packet with either 9 or 1.
            //   & 0x80: last packet in sequence
            //
            // data[1]: Code of report; see function SatNavRequestStr(). Only in first packet.
            //
            // data[dataLen - 1] (last data byte): always the same as the first (data[0]).
            //
            // Lists are formatted as follows:
            // - Record: terminated with '\1', containing a set of:
            //   - Strings: terminated with '\0'
            //     - Special characters:
            //       * 0x81 = '+' in solid circle, means: this destination cannot be selected with the current
            //         navigation disc. Something from UTF-8? See also U+2A01 (UTF-8 0xe2 0xa8 0x81) at:
            //         https://www.utf8-chartable.de/unicode-utf8-table.pl?start=10752&number=1024&utf8=0x&htmlent=1
            //
            // Character set is "Extended ASCII/Windows-1252"; e.g. ë is 0xEB.
            // See also: https://bytetool.web.app/en/ascii/
            //
            // An address is formatted as an array of strings. There can be up to 12 array elements ([0]...[11]):
            //
            // [0] "V" (Ville? Vers?) or "C" (Coordinates?)
            //     Observed:
            //     * "V": the address ends with city (+ optional district), then street and house number (or '0'
            //            if unknown or not applicable)
            //     * "C" the address ends with city (+ optional district), then GPS "coordinate"
            // [1] Country
            // [2] Province
            // [3] City
            // [4] District (or empty string)
            // [5] Either:
            //     * "G": (iGnore?) which can be followed by text that is not important for searching on, e.g.
            //            "GRue de"
            //     * "I": Next entry is not a street name but a building name ??
            // [6] Street name (important part)
            //
            // Then, depending on the content of string [0] ("V" or "C"):
            // * Type "V"
            //   [7]: house number (or "0" if unknown or not applicable),
            //   For  "private" and "business" address:
            //   [8] Name of entry (e.g. "HOME")
            // * Type "C"
            //   [7], [8]: GPS coordinates (e.g. "+495456", "+060405"). Note that these coordinates are in degrees, NOT
            //             in decimal notation. So the just given example would translate to: 49°54'56"N 6°04'05"E.
            //             There seems to be however some offset, because the shown GPS coordinates do not exactly
            //             match the destination.
            //   For  "private" and "business" address:
            //   [9] Name of entry (e.g. "HOME")
            //
            //
            // For "place of interest" address (always type "C"):
            // [9] Name of entry (e.g. "CINE SCALA")
            // [10] Always "?"
            // [11] Distance in meters from current location (e.g. "12000"). TODO - not sure.

            #define MAX_SATNAV_STRING_SIZE 128
            static char buffer[MAX_SATNAV_STRING_SIZE];
            static int offsetInBuffer = 0;

            int offsetInPacket = 1;

            if ((data[0] & 0x7F) <= 7)
            {
                // First packet of a report sequence
                offsetInPacket = 2;
                offsetInBuffer = 0;

                SERIAL.printf_P(PSTR("report=%S:\n    "), SatNavRequestStr(data[1]));
            }
            else
            {
                // TODO - check if data[0] & 0x7F has incremented by either 1 or 9 w.r.t. the last received packet.
                // If it has incremented by e.g. 10 (9+1) or 18 (9+9), then we have obviously missed a packet, so
                // appending the text of the current packet to that of the previous packet would be incorrect.

                SERIAL.print(F("\n    "));
            } // if

            while (offsetInPacket < dataLen - 1)
            {
                // New record?
                if (data[offsetInPacket] == 0x01)
                {
                    offsetInPacket++;
                    offsetInBuffer = 0;
                    SERIAL.print(F("\n    "));
                } // if

                int maxLen = dataLen - 1 - offsetInPacket;
                if (offsetInBuffer + maxLen >= MAX_SATNAV_STRING_SIZE)
                {
                    maxLen = MAX_SATNAV_STRING_SIZE - offsetInBuffer - 1;
                } // if

                strncpy(buffer + offsetInBuffer, (const char*) data + offsetInPacket, maxLen);
                buffer[offsetInBuffer + maxLen] = 0;

                offsetInPacket += strlen(buffer) + 1 - offsetInBuffer;
                if (offsetInPacket <= dataLen - 1)
                {
                    SERIAL.printf_P(PSTR("'%s' - "), buffer);
                    offsetInBuffer = 0;
                }
                else
                {
                    offsetInBuffer = strlen(buffer);
                } // if
            } // while

            // Last packet in report sequence?
            if (data[0] & 0x80) SERIAL.print(F("--LAST--"));
            SERIAL.println();
        }
        break;

        case MFD_TO_SATNAV_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#94E
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#94E

            SERIAL.print(F("--> MFD to SatNav: "));

            if (dataLen != 4 && dataLen != 9 && dataLen != 11)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            // Possible meanings of data:
            //
            // -----
            // dataLen is 4, 9 or 11:
            //
            // * data[0]: request code
            //
            // * data[1]: parameter
            //   - 0x0D: user is selecting an entry from a list
            //   - 0x0E: user is selecting an entry from a list
            //   - 0x1D: user is entering data (city, street, house number, ...)
            //   - 0xFF: user or MFD is requesting a list or data item
            //
            // * data[2]: type
            //   - 0 (dataLen = 4): request length of list
            //   - 1 (dataLen = 9): request list
            //       -- data[5] << 8 | data[6]: offset (0-based)
            //       -- data[7] << 8 | data[8]: number of items requested
            //   - 2 (dataLen = 11): select item
            //       -- data[5] << 8 | data[6]: selected item (0-based)
            //
            // data[3]: selected letter, digit or character (A..Z 0..9 ' <space> <Esc>); 0 if not applicable
            //
            // -----
            // dataLen is 9 or 11:
            //
            // * data[4]: always 0
            //
            // * data[5] << 8 | data[6]: selected item or offset (0-based)
            //
            // * data[7] << 8 | data[8]: number of items or length; 0 if not applicable
            //
            // -----
            // dataLen 11:
            //
            // * data[9]: always 0
            //
            // * data[10]: always 0
            //
            uint8_t request = data[0];
            uint8_t param = data[1];
            uint8_t type = data[2];

            SERIAL.printf_P(
                PSTR("request=%S, param=%S, type=%S, effective_command=%S"),

                SatNavRequestStr(request),

                ToHexStr(param),  // TODO - provide descriptive string

                type == 0 ? PSTR("REQ_LIST_LENGTH") :
                type == 1 ? PSTR("REQ_LIST") :
                type == 2 ? PSTR("SELECT") :
                type == 3 ? PSTR("CITY_CENTER") :  // TODO - not sure
                ToStr(type),

                // Combinations:
                //
                // * request == 0x02 ("ENTER_CITY"), 0x05 ("ENTER_STREET"),
                //   param == 0x1D:
                //   - type = 0 (dataLen = 4): request (remaining) list length
                //     -- data[3]: (next) character to narrow down selection with. 0x00 if none.
                //   - type = 1 (dataLen = 9): request list 
                //     -- data[5] << 8 | data[6]: offset in list (0-based)
                //     -- data[7] << 8 | data[8]: number of items to retrieve
                //   - type = 2 (dataLen = 11): select entry
                //     -- data[5] << 8 | data[6]: selected entry (0-based)

                request == SR_ENTER_CITY && param == 0x1D && type == 0 ? "ENTER_CITY_BY_LETTER" :
                request == SR_ENTER_CITY && param == 0x1D && type == 1 ? "ENTER_CITY_GET_LIST" :
                request == SR_ENTER_CITY && param == 0x1D && type == 2 ? "ENTER_CITY_SELECT" :

                request == SR_ENTER_STREET && param == 0x1D && type == 0 ? "ENTER_STREET_BY_LETTER" :
                request == SR_ENTER_STREET && param == 0x1D && type == 1 ? "ENTER_STREET_GET_LIST" :
                request == SR_ENTER_STREET && param == 0x1D && type == 2 ? "ENTER_STREET_SELECT" :

                // * request == 0x06 ("ENTER_HOUSE_NUMBER"),
                //   param == 0x1D:
                //   - type = 1 (dataLen = 9): request range of house numbers
                //     -- data[7] << 8 | data[8]: always 1
                //   - type = 2 (dataLen = 11): enter house number
                //     -- data[5] << 8 | data[6]: entered house number. 0 if not applicable.

                request == SR_ENTER_HOUSE_NUMBER && param == 0x1D && type == 1 ? "ENTER_HOUSE_NUMBER_GET_RANGE" :
                request == SR_ENTER_HOUSE_NUMBER && param == 0x1D && type == 2 ? "ENTER_HOUSE_NUMBER_SELECT" :

                // * request == 0x08 ("PLACE_OF_INTEREST_CATEGORY"),
                //   param == 0x0D:
                //   - type = 0 (dataLen = 4): request list length. Satnav will respond with 38 and no
                //              letters or number to choose from.
                //   - type = 2 (dataLen = 11): select entry from list
                //     -- data[5] << 8 | data[6]: selected entry (0-based)

                request == SR_PLACE_OF_INTEREST_CATEGORY && param == 0x0D && type == 0 ? "PLACE_OF_INTEREST_CATEGORY_GET_LIST_LENGTH" :
                request == SR_PLACE_OF_INTEREST_CATEGORY && param == 0x0D && type == 2 ? "PLACE_OF_INTEREST_CATEGORY_SELECT" :

                // * request == 0x08 ("PLACE_OF_INTEREST_CATEGORY"),
                //   param == 0xFF:
                //   - type = 0 (dataLen = 4): present nag screen. Satnav response is PLACE_OF_INTEREST_CATEGORY_LIST
                //              with list_size=38, but the MFD ignores that.
                //   - type = 1 (dataLen = 9): request list 
                //     -- data[5] << 8 | data[6]: offset in list (always 0)
                //     -- data[7] << 8 | data[8]: number of items to retrieve (always 38)

                request == SR_PLACE_OF_INTEREST_CATEGORY && param == 0xFF && type == 0 ? "START_SATNAV_SESSION" :
                request == SR_PLACE_OF_INTEREST_CATEGORY && param == 0xFF && type == 1 ? "PLACE_OF_INTEREST_CATEGORY_GET_LIST" :

                // * request == 0x09 ("PLACE_OF_INTEREST"),
                //   param == 0x0D:
                //   - type = 0 (dataLen = 4): request list length
                //   - type = 1 (dataLen = 9): request list 
                //     -- data[5] << 8 | data[6]: offset in list (always 0)
                //     -- data[7] << 8 | data[8]: number of items to retrieve (always 1: MFD browses address by address)

                request == SR_PLACE_OF_INTEREST && param == 0x0D && type == 0 ? "PLACE_OF_INTEREST_GET_LIST_LENGTH" :
                request == SR_PLACE_OF_INTEREST && param == 0x0D && type == 1 ? "PLACE_OF_INTEREST_GET_LIST" :

                // * request == 0x09 ("PLACE_OF_INTEREST"),
                //   param == 0x0E:
                //   - type = 2 (dataLen = 11): select entry from list
                //     -- data[5] << 8 | data[6]: selected entry (0-based)

                request == SR_PLACE_OF_INTEREST && param == 0x0E && type == 2 ? "SELECT_PLACE_OF_INTEREST_ENTRY" :

                // * request == 0x0E ("LAST_DESTINATION"),
                //   param == 0x0D:
                //   - type = 2 (dataLen = 11):
                //     -- no further data

                request == SR_LAST_DESTINATION && param == 0x0D && type == 2 ? "SELECT_LAST_DESTINATION" :

                // * request == 0x0E ("LAST_DESTINATION"),
                //   param == 0xFF:
                //   - type = 1 (dataLen = 9):
                //     -- data[5] << 8 | data[6]: offset in list (always 0)
                //     -- data[7] << 8 | data[8]: number of items to retrieve (always 1)

                request == SR_LAST_DESTINATION && param == 0xFF && type == 1 ? "GET_LAST_DESTINATION" :

                // * request == 0x0F ("NEXT_STREET"),
                //   param == 0xFF:
                //   - type = 1 (dataLen = 9):
                //     -- data[5] << 8 | data[6]: offset in list (always 0)
                //     -- data[7] << 8 | data[8]: number of items to retrieve (always 1)

                request == SR_NEXT_STREET && param == 0xFF && type == 1 ? "GET_NEXT_STREET" :

                // * request == 0x10 ("CURRENT_STREET"),
                //   param == 0x0D:
                //   - type = 2 (dataLen = 11): select current location for e.g. places of interest
                //     -- no further data

                request == SR_CURRENT_STREET && param == 0x0D && type == 2 ? "SELECT_CURRENT_STREET" :

                // * request == 0x10 ("CURRENT_STREET"),
                //   param == 0xFF:
                //   - type = 1 (dataLen = 9): get current location during sat nav guidance
                //     -- data[5] << 8 | data[6]: offset in list (always 0)
                //     -- data[7] << 8 | data[8]: number of items to retrieve (always 1)

                request == SR_CURRENT_STREET && param == 0xFF && type == 1 ? "GET_CURRENT_STREET" :

                // * request == 0x11 ("PRIVATE_ADDRESS"),
                //   param == 0x0E:
                //   - type = 2 (dataLen = 11): select entry from list
                //     -- data[5] << 8 | data[6]: selected entry (0-based)

                request == SR_PRIVATE_ADDRESS && param == 0x0E && type == 2 ? "SELECT_PRIVATE_ADDRESS_ENTRY" :

                // * request == 0x11 ("PRIVATE_ADDRESS"),
                //   param == 0xFF:
                //   - type = 1 (dataLen = 9): get entry
                //     -- data[5] << 8 | data[6]: selected entry (0-based)

                request == SR_PRIVATE_ADDRESS && param == 0xFF && type == 1 ? "RETRIEVE_PRIVATE_ADDRESS" :

                // * request == 0x12 ("BUSINESS_ADDRESS"),
                //   param == 0x0E:
                //   - type = 2 (dataLen = 11): select entry from list
                //     -- data[5] << 8 | data[6]: selected entry (0-based)

                request == SR_BUSINESS_ADDRESS && param == 0x0E && type == 2 ? "SELECT_BUSINESS_ADDRESS_ENTRY" :

                // * request == 0x12 ("BUSINESS_ADDRESS"),
                //   param == 0xFF:
                //   - type = 1 (dataLen = 9): get entry
                //     -- data[5] << 8 | data[6]: selected entry (0-based)

                request == SR_BUSINESS_ADDRESS && param == 0xFF && type == 1 ? "RETRIEVE_BUSINESS_ADDRESS" :

                // * request == 0x1B ("PRIVATE_ADDRESS_LIST"),
                //   param == 0xFF:
                //   - type = 0 (dataLen = 4): request list length
                //   - type = 1 (dataLen = 9): request list 
                //     -- data[5] << 8 | data[6]: offset in list
                //     -- data[7] << 8 | data[8]: number of items to retrieve

                request == SR_PRIVATE_ADDRESS_LIST && param == 0xFF && type == 0 ? "PRIVATE_ADDRESS_GET_LIST_LENGTH" :
                request == SR_PRIVATE_ADDRESS_LIST && param == 0xFF && type == 1 ? "PRIVATE_ADDRESS_GET_LIST" :

                // * request == 0x1C ("BUSINESS_ADDRESS_LIST"),
                //   param == 0xFF:
                //   - type = 0 (dataLen = 4): request list length
                //   - type = 1 (dataLen = 9): request list 
                //     -- data[5] << 8 | data[6]: offset in list
                //     -- data[7] << 8 | data[8]: number of items to retrieve

                request == SR_BUSINESS_ADDRESS_LIST && param == 0xFF && type == 0 ? "BUSINESS_ADDRESS_GET_LIST_LENGTH" :
                request == SR_BUSINESS_ADDRESS_LIST && param == 0xFF && type == 1 ? "BUSINESS_ADDRESS_GET_LIST" :

                // * request == 0x1D ("DESTINATION"),
                //   param == 0x0E:
                //   - type = 2 (dataLen = 11): select fastest route
                //     -- no further data
                //
                // TODO - run a session in which something else than fastest route is selected

                request == SR_DESTINATION && param == 0x0E && type == 2 ? "SELECT_FASTEST_ROUTE" :

                // * request == 0x1D ("DESTINATION"),
                //   param == 0xFF:
                //   - type = 1 (dataLen = 9): get current destination. Satnav replies (IDEN 0x6CE) with the last
                //     destination, a city center with GPS coordinate (if no street has been entered yet), or a
                //     full address
                //     -- data[5] << 8 | data[6]: offset in list (always 0)
                //     -- data[7] << 8 | data[8]: number of items to retrieve (always 1)

                request == SR_DESTINATION && param == 0xFF && type == 1 ? "GET_CURRENT_DESTINATION" :

                ToHexStr(param, type)
/*
                Old list

                request == 0x021D ? PSTR("ENTER_CITY") : // 4, 9 or 11 bytes
                request == 0x051D ? PSTR("ENTER_STREET") : // 4, 9 or 11 bytes
                request == 0x061D ? PSTR("ENTER_HOUSE_NUMBER") : // 9 or 11 bytes
                request == 0x080D && dataLen == 4 ? PSTR("REQUEST_LIST_SIZE_OF_CATEGORIES") :
                request == 0x080D && dataLen == 9 ? PSTR("CHOOSE_CATEGORY") :

                // Strange: when starting a SatNav session, the MFD always starts off by asking the number of
                // items in the list of categories. It gets the correct answer (38) but just ignores that.
                // MFD shows the nag screen.
                request == 0x08FF && dataLen == 4 ? PSTR("START_SATNAV") :

                request == 0x08FF && dataLen == 9 ? PSTR("REQUEST_LIST_OF_CATEGORIES") :
                request == 0x090D ? PSTR("PLACE_OF_INTEREST_ADDRESS") :
                request == 0x090E ? PSTR("CHOOSE_PLACE_OF_INTEREST") :
                request == 0x0E0D ? PSTR("CHOOSE_ADDRESS_FOR_PLACES_OF_INTEREST") :
                request == 0x0EFF ? PSTR("REQUEST_ADDRESS_FOR_PLACES_OF_INTEREST") :
                request == 0x0FFF ? PSTR("REQUEST_NEXT_STREET") :
                request == 0x100D ? PSTR("CHOOSE_CURRENT_ADDRESS") :
                request == 0x10FF ? PSTR("REQUEST_CURRENT_STREET") :
                request == 0x110E ? PSTR("CHOOSE_PRIVATE_ADDRESS") :
                request == 0x11FF ? PSTR("REQUEST_PRIVATE_ADDRESS") :
                request == 0x120E ? PSTR("CHOOSE_BUSINESS_ADDRESS") :
                request == 0x12FF ? PSTR("REQUEST_BUSINESS_ADDRESS") :
                request == 0x13FF ? PSTR("REQUEST_SOFTWARE_MODULE_VERSIONS") :
                request == 0x1BFF && dataLen == 4 ? PSTR("REQUEST_LIST_SIZE_OF_PRIVATE_ADDRESSES") :
                request == 0x1BFF && dataLen == 9 ? PSTR("REQUEST_PRIVATE_ADDRESSES") :
                request == 0x1CFF && dataLen == 4 ? PSTR("REQUEST_LIST_SIZE_OF_BUSINESS_ADDRESSES") :
                request == 0x1CFF && dataLen == 9 ? PSTR("REQUEST_BUSINESS_ADDRESSES") :
                request == 0x1D0E ? PSTR("SELECT_FASTEST_ROUTE?") :
                request == 0x1DFF ? PSTR("CHOOSE_DESTINATION_SHOW_CURRENT_ADDRESS") :
                ToHexStr(request)
*/
            );

            if (data[3] != 0x00)
            {
                char buffer[2];
                sprintf_P(buffer, PSTR("%c"), data[3]);
                SERIAL.printf_P(
                    PSTR(", letter=%s"),
                    (data[3] >= 'A' && data[3] <= 'Z') || (data[3] >= '0' && data[3] <= '9') || data[3] == '\'' ? buffer :
                    data[3] == ' ' ? "_" : // Space
                    data[3] == 0x01 ? "Esc" :
                    "?"
                );
            } // if

            if (dataLen >= 9)
            {
                uint16_t selectionOrOffset = (uint16_t)data[5] << 8 | data[6];
                uint16_t length = (uint16_t)data[7] << 8 | data[8];

                //if (selectionOrOffset > 0 && length > 0)
                if (length > 0)
                {
                    SERIAL.printf_P(PSTR(", offset=%u, length=%u"), selectionOrOffset, length);
                }
                //else if (selectionOrOffset > 0)
                else
                {
                    SERIAL.printf_P(PSTR(", selection=%u"), selectionOrOffset);
                } // if
            } // if

            SERIAL.println();
        }
        break;

        case SATNAV_TO_MFD_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#74E
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#74E

            SERIAL.print(F("--> SatNav to MFD: "));

            if (dataLen != 27)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            // Possible meanings of data:

            // data[0] & 0x07: sequence number
            // data[1]: request code
            // data[4] << 8 | data[5]: number of items
            // data[17...22]: bits indicating available letters, numbers, single quote (') or space

            SERIAL.printf_P(
                PSTR("response=%S; list_size=%u, "),
                SatNavRequestStr(data[1]),
                (uint16_t)data[4] << 8 | data[5]
            );

            // Available letters are bit-coded in bytes 17...20. Print the letter if it is available, print a '.'
            // if not.
            for (int byte = 0; byte <= 3; byte++)
            {
                for (int bit = 0; bit < (byte == 3 ? 2 : 8); bit++)
                {
                    SERIAL.printf_P(PSTR("%c"), data[byte + 17] >> bit & 0x01 ? 65 + 8 * byte + bit : '.');
                } // for
            } // for

            // Special character '
            SERIAL.printf_P(PSTR("%c"), data[21] >> 6 & 0x01 ? '\'' : '.');

            // Available numbers are bit-coded in bytes 20...21, starting with '0' at bit 2 of byte 20, ending
            // with '9' at bit 3 of byte 21. Print the number if it is available, print a '.' if not.
            for (int byte = 0; byte <= 1; byte++)
            {
                for (int bit = (byte == 0 ? 2 : 0); bit < (byte == 1 ? 3 : 8); bit++)
                {
                    SERIAL.printf_P(PSTR("%c"), data[byte + 20] >> bit & 0x01 ? 48 + 8 * byte + bit - 2 : '.');
                } // for
            } // for

            // <Space>, printed here as '_'
            SERIAL.printf_P(PSTR("%c"), data[22] >> 1 & 0x01 ? '_' : '.');

            SERIAL.println();
        }
        break;

        case SATNAV_DOWNLOADING_IDEN:
        {
            // I think this is just a message from the SatNav system that it is processing a new navigation disc.
            // MFD shows "DOWNLOADING".

            // Examples:
            // Raw: #1593 ( 3/15)  5 0E 6F4 RA1 3A-3E NO_ACK OK 3A3E CRC_OK

            SERIAL.print(F("--> SatNav is DOWNLOADING: "));

            if (dataLen != 0)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            SERIAL.println();
        }
        break;

        case SATNAV_DOWNLOADED1_IDEN:
        {
            // SatNav system somehow indicating that it is finished "DOWNLOADING".
            // Sequence of messages is the same for different discs.

            // Examples:
            // Raw: #2894 (14/15)  7 0E A44 WA0 21-80-74-A4 ACK OK 74A4 CRC_OK
            // Raw: #2932 ( 7/15)  6 0E A44 WA0 82-D5-86 ACK OK D586 CRC_OK

            SERIAL.print(F("--> SatNav DOWNLOADING finished 1: "));

            if (dataLen != 1 && dataLen != 2)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            SERIAL.println();
        }
        break;

        case SATNAV_DOWNLOADED2_IDEN:
        {
            // SatNav system somehow indicating that it is finished "DOWNLOADING".
            // Sequence of messages is the same for different discs.

            // Examples:
            // Raw: #2896 ( 1/15)  5 0E AC4 RA1 C5-D6 NO_ACK OK C5D6 CRC_OK
            // Raw: #2897 ( 2/15)  5 0E AC4 RA1 C5-D6 NO_ACK OK C5D6 CRC_OK
            // Raw: #2898 ( 3/15) 31 0E AC4 RA0 89-61-80-00-00-00-00-00-00-00-00-00-00-00-00-00-00-00-00-00-00-00-00-00-00-89-ED-C0 ACK OK EDC0 CRC_OK
            // Raw: #2933 ( 8/15)  8 0E AC4 RA0 8A-C2-8A-21-04 ACK OK 2104 CRC_OK

            SERIAL.print(F("--> SatNav DOWNLOADING finished 2: "));

            if (dataLen != 0 && dataLen != 3 && dataLen != 26)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            SERIAL.println();
        }
        break;

        case WHEEL_SPEED_IDEN:
        {
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#744

            SERIAL.print(F("--> Wheel speed: "));

            if (dataLen != 5)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            char floatBuf[2][MAX_FLOAT_SIZE];
            SERIAL.printf_P(
                PSTR("rear right=%s km/h, rear left=%s km/h, rear right pulses=%u, rear left pulses=%u\n"),
                FloatToStr(floatBuf[0], ((uint16_t)data[0] << 8 | data[1]) / 100.0, 2),
                FloatToStr(floatBuf[1], ((uint16_t)data[2] << 8 | data[3]) / 100.0, 2),
                (uint16_t)data[4] << 8 | data[5],
                (uint16_t)data[6] << 8 | data[7]
            );
        }
        break;

        case ODOMETER_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#8FC
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#8FC

            // Print only if not duplicate of previous packet
            static uint8_t packetData[VAN_MAX_DATA_BYTES] = "";  // Previous packet data
            if (memcmp(data, packetData, dataLen) == 0) return VAN_PACKET_DUPLICATE;
            memcpy(packetData, data, dataLen);

            SERIAL.print(F("--> Odometer: "));

            if (dataLen != 5)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            char floatBuf[MAX_FLOAT_SIZE];
            SERIAL.printf_P(
                PSTR("kms=%s\n"),
                FloatToStr(floatBuf, ((uint32_t)data[1] << 16 | (uint32_t)data[2] << 8 | data[3]) / 10.0, 1)
            );
        }
        break;

        case COM2000_IDEN:
        {
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#450

            SERIAL.print(F("--> COM2000: "));

            if (dataLen != 10)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            SERIAL.printf_P(PSTR("\nLight switch: %S%S%S%S%S%S%S%S\n"),
                data[1] & 0x02 ? PSTR("Fog light switch turned FORWARD, ") : emptyStr,
                data[1] & 0x01 ? PSTR("Auto light button pressed, ") : emptyStr,
                data[1] & 0x04 ? PSTR("Fog light switch turned BACKWARD, ") : emptyStr,
                data[1] & 0x08 ? PSTR("Main beam handle gently ON, ") : emptyStr,
                data[1] & 0x10 ? PSTR("Main beam handle fully ON, ") : emptyStr,
                data[1] & 0x20 ? PSTR("All OFF, ") : emptyStr,
                data[1] & 0x40 ? PSTR("Sidelights ON, ") : emptyStr,
                data[1] & 0x80 ? PSTR("Low beam ON, ") : emptyStr
            );

            SERIAL.printf_P(PSTR("Right stalk: %S%S%S%S%S%S%S%S\n"),
                data[2] & 0x01 ? PSTR("Trip computer button pressed, ") : emptyStr,
                data[2] & 0x02 ? PSTR("Rear wiper switched turned to screen wash position, ") : emptyStr,
                data[2] & 0x04 ? PSTR("Rear wiper switched turned to position 1, ") : emptyStr,
                data[2] & 0x08 ? PSTR("Screen wash, ") : emptyStr,
                data[2] & 0x10 ? PSTR("Single screen wipe, ") : emptyStr,
                data[2] & 0x20 ? PSTR("Screen wipe speed 1, ") : emptyStr,
                data[2] & 0x40 ? PSTR("Screen wipe speed 2, ") : emptyStr,
                data[2] & 0x80 ? PSTR("Screen wipe speed 3, ") : emptyStr
            );

            SERIAL.printf_P(PSTR("Turn signal indicator: %S%S\n"),
                data[3] & 0x40 ? PSTR("Left signal ON, ") : emptyStr,
                data[3] & 0x80 ? PSTR("Right signal ON, ") : emptyStr
            );

            SERIAL.printf_P(PSTR("Head unit stalk: %S%S%S%S%S\n"),
                data[5] & 0x02 ? PSTR("SRC button pressed, ") : emptyStr,
                data[5] & 0x03 ? PSTR("Volume down button pressed, ") : emptyStr,
                data[5] & 0x08 ? PSTR("Volume up button pressed, ") : emptyStr,
                data[5] & 0x40 ? PSTR("Seek backward button pressed, ") : emptyStr,
                data[5] & 0x80 ? PSTR("Seek forward button pressed, ") : emptyStr
            );

            SERIAL.printf_P(PSTR("Head unit stalk wheel position: %d\n"),
                (sint8_t)data[6]);
        }
        break;

        case CDCHANGER_COMMAND_IDEN:
        {
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#8EC

            // Examples:
            // 0E8ECC 1181 30F0

            // Print only if not duplicate of previous packet
            static uint8_t packetData[VAN_MAX_DATA_BYTES] = "";  // Previous packet data
            if (memcmp(data, packetData, dataLen) == 0) return VAN_PACKET_DUPLICATE;
            memcpy(packetData, data, dataLen);

            SERIAL.print(F("--> CD changer command: "));

            if (dataLen != 2)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            uint16_t cdcCommand = (uint16_t)data[0] << 8 | data[1];

            SERIAL.printf_P(PSTR("%S\n"),
                cdcCommand == 0x1101 ? PSTR("POWER_OFF") :
                cdcCommand == 0x2101 ? PSTR("POWER_OFF") :
                cdcCommand == 0x1181 ? PSTR("PAUSE") :
                cdcCommand == 0x1183 ? PSTR("PLAY") :
                cdcCommand == 0x31FE ? PSTR("PREVIOUS_TRACK") :
                cdcCommand == 0x31FF ? PSTR("NEXT_TRACK") :
                cdcCommand == 0x4101 ? PSTR("CD_1") :
                cdcCommand == 0x4102 ? PSTR("CD_2") :
                cdcCommand == 0x4103 ? PSTR("CD_3") :
                cdcCommand == 0x4104 ? PSTR("CD_4") :
                cdcCommand == 0x4105 ? PSTR("CD_5") :
                cdcCommand == 0x4106 ? PSTR("CD_6") :
                cdcCommand == 0x41FE ? PSTR("PREVIOUS_CD") :
                cdcCommand == 0x41FF ? PSTR("NEXT_CD") :
                ToHexStr(cdcCommand)
            );
        }
        break;

        case MFD_TO_HEAD_UNIT_IDEN:
        {
            // http://graham.auld.me.uk/projects/vanbus/packets.html#8D4
            // http://pinterpeti.hu/psavanbus/PSA-VAN.html#8D4

            // Print only if not duplicate of previous packet
            static uint8_t packetData[VAN_MAX_DATA_BYTES] = "";  // Previous packet data
            if (memcmp(data, packetData, dataLen) == 0) return VAN_PACKET_DUPLICATE;
            memcpy(packetData, data, dataLen);

            // Maybe this is in fact "Head unit to MFD"??
            SERIAL.print(F("--> MFD to head unit: "));

            if (data[0] == 0x11)
            {
                if (dataLen != 2)
                {
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                SERIAL.printf_P(
                    PSTR(
                        "command=HEAD_UNIT_UPDATE_AUDIO_BITS, mute=%S, auto_volume=%S, loudness=%S, audio_menu=%S,\n"
                        "    power=%S, contact_key=%S\n"
                    ),
                    data[1] & 0x01 ? onStr : offStr,
                    data[1] & 0x02 ? onStr : offStr,
                    data[1] & 0x10 ? onStr : offStr,

                    // Bug: if CD changer is playing, this one is always "OPEN"...
                    data[1] & 0x20 ? PSTR("OPEN") : PSTR("CLOSED"),

                    data[1] & 0x40 ? onStr : offStr,
                    data[1] & 0x80 ? onStr : offStr
                );
            }
            else if (data[0] == 0x12)
            {
                if (dataLen != 2 && dataLen != 11)
                {
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                SERIAL.printf_P(PSTR("command=HEAD_UNIT_SWITCH_TO, param=%S\n"),
                    data[1] == 0x01 ? PSTR("TUNER") :
                    data[1] == 0x02 ? PSTR("INTERNAL_CD_OR_TAPE") :
                    data[1] == 0x03 ? PSTR("CD_CHANGER") :

                    // This is the "default" mode for the head unit, to sit there and listen to the navigation
                    // audio. The navigation audio volume is also always set (usually a lot higher than the radio)
                    // whenever this source is chosen.
                    data[1] == 0x05 ? PSTR("NAVIGATION_AUDIO") :

                    ToHexStr(data[1])
                );

                if (dataLen == 11)
                {
                    SERIAL.printf_P(
                        PSTR(
                            "    power=%S, source=%S,\n"
                            "    volume=%u%S, balance=%d%S, fader=%d%S, bass=%d%S, treble=%d%S\n"
                        ),
                        data[2] & 0x01 ? onStr : offStr,

                        (data[4] & 0x0F) == 0x00 ? noneStr :  // source
                        (data[4] & 0x0F) == 0x01 ? PSTR("TUNER") :
                        (data[4] & 0x0F) == 0x02 ? PSTR("INTERNAL_CD_OR_TAPE") :
                            // TODO - is this applicable:
                            // data[4] & 0x20 ? "TAPE" : 
                            // data[4] & 0x40 ? "INTERNAL_CD" : 
                        (data[4] & 0x0F) == 0x03 ? PSTR("CD_CHANGER") :

                        // This is the "default" mode for the head unit, to sit there and listen to the navigation
                        // audio. The navigation audio volume is also always set (usually a lot higher than the radio)
                        // whenever this source is chosen.
                        (data[4] & 0x0F) == 0x05 ? PSTR("NAVIGATION_AUDIO") :

                        ToHexStr((uint8_t)(data[4] & 0x0F)),

                        data[5] & 0x7F,
                        data[5] & 0x80 ? updatedStr : emptyStr,
                        (sint8_t)(0x3F) - (data[6] & 0x7F),
                        data[6] & 0x80 ? updatedStr : emptyStr,
                        (sint8_t)(0x3F) - (data[7] & 0x7F),
                        data[7] & 0x80 ? updatedStr : emptyStr,
                        (sint8_t)(data[8] & 0x7F) - 0x3F,
                        data[8] & 0x80 ? updatedStr : emptyStr,
                        (sint8_t)(data[9] & 0x7F) - 0x3F,
                        data[9] & 0x80 ? updatedStr : emptyStr
                    );
                } // if
            }
            else if (data[0] == 0x13)
            {
                if (dataLen != 2)
                {
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                SERIAL.printf_P(PSTR("command=HEAD_UNIT_UPDATE_VOLUME, param=%u(%S%S)\n"),
                    data[1] & 0x1F,
                    data[1] & 0x40 ? PSTR("relative: ") : PSTR("absolute"),
                    data[1] & 0x40 ?
                        data[1] & 0x20 ? PSTR("decrease") : PSTR("increase") :
                        emptyStr
                );
            }
            else if (data[0] == 0x14)
            {
                // Seen when the audio popup is on the MFD and a level is changed, and when the audio popup on
                // the MFD disappears.

                // Examples:
                // Raw: #5848 ( 3/15) 10 0E 8D4 WA0 14-BF-3F-43-43-51-D6 ACK OK 51D6 CRC_OK
                // Raw: #6031 (11/15) 10 0E 8D4 WA0 14-BF-3F-45-43-60-84 ACK OK 6084 CRC_OK
                // Raw: #8926 (11/15) 10 0E 8D4 WA0 14-BF-3F-46-43-F7-B0 ACK OK F7B0 CRC_OK

                if (dataLen != 5)
                {
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                // TODO - bit 7 of data[1] is always 1 ?

                SERIAL.printf_P(PSTR("command=HEAD_UNIT_UPDATE_AUDIO_LEVELS, balance=%d, fader=%d, bass=%d, treble=%d\n"),
                    (sint8_t)(0x3F) - (data[1] & 0x7F),
                    (sint8_t)(0x3F) - data[2],
                    (sint8_t)data[3] - 0x3F,
                    (sint8_t)data[4] - 0x3F
                );
            }
            else if (data[0] == 0x24)
            {
                if (dataLen != 2)
                {
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                // TODO - Not sure what this is. Seen just before a trafic announcement was issued by the tuner.

                SERIAL.printf_P(PSTR("traffic_announcement=%s\n"), ToHexStr(data[1]));
            }
            else if (data[0] == 0x27)
            {
                if (dataLen != 2)
                {
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                SERIAL.printf_P(PSTR("preset_request band=%S, preset=%u\n"),
                    TunerBandStr(data[1] >> 4 & 0x07),
                    data[1] & 0x0F
                );
            }
            else if (data[0] == 0x61)
            {
                if (dataLen != 4)
                {
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                SERIAL.printf_P(PSTR("command=REQUEST_CD, param=%S\n"),
                    data[1] == 0x02 ? PSTR("PAUSE") :
                    data[1] == 0x03 ? PSTR("PLAY") :
                    data[3] == 0xFF ? PSTR("NEXT") :
                    data[3] == 0xFE ? PSTR("PREVIOUS") :
                    ToHexStr(data[3])
                );
            }
            else if (data[0] == 0xD1)
            {
                if (dataLen != 1)
                {
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                SERIAL.print(F("command=REQUEST_TUNER_INFO\n"));
            }
            else if (data[0] == 0xD2)
            {
                if (dataLen != 1)
                {
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                SERIAL.print(F("command=REQUEST_TAPE_INFO\n"));
            }
            else if (data[0] == 0xD6)
            {
                if (dataLen != 1)
                {
                    SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                    return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
                } // if

                SERIAL.print(F("command=REQUEST_CD_TRACK_INFO\n"));
            }
            else
            {
                SERIAL.printf("%s [to be decoded]\n", ToHexStr(data[0]));

                return VAN_PACKET_PARSE_TO_BE_DECODED;
            } // if
        }
        break;

        case AIR_CONDITIONER_DIAG_IDEN:
        {
            SERIAL.print(F("--> Aircon diag: "));
            SERIAL.println(FPSTR(toBeDecodedStr));
            return VAN_PACKET_PARSE_TO_BE_DECODED;
        }
        break;

        case AIR_CONDITIONER_DIAG_COMMAND_IDEN:
        {
            SERIAL.print(F("--> Aircon diag command: "));
            SERIAL.println(FPSTR(toBeDecodedStr));
            return VAN_PACKET_PARSE_TO_BE_DECODED;
        }
        break;

        case ECU_IDEN:
        {
            SERIAL.print(F("--> ECU status(?): "));

            if (dataLen != 15)
            {
                SERIAL.println(FPSTR(unexpectedPacketLengthStr));
                return VAN_PACKET_PARSE_UNEXPECTED_LENGTH;
            } // if

            // No idea who is sending this packet (engine ECU?) and what this packet means.

            // Examples:
            //
            // Raw: #3857 (12/15) 20 0E B0E W-0 01-1D-6E-B0-0B-35-5A-3C-79-E3-1D-F7-00-00-00-F6-BE NO_ACK OK F6BE CRC_OK
            // Raw: #3918 (13/15) 20 0E B0E W-0 01-1D-6E-B0-0B-35-5A-3C-79-E3-1D-FA-00-00-02-D1-5A NO_ACK OK D15A CRC_OK
            // Raw: #3977 (12/15) 20 0E B0E W-0 01-1D-6E-B0-0B-35-5A-3C-79-E3-1D-FA-00-00-00-EF-2E NO_ACK OK EF2E CRC_OK
            // Raw: #4038 (13/15) 20 0E B0E W-0 01-1D-6E-B0-0B-35-5A-3C-79-E3-1D-FB-00-00-02-9A-EC NO_ACK OK 9AEC CRC_OK
            // Raw: #4099 (14/15) 20 0E B0E W-0 01-1D-6E-B0-0B-35-5A-3C-79-E3-1E-01-00-00-01-40-E8 NO_ACK OK 40E8 CRC_OK
            // Raw: #4160 ( 0/15) 20 0E B0E W-0 01-1D-6E-B0-0B-35-5A-3C-79-E3-1E-02-00-00-01-9C-32 NO_ACK OK 9C32 CRC_OK
            // Raw: #4221 ( 1/15) 20 0E B0E W-0 01-1D-6E-B0-0B-35-5A-3C-79-E3-1E-02-00-00-00-83-08 NO_ACK OK 8308 CRC_OK
            //
            // Raw: #8351 ( 1/15) 20 0E B0E W-0 01-1D-6E-C2-0B-35-5A-3C-79-E3-1E-FA-00-00-01-28-DE NO_ACK OK 28DE CRC_OK
            // Raw: #8470 ( 0/15) 20 0E B0E W-0 01-1D-6E-C2-0B-35-5A-3C-79-E3-1E-FB-00-00-01-63-68 NO_ACK OK 6368 CRC_OK
            // Raw: #8527 (12/15) 20 0E B0E W-0 01-1D-6E-C2-0B-35-5A-3C-79-E3-20-01-00-00-01-82-48 NO_ACK OK 8248 CRC_OK --> Skipping 1F ??
            // Raw: #8586 (11/15) 20 0E B0E W-0 01-1D-6E-C2-0B-35-5A-3C-79-E3-20-02-00-00-01-5E-92 NO_ACK OK 5E92 CRC_OK
            // Raw: #8645 (10/15) 20 0E B0E W-0 01-1D-6E-C2-0B-35-5A-3C-79-E3-20-03-00-00-01-15-24 NO_ACK OK 1524 CRC_OK
            // Raw: #8763 ( 8/15) 20 0E B0E W-0 01-1D-6E-C2-0B-35-5A-3C-79-E3-20-05-00-00-01-B3-AA NO_ACK OK B3AA CRC_OK
            //
            // Raw: #1741 (11/15) 20 0E B0E W-0 01-48-E5-02-0A-B3-E3-3B-79-FD-35-F7-00-00-01-5A-1C NO_ACK OK 5A1C CRC_OK
            // Raw: #1876 (11/15) 20 0E B0E W-0 01-48-E5-02-0A-B3-E3-3B-79-FD-35-F9-00-00-01-9F-56 NO_ACK OK 9F56 CRC_OK
            // Raw: #2010 (10/15) 20 0E B0E W-0 01-48-E5-02-0A-B3-E3-3B-79-FD-35-FA-00-00-01-43-8C NO_ACK OK 438C CRC_OK
            // Raw: #2079 ( 4/15) 20 0E B0E W-0 01-48-E5-02-0A-B3-E3-3B-79-FD-36-00-00-00-02-99-88 NO_ACK OK 9988 CRC_OK
            // Raw: #2150 ( 0/15) 20 0E B0E W-0 01-48-E5-02-0A-B3-E3-3B-79-FD-36-02-00-00-01-2F-AA NO_ACK OK 2FAA CRC_OK

            // data[9] << 16 | data[10] << 8 | data[11]: Counter incrementing by 1 per second?
            //     Counting is regardless of engine running or not.

            SERIAL.printf_P(PSTR("counter=%lu\n"),
                (uint32_t)data[9] << 16 | (uint32_t)data[10] << 8 | data[11]
            );

            return VAN_PACKET_PARSE_TO_BE_DECODED;
        }
        break;

        default:
        {
            return VAN_PACKET_PARSE_UNRECOGNIZED_IDEN;
        }
        break;
    } // switch

    return VAN_PACKET_PARSE_OK;
} // ParseVanPacket

void setup()
{
    delay(1000);
    SERIAL.begin(115200);
    SERIAL.println(F("Starting VAN bus packet parser"));

    // Disable Wi-Fi altogether to get rid of long and variable interrupt latency, causing packet CRC errors
    // From: https://esp8266hints.wordpress.com/2017/06/29/save-power-by-reliably-switching-the-esp-wifi-on-and-off/
    WiFi.disconnect(true);
    delay(1); 
    WiFi.mode(WIFI_OFF);
    delay(1);
    WiFi.forceSleepBegin();
    delay(1);

    VanBusRx.Setup(RX_PIN);
    Serial.printf_P(PSTR("VanBusRx queue of size %d is set up\n"), VanBusRx.QueueSize());
} // setup

enum VanPacketFilter_t
{
    VAN_PACKETS_ALL_EXCEPT,
    VAN_PACKETS_NONE_EXCEPT,
    VAN_PACKETS_HEAD_UNIT,
    VAN_PACKETS_AIRCON,
    VAN_PACKETS_SAT_NAV
}; // enum VanPacketParseResult_t

// Defined in PacketFilter.ino
bool isPacketSelected(uint16_t iden, VanPacketFilter_t filter);

void loop()
{
    int n = 0;
    while (VanBusRx.Available())
    {
        TVanPacketRxDesc pkt;
        bool isQueueOverrun = false;
        VanBusRx.Receive(pkt, &isQueueOverrun);

        if (isQueueOverrun) SERIAL.print(F("QUEUE OVERRUN!\n"));

        pkt.CheckCrcAndRepair();

        // Filter on specific IDENs
        uint16_t iden = pkt.Iden();
        if (! isPacketSelected(iden, VAN_PACKETS_ALL_EXCEPT)) continue;

        // Show packet as parsed by ISR
        VanPacketParseResult_t parseResult = ParseVanPacket(&pkt);

        // Show byte content only for packets that are not a duplicate of a previously received packet
        if (parseResult != VAN_PACKET_DUPLICATE) pkt.DumpRaw(SERIAL);

        // Process at most 30 packets at a time
        if (++n >= 30) break;
    } // while

    // Print statistics every 5 seconds
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate >= 5000UL) // Arithmetic has safe roll-over
    {
        lastUpdate = millis();
        VanBusRx.DumpStats(SERIAL);
    } // if
} // loop
