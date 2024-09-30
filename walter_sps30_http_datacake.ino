/**
 * @file walter_sps30_http_datacake.ino
 * @author Daan Pape <daan@dptechnics.com>
 * @date Sep 2024
 * @copyright DPTechnics bv
 * @brief SPS30 + HTTP Datacake example
 *
 * @section LICENSE
 *
 * Copyright (C) 2023, DPTechnics bv
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 * 
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 * 
 *   3. Neither the name of DPTechnics bv nor the names of its contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 * 
 *   4. This software, with or without modification, may only be used with a
 *      Walter board from DPTechnics bv.
 * 
 *   5. Any software provided in binary form under this license must not be
 *      reverse engineered, decompiled, modified and/or disassembled.
 * 
 * THIS SOFTWARE IS PROVIDED BY DPTECHNICS BV “AS IS” AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL DPTECHNICS BV OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @section DESCRIPTION
 * 
 * This sketch reads out an SPS30 particulate sensor over UART. The result
 * is then sent to Datacake using the HTTP endpoint.
 */

#include <esp_mac.h>
#include <WalterModem.h>
#include <HardwareSerial.h>

#include "sps30.h"
#include "sensirion_uart.h"

/**
 * @brief HTTP profile
 */
#define HTTP_PROFILE 1

/**
 * @brief TLS profile
 */
#define TLS_PROFILE 1

/**
 * @brief The size of the outgoing JSON buffer.
 */
#define JSON_BUFSIZE 128

/**
 * @brief The modem instance.
 */
WalterModem modem;

/**
 * @brief The buffer to transmit to the UDP server. The first 6 bytes will be
 * the MAC address of the Walter this code is running on.
 */

/**
 * @brief The buffer used to format the outgoing JSON messages in.
 */
uint8_t outBuf[JSON_BUFSIZE] = { 0 };

/**
 * @brief Buffer for incoming HTTP response
 */
uint8_t inBuf[256] = { 0 };

void setup() {
  Serial.begin(115200);
  delay(5000);

  Serial.print("Walter SPS30 particulate sensor for Datacake V0.0.1\r\n");

  /* Get the MAC address for board validation */
  esp_read_mac(outBuf, ESP_MAC_WIFI_STA);
  Serial.printf("Walter's MAC is: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
    outBuf[0],
    outBuf[1],
    outBuf[2],
    outBuf[3],
    outBuf[4],
    outBuf[5]);

  if(WalterModem::begin(&Serial1)) {
    Serial.print("Modem initialization OK\r\n");
  } else {
    Serial.print("Modem initialization ERROR\r\n");
    return;
  }

  if(modem.checkComm()) {
    Serial.print("Modem communication is ok\r\n");
  } else {
    Serial.print("Modem communication error\r\n");
    return;
  }

  /* Detect the SPS30 sensor and start measurements */
  sensirion_uart_open();
  while (sps30_probe() != 0) {
    Serial.println("Could not detect SPS30 particulate sensor");
    delay(1000);
  }
  Serial.println("SPS30 sensor initialized");

  if(sps30_start_measurement() != 0) {
    Serial.println("error starting measurement");
  }
  Serial.println("Started particulate matter measurements");

  WalterModemRsp rsp = {};
  if(modem.getOpState(&rsp)) {
    Serial.printf("Modem operational state: %d\r\n", rsp.data.opState);
  } else {
    Serial.print("Could not retrieve modem operational state\r\n");
    return;
  }

  if(modem.getRadioBands(&rsp)) {
    Serial.print("Modem is configured for the following bands:\r\n");
    
    for(int i = 0; i < rsp.data.bandSelCfgSet.count; ++i) {
      WalterModemBandSelection *bSel = rsp.data.bandSelCfgSet.config + i;
      Serial.printf("  - Operator '%s' on %s: 0x%05X\r\n",
        bSel->netOperator.name,
        bSel->rat == WALTER_MODEM_RAT_NBIOT ? "NB-IoT" : "LTE-M",
        bSel->bands);
    }
  } else {
    Serial.print("Could not retrieve configured radio bands\r\n");
    return;
  }

  if(modem.setOpState(WALTER_MODEM_OPSTATE_NO_RF)) {
    Serial.print("Successfully set operational state to NO RF\r\n");
  } else {
    Serial.print("Could not set operational state to NO RF\r\n");
    return;
  }

  /* Give the modem time to detect the SIM */
  delay(2000);

  if(modem.unlockSIM()) {
    Serial.print("Successfully unlocked SIM card\r\n");
  } else {
    Serial.print("Could not unlock SIM card\r\n");
    return;
  }

  /* Create PDP context */
  if(modem.createPDPContext("soracom.io", WALTER_MODEM_PDP_AUTH_PROTO_PAP, "sora", "sora"))
  {
    Serial.print("Created PDP context\r\n");
  } else {
    Serial.print("Could not create PDP context\r\n");
    return;
  }

  /* Authenticate the PDP context */
  if(modem.authenticatePDPContext()) {
    Serial.print("Authenticated the PDP context\r\n");
  } else {
    Serial.print("Could not authenticate the PDP context\r\n");
    return;
  }

  /* Set the operational state to full */
  if(modem.setOpState(WALTER_MODEM_OPSTATE_FULL)) {
    Serial.print("Successfully set operational state to FULL\r\n");
  } else {
    Serial.print("Could not set operational state to FULL\r\n");
    return;
  }

  /* Set the network operator selection to automatic */
  if(modem.setNetworkSelectionMode(WALTER_MODEM_NETWORK_SEL_MODE_AUTOMATIC)) {
    Serial.print("Network selection mode to was set to automatic\r\n");
  } else {
    Serial.print("Could not set the network selection mode to automatic\r\n");
    return;
  }

  /* Wait for the network to become available */
  WalterModemNetworkRegState regState = modem.getNetworkRegState();
  while(!(regState == WALTER_MODEM_NETWORK_REG_REGISTERED_HOME ||
          regState == WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING))
  {
    delay(100);
    regState = modem.getNetworkRegState();
  }
  Serial.print("Connected to the network\r\n");

  /* Activate the PDP context */
  if(modem.setPDPContextActive(true)) {
    Serial.print("Activated the PDP context\r\n");
  } else {
    Serial.print("Could not activate the PDP context\r\n");
    return;
  }

  /* Attach the PDP context */
  if(modem.attachPDPContext(true)) {
    Serial.print("Attached to the PDP context\r\n");
  } else {
    Serial.print("Could not attach to the PDP context\r\n");
    return;
  }

  if(modem.getPDPAddress(&rsp)) {
    Serial.print("PDP context address list:\r\n");
    Serial.printf("  - %s\r\n", rsp.data.pdpAddressList.pdpAddress);
    if(rsp.data.pdpAddressList.pdpAddress2[0] != '\0') {
      Serial.printf("  - %s\r\n", rsp.data.pdpAddressList.pdpAddress2);
    }
  } else {
    Serial.print("Could not retrieve PDP context addresses\r\n");
    return;
  }

  /* Configure TLS profile */
  if(modem.tlsConfigProfile(TLS_PROFILE)) {
    Serial.print("Successfully configured the TLS profile\r\n");
  } else {
    Serial.print("Failed to configure TLS profile\r\n");
  }

  /* Configure http profile for a simple test */
  if(modem.httpConfigProfile(HTTP_PROFILE, "api.datacake.co", 443, 1)) {
    Serial.print("Successfully configured the HTTP profile\r\n");
  } else {
    Serial.print("Failed to configure HTTP profile\r\n");
  }
}

void loop() {
  struct sps30_measurement measurement = { 0 };
  int16_t ret = sps30_read_measurement(&measurement);
  while(ret < 0) {
    Serial.println("Could not read SPS30 measurement");
    delay(1000);
  }

  if(SPS30_IS_ERR_STATE(ret)) {
    Serial.print("SPS30 state: ");
    Serial.print(SPS30_GET_ERR_STATE(ret), DEC);
    Serial.println(" - measurements may not be accurate");
  }

  uint16_t dataSize = snprintf(
    (char*) outBuf,
    JSON_BUFSIZE,
    "{\"device\":\"68:B6:B3:46:50:34\",\"PM1_0\":%.2f,\"PM2_5\":%.2f,\"PM4_0\":%.2f,\"PM10\":%.2f}",
    measurement.mc_1p0,
    measurement.mc_2p5,
    measurement.mc_4p0,
    measurement.mc_10p0);

  Serial.printf("%s\n",outBuf);

  WalterModemRsp rsp = {};

  /* HTTP test */
  static short httpReceiveAttemptsLeft = 0;
  static char ctbuf[32];

  if(!httpReceiveAttemptsLeft) {
    if(modem.httpSend(
        HTTP_PROFILE,
        "/integrations/api/e53a541f-df58-4064-bc22-742365902b63/",
        outBuf,
        dataSize,
        WALTER_MODEM_HTTP_SEND_CMD_POST,
        WALTER_MODEM_HTTP_POST_PARAM_JSON,
        ctbuf,
        sizeof(ctbuf)))
      {
      Serial.print("http query performed\r\n");
      httpReceiveAttemptsLeft = 3;
    } else {
      Serial.print("http query failed\r\n");
      delay(1000);
    }
  } else {
    httpReceiveAttemptsLeft--;

    if(modem.httpDidRing(HTTP_PROFILE, inBuf, sizeof(inBuf), &rsp)) {
      httpReceiveAttemptsLeft = 0;

      Serial.printf("http status code: %d\r\n", rsp.data.httpResponse.httpStatus);
      Serial.printf("content type: %s\r\n", ctbuf);
      Serial.printf("[%s]\r\n", inBuf);
    } else {
      Serial.print("HTTP response not yet received\r\n");
    }
  }

  delay(300000);
}