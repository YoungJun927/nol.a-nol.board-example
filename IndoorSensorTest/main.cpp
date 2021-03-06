#include <cox.h>
#include "PMS3003.hpp"
#include "RHT03.hpp"
#include "SparkFunCCS811.hpp"
#include "LoRaMacKR920.hpp"

PMS3003 pms3003 = PMS3003(Serial2, D3, D4);
CCS811 mysensor(Wire, 0x5B);
RHT03 rht;

#define LORAWAN_SKT 0
#if (LORAWAN_SKT == 1)
#include "LoRaMacKR920SKT.hpp"
LoRaMacKR920SKT LoRaWAN = LoRaMacKR920SKT(SX1276, 10);
#else
#include "LoRaMacKR920.hpp"
LoRaMacKR920 LoRaWAN = LoRaMacKR920(SX1276, 10);
#endif

Timer measurement;
Timer timerSend;

int32_t temp1_0=0;
int32_t temp10_0=0;

int CO2;
int TVOC;

float latestHumidity;
float latestTempC;
float latestTempF;

#define OVER_THE_AIR_ACTIVATION 1

#if (OVER_THE_AIR_ACTIVATION == 1)
static uint8_t devEui[]="\x14\x0C\x5B\xFF\xFF\x00\x05\x4A";
static uint8_t appKey[]="\x4e\x59\x36\xca\x46\x05\x32\xec\xe8\x50\xeb\xd0\x0b\x75\xef\x25";
static const uint8_t appEui[] = "\x00\x00\x00\x00\x00\x00\x00\x00";

char keyBuf[128];
Timer timerKeyInput;
#else

static const uint8_t NwkSKey[] = "\xa4\x88\x55\xad\xe9\xf8\xf4\x6f\xa0\x94\xb1\x98\x36\xc3\xc0\x86";
static const uint8_t AppSKey[] = "\x7a\x56\x2a\x75\xd7\xa3\xbd\x89\xa3\xde\x53\xe1\xcf\x7f\x1c\xc7";
static uint32_t DevAddr = 0x06e632e8;
#endif //OVER_THE_AIR_ACTIVATION

static void eventSensorMeasuredDone(int32_t pm1_0_CF1,
                                    int32_t pm2_5_CF1,
                                    int32_t pm10_0_CF1,
                                    int32_t pm1_0_Atmosphere,
                                    int32_t pm2_5_Atmosphere,
                                    int32_t pm10_0_Atmosphere) {
                                        temp1_0 = pm1_0_CF1;
                                        temp10_0=pm10_0_Atmosphere;
}

//! [How to send]
static void taskPeriodicSend(void *) {
  LoRaMacFrame *f = new LoRaMacFrame(255);
  if (!f) {
    printf("* Out of memory\n");
    return NULL;
  }

  f->port = 1;
  f->type = LoRaMacFrame::CONFIRMED;
  f->len = sprintf((char *) f->buf,"\"Hum\":%u.%u,\"Tem\":%u.%u,\"PM1\":%lu,\"PM10\":%lu,\"CO2\":%d,\"VOC\":%d",
                                      (uint16_t) latestHumidity,
                                      (uint16_t) round(latestHumidity * 100) % 100,
                                      (uint16_t) latestTempC,
                                      (uint16_t) round(latestTempC * 100) % 100,
                                      temp1_0 ,
                                      temp10_0,
                                      CO2,
                                      TVOC);

  error_t err = LoRaWAN.send(f);
  printf("* Sending periodic report (%s (%u byte)): %d\n", f->buf, f->len, err);
  if (err != ERROR_SUCCESS) {
    delete f;
    timerSend.startOneShot(10000);
  }

  err = LoRaWAN.requestLinkCheck();
  printf("* Request LinkCheck: %d\n", err);

  // err = LoRaWAN.requestDeviceTime();
  // printf("* Request DeviceTime: %d\n", err);
}

static void readSensor(void*){
  digitalWrite(D4,HIGH);
  int updateRet = rht.update();
  if (updateRet == 1){
    latestHumidity = rht.humidity();
    latestTempC = rht.tempC();
    latestTempF = rht.tempF();
  }
  mysensor.readAlgorithmResults();
  CO2=mysensor.getCO2();
  TVOC=mysensor.getTVOC();
  pms3003.onReadDone(eventSensorMeasuredDone);
  digitalWrite(D4,LOW);
}
//! [How to send]

#if (OVER_THE_AIR_ACTIVATION == 1)

#if (LORAWAN_SKT == 1)
//! [How to use onJoin callback for SKT]
static void eventLoRaWANJoin(
  LoRaMac &,
  bool joined,
  const uint8_t *joinedDevEui,
  const uint8_t *joinedAppEui,
  const uint8_t *joinedAppKey,
  const uint8_t *joinedNwkSKey,
  const uint8_t *joinedAppSKey,
  uint32_t joinedDevAddr,
  const RadioPacket &
) {
  if (joinedNwkSKey && joinedAppSKey) {
    /* RealAppKey Joining */
    if (joined) {
      Serial.println("* RealAppKey joining done!");
      postTask(taskPeriodicSend, NULL);
    } else {
      Serial.println("* RealAppKey joining failed. Retry to join.");
      LoRaWAN.beginJoining(NULL, NULL, NULL);
    }
  } else {
    /* PseudoAppKey Joining */
    if (joined) {
      Serial.println("* PseudoAppKey joining done! Let's do RealAppKey joining!");
      LoRaWAN.beginJoining(NULL, NULL, NULL);
    } else {
      Serial.println("* PseudoAppKey joining failed. Retry to join.");
      LoRaWAN.beginJoining(devEui, appEui, appKey);
    }
  }
}
//! [How to use onJoin callback for SKT]
#else
//! [How to use onJoin callback]
static void eventLoRaWANJoin(
  LoRaMac &lw,
  bool joined,
  const uint8_t *joinedDevEui,
  const uint8_t *joinedAppEui,
  const uint8_t *joinedAppKey,
  const uint8_t *joinedNwkSKey,
  const uint8_t *joinedAppSKey,
  uint32_t joinedDevAddr,
  const RadioPacket &
) {
  if (joined) {
    Serial.println("* Joining done!");
    postTask(taskPeriodicSend, NULL);
  } else {
    Serial.println("* Joining failed. Retry to join.");
    lw.beginJoining(devEui, appEui, appKey);
  }
}
//! [How to use onJoin callback]
#endif //LORAWAN_SKT
#endif //OVER_THE_AIR_ACTIVATION

//! [How to use onSendDone callback]
static void eventLoRaWANSendDone(LoRaMac &lw, LoRaMacFrame *frame) {
  Serial.printf(
    "* Send done(%d): destined for port:%u, fCnt:0x%08lX, Freq:%lu Hz, "
    "Power:%d dBm, # of Tx:%u, ",
    frame->result,
    frame->port,
    frame->fCnt,
    frame->freq,
    frame->power,
    frame->numTrials
  );

  if (frame->modulation == Radio::MOD_LORA) {
    const char *strBW[] = {
      "Unknown", "125kHz", "250kHz", "500kHz", "Unexpected value"
    };
    if (frame->meta.LoRa.bw > 3) {
      frame->meta.LoRa.bw = (Radio::LoRaBW_t) 4;
    }
    Serial.printf(
      "LoRa, SF:%u, BW:%s, ", frame->meta.LoRa.sf, strBW[frame->meta.LoRa.bw]
    );
  } else if (frame->modulation == Radio::MOD_FSK) {
    Serial.printf("FSK, ");
  } else {
    Serial.printf("Unkndown modulation, ");
  }
  if (frame->type == LoRaMacFrame::UNCONFIRMED) {
    Serial.printf("UNCONFIRMED");
  } else if (frame->type == LoRaMacFrame::CONFIRMED) {
    Serial.printf("CONFIRMED");
  } else if (frame->type == LoRaMacFrame::MULTICAST) {
    Serial.printf("MULTICAST (error)");
  } else if (frame->type == LoRaMacFrame::PROPRIETARY) {
    Serial.printf("PROPRIETARY");
  } else {
    Serial.printf("unknown type");
  }
  Serial.printf(" frame\n");

  for (uint8_t t = 0; t < 8; t++) {
    const char *strTxResult[] = {
      "not started",
      "success",
      "no ack",
      "air busy",
      "Tx timeout",
    };
    Serial.printf("- [%u] %s\n", t, strTxResult[min(frame->txResult[t], 4)]);
  }
  delete frame;

  timerSend.startOneShot(60000);
}
//! [How to use onSendDone callback]

//! [How to use onReceive callback]
static void eventLoRaWANReceive(LoRaMac &lw, const LoRaMacFrame *frame) {
  static uint32_t fCntDownPrev = 0;

  Serial.print("* Received a frame. Destined for port:");
  Serial.print(frame->port);
  Serial.print(", fCnt:0x");
  Serial.print(frame->fCnt, HEX);
  if (fCntDownPrev != 0 && fCntDownPrev == frame->fCnt) {
    Serial.print(" (duplicate)");
  }
  fCntDownPrev = frame->fCnt;
  Serial.print(", Freq:");
  Serial.print(frame->freq);
  Serial.print(" Hz, RSSI:");
  Serial.print(frame->power);
  Serial.print(" dB");

  if (frame->modulation == Radio::MOD_LORA) {
    const char *strBW[] = {
      "Unknown", "125kHz", "250kHz", "500kHz", "Unexpected value"
    };
    Serial.printf(
      ", LoRa, SF:%u, BW:%s",
      frame->meta.LoRa.sf, strBW[min(frame->meta.LoRa.bw, 4)]
    );
  } else if (frame->modulation == Radio::MOD_FSK) {
    Serial.printf(", FSK");
  } else {
    Serial.printf(", Unkndown modulation");
  }
  if (frame->type == LoRaMacFrame::UNCONFIRMED) {
    Serial.printf(", Type:UNCONFIRMED");
  } else if (frame->type == LoRaMacFrame::CONFIRMED) {
    Serial.printf(", Type:CONFIRMED");

    if (LoRaWAN.getNumPendingSendFrames() == 0) {
      // If there is no pending send frames, send an empty frame to ack.
      LoRaMacFrame *ackFrame = new LoRaMacFrame(0);
      if (ackFrame) {
        error_t err = LoRaWAN.send(ackFrame);
        if (err != ERROR_SUCCESS) {
          delete ackFrame;
        }
      }
    }

  } else if (frame->type == LoRaMacFrame::MULTICAST) {
    Serial.printf(", Type:MULTICAST");
  } else if (frame->type == LoRaMacFrame::PROPRIETARY) {
    Serial.printf(", Type:PROPRIETARY");
  } else {
    Serial.printf(", unknown type");
  }

  if (frame->len > 0) {
    Serial.println(", ");
    for (uint8_t i = 0; i < frame->len; i++) {
      Serial.printf(" %02X", frame->buf[i]);
    }
  }
  Serial.println();
}
//! [How to use onReceive callback]

//! [How to use onJoinRequested callback]
static void eventLoRaWANJoinRequested(
  LoRaMac &, uint32_t frequencyHz, const LoRaMac::DatarateParams_t &dr
) {
  printf("* JoinRequested(Frequency: %lu Hz, Modulation: ", frequencyHz);
  if (dr.mod == Radio::MOD_FSK) {
    printf("FSK\n");
  } else if (dr.mod == Radio::MOD_LORA) {
    const char *strLoRaBW[] = { "UNKNOWN", "125kHz", "250kHz", "500kHz" };
    printf("LoRa, SF:%u, BW:%s)\n", dr.param.LoRa.sf, strLoRaBW[dr.param.LoRa.bw]);
  }
}
//! [How to use onJoinRequested callback]

//! [eventLoRaWANLinkADRReqReceived]
static void eventLoRaWANLinkADRReqReceived(LoRaMac &l, const uint8_t *payload) {
  printf("* LoRaWAN LinkADRReq received: [");
  for (uint8_t i = 0; i < 4; i++) {
    printf(" %02X", payload[i]);
  }
  printf(" ]\n");
}
//! [eventLoRaWANLinkADRReqReceived]

//! [eventLoRaWANLinkADRAnsSent]
static void printChannelInformation(LoRaMac &lw) {
  //! [getChannel]
  for (uint8_t i = 0; i < lw.MaxNumChannels; i++) {
    const LoRaMac::ChannelParams_t *p = lw.getChannel(i);
    if (p) {
      printf(" - [%u] Frequency:%lu Hz\n", i, p->Frequency);
    } else {
      printf(" - [%u] disabled\n", i);
    }
  }
  //! [getChannel]

  //! [getDatarate]
  const LoRaMac::DatarateParams_t *dr = lw.getDatarate(lw.getCurrentDatarateIndex());
  printf(" - Default DR%u:", lw.getCurrentDatarateIndex());
  if (dr->mod == Radio::MOD_LORA) {
    const char *strBW[] = {
      "Unknown", "125kHz", "250kHz", "500kHz", "Unexpected value"
    };
    printf(
      "LoRa(SF%u BW:%s)\n",
      dr->param.LoRa.sf,
      strBW[min(dr->param.LoRa.bw, 4)]
    );
  } else if (dr->mod == Radio::MOD_FSK) {
    printf("FSK\n");
  } else {
    printf("Unknown modulation\n");
  }
  //! [getDatarate]

  //! [getTxPower]
  int8_t power = lw.getTxPower(lw.getCurrentTxPowerIndex());
  printf(" - Default Tx: ");
  if (power == -127) {
    printf("unexpected value\n");
  } else {
    printf("%d dBm\n", power);
  }
  //! [getTxPower]

  printf(
    " - # of repetitions of unconfirmed uplink frames: %u\n",
    lw.getNumRepetitions()
  );
}

static void eventLoRaWANLinkADRAnsSent(LoRaMac &lw, uint8_t status) {
  printf("* LoRaWAN LinkADRAns sent with status 0x%02X.\n", status);
  printChannelInformation(lw);
}
//! [eventLoRaWANLinkADRAnsSent]

//! [eventLoRaWANDutyCycleReqReceived]
static void eventLoRaWANDutyCycleReqReceived(
  LoRaMac &lw, const uint8_t *payload
) {
  printf("* LoRaWAN DutyCycleReq received: [");
  for (uint8_t i = 0; i < 1; i++) {
    printf(" %02X", payload[i]);
  }
  printf(" ]\n");
}
//! [eventLoRaWANDutyCycleReqReceived]

//! [eventLoRaWANDutyCycleAnsSent]
static void eventLoRaWANDutyCycleAnsSent(LoRaMac &lw) {
  printf(
    "* LoRaWAN DutyCycleAns sent. Current MaxDCycle is %u.\n",
    lw.getMaxDutyCycle()
  );
}
//! [eventLoRaWANDutyCycleAnsSent]

//! [eventLoRaWANRxParamSetupReqReceived]
static void eventLoRaWANRxParamSetupReqReceived(
  LoRaMac &lw,
  const uint8_t *payload
) {
  printf("* LoRaWAN RxParamSetupReq received: [");
  for (uint8_t i = 0; i < 4; i++) {
    printf(" %02X", payload[i]);
  }
  printf(" ]\n");
}
//! [eventLoRaWANRxParamSetupReqReceived]

//! [eventLoRaWANRxParamSetupAnsSent]
static void eventLoRaWANRxParamSetupAnsSent(LoRaMac &lw, uint8_t status) {
  printf(
    "* LoRaWAN RxParamSetupAns sent with status 0x%02X. "
    "Current Rx1Offset is %u, and Rx2 channel is (DR%u, %lu Hz).\n",
    status,
    lw.getRx1DrOffset(),
    lw.getRx2Datarate(),
    lw.getRx2Frequency()
  );
}
//! [eventLoRaWANRxParamSetupAnsSent]

static void eventLoRaWANDevStatusReqReceived(LoRaMac &lw) {
  printf("* LoRaWAN DevStatusReq received.\n");
}

//! [eventLoRaWANDevStatusAnsSent]
static void eventLoRaWANDevStatusAnsSent(
  LoRaMac &lw,
  uint8_t bat,
  uint8_t margin
) {
  printf("* LoRaWAN DevStatusAns sent. (");
  if (bat == 0) {
    printf("Powered by external power source. ");
  } else if (bat == 255) {
    printf("Battery level cannot be measured. ");
  } else {
    printf("Battery: %lu %%. ", map(bat, 1, 254, 0, 100));
  }

  if (bitRead(margin, 5) == 1) {
    margin |= bit(7) | bit(6);
  }

  printf(" SNR: %d)\n", (int8_t) margin);
}
//! [eventLoRaWANDevStatusAnsSent]

//! [eventLoRaWANNewChannelReqReceived]
static void eventLoRaWANNewChannelReqReceived(
  LoRaMac &lw, const uint8_t *payload
) {
  printf("* LoRaWAN NewChannelReq received [");
  for (uint8_t i = 0; i < 5; i++) {
    printf(" %02X", payload[i]);
  }
  printf(" ]\n");
}
//! [eventLoRaWANNewChannelReqReceived]

//! [eventLoRaWANNewChannelAnsSent]
static void eventLoRaWANNewChannelAnsSent(LoRaMac &lw, uint8_t status) {
  printf(
    "* LoRaWAN NewChannelAns sent with "
    "(Datarate range %s and channel frequency %s).\n",
    (bitRead(status, 1) == 1) ? "OK" : "NOT OK",
    (bitRead(status, 0) == 1) ? "OK" : "NOT OK"
  );

  for (uint8_t i = 0; i < lw.MaxNumChannels; i++) {
    const LoRaMac::ChannelParams_t *p = lw.getChannel(i);
    if (p) {
      printf(" - [%u] Frequency:%lu Hz\n", i, p->Frequency);
    } else {
      printf(" - [%u] disabled\n", i);
    }
  }
}
//! [eventLoRaWANNewChannelAnsSent]

//! [eventLoRaWANRxTimingSetupReqReceived]
static void eventLoRaWANRxTimingSetupReqReceived(
  LoRaMac &lw,
  const uint8_t *payload
) {
  printf("* LoRaWAN RxTimingSetupReq received:  [");
  for (uint8_t i = 0; i < 1; i++) {
    printf(" %02X", payload[i]);
  }
  printf(" ]\n");
}
//! [eventLoRaWANRxTimingSetupReqReceived]

//! [eventLoRaWANRxTimingSetupAnsSent]
static void eventLoRaWANRxTimingSetupAnsSent(LoRaMac &lw) {
  printf(
    "* LoRaWAN RxTimingSetupAns sent. "
    "Current Rx1 delay is %u msec, and Rx2 delay is %u msec.\n",
    lw.getRx1Delay(),
    lw.getRx2Delay()
  );
}
//! [eventLoRaWANRxTimingSetupAnsSent]

//! [How to use onLinkChecked callback]
static void eventLoRaWANLinkChecked(
  LoRaMac &lw,
  uint8_t demodMargin,
  uint8_t numGateways
) {
  printf(
    "* LoRaWAN LinkChecked. Demodulation margin: %u dB, # of gateways: %u\n",
    demodMargin, numGateways
  );
}
//! [How to use onLinkChecked callback]

//! [How to use onDeviceTimeAnswered callback]
static void eventLoRaWANDeviceTimeAnswered(
  LoRaMac &lw,
  uint32_t tSeconds,
  uint8_t tFracSeconds
) {
  struct tm tLocal, tUtc;
  System.getDateTime(tLocal);
  System.getUTC(tUtc);
  printf(
    "* LoRaWAN DeviceTime answered: (%lu + %u/256) GPS epoch.\n"
    "- Adjusted local time: %u-%u-%u %02u:%02u:%02u\n"
    "- Adjusted UTC time: %u-%u-%u %02u:%02u:%02u\n",
    tSeconds, tFracSeconds,
    tLocal.tm_year + 1900, tLocal.tm_mon + 1, tLocal.tm_mday,
    tLocal.tm_hour, tLocal.tm_min, tLocal.tm_sec,
    tUtc.tm_year + 1900, tUtc.tm_mon + 1, tUtc.tm_mday,
    tUtc.tm_hour, tUtc.tm_min, tUtc.tm_sec
  );
}
//! [How to use onDeviceTimeAnswered callback]

static void eventButtonPressed() {
  printf("* Button pressed:\n");

  LoRaMacFrame *f = new LoRaMacFrame(20);
  if (f == NULL) {
    printf("- Not enough memory\n");
    return;
  }

  if (LoRaWAN.getDeviceClass() == LoRaMac::CLASS_A) {
    printf("- Change class A to C\n");
    f->len = sprintf((char *) f->buf, "\"class\":\"C\"");
  } else {
    printf("- Change class C to A\n");
    f->len = sprintf((char *) f->buf, "\"class\":\"A\"");
  }

  f->port = 223;
  f->type = LoRaMacFrame::CONFIRMED;
  error_t err = LoRaWAN.send(f);
  printf(
    "* Sending class configuration message (%s (%u byte)): %d\n",
    f->buf, f->len, err
  );
  if (err == ERROR_SUCCESS) {
    if (LoRaWAN.getDeviceClass() == LoRaMac::CLASS_A) {
      LoRaWAN.setDeviceClass(LoRaMac::CLASS_C);
    } else {
      LoRaWAN.setDeviceClass(LoRaMac::CLASS_A);
    }
  } else {
    delete f;
  }
}

#if (OVER_THE_AIR_ACTIVATION == 1)

#if (LORAWAN_SKT == 1)
static void taskBeginJoin(void *) {
  Serial.stopListening();

#if 0
  Serial.println("* Let's start PseudoAppKey join!");
  //! [SKT PseudoAppKey joining]
  LoRaWAN.setNetworkJoined(false);
  LoRaWAN.beginJoining(devEui, appEui, appKey);
  //! [SKT PseudoAppKey joining]
#else
  Serial.println("* Let's start RealAppKey join!");
  //! [SKT RealAppKey joining]
  LoRaWAN.setNetworkJoined(true);
  LoRaWAN.beginJoining(devEui, appEui, appKey);
  //! [SKT RealAppKey joining]
#endif
}
#else
static void taskBeginJoin(void *) {
  Serial.stopListening();
  Serial.println("* Let's start join!");
  LoRaWAN.setCurrentDatarateIndex(1); //SF8
  LoRaWAN.beginJoining(devEui, appEui, appKey);
}
#endif

static void eventAppKeyInput(SerialPort &) {
  uint8_t numOctets = strlen(keyBuf);
  if (numOctets == 32) {
    numOctets /= 2;
    char strOctet[3];

    for (uint8_t j = 0; j < numOctets; j++) {
      strOctet[0] = keyBuf[2 * j];
      strOctet[1] = keyBuf[2 * j + 1];
      strOctet[2] = '\0';

      appKey[j] = strtoul(strOctet, NULL, 16);
    }

    printf("* New AppKey:");
    for (uint8_t j = 0; j < numOctets; j++) {
      printf(" %02X", appKey[j]);
    }
    printf(" (%u byte)\n", numOctets);
    ConfigMemory.write(appKey, 0, numOctets);
    postTask(taskBeginJoin, NULL);
    return;
  } else {
    printf("* HEX string length MUST be 32-byte.");
  }

  Serial.inputKeyboard(keyBuf, sizeof(keyBuf) - 1);
}

static void eventKeyInput(SerialPort &) {
  timerKeyInput.stop();
  while (Serial.available() > 0) {
    Serial.read();
  }

  Serial.onReceive(eventAppKeyInput);
  Serial.inputKeyboard(keyBuf, sizeof(keyBuf) - 1);
  Serial.println(
    "* Enter a new appKey as a hexadecimal string "
    "[ex. 00112233445566778899aabbccddeeff]"
  );
}

#endif //OVER_THE_AIR_ACTIVATION

void setup() {
  Serial.begin(115200);
  Serial.printf("\n*** [Nol.Board] LoRaWAN Class A&C Example ***\n");

  mysensor.begin();
  rht.begin(D13);
  pms3003.begin();

  measurement.onFired(readSensor,NULL);
  measurement.startPeriodic(60000);
  // Button to switch between class A and C.
  pinMode(D9, INPUT);
  attachInterrupt(D9, eventButtonPressed, FALLING);

  System.setTimeDiff(9 * 60);  // KST

  timerSend.onFired(taskPeriodicSend, NULL);

  LoRaWAN.begin();

  //! [How to set onSendDone callback]
  LoRaWAN.onSendDone(eventLoRaWANSendDone);
  //! [How to set onSendDone callback]

  //! [How to set onReceive callback]
  LoRaWAN.onReceive(eventLoRaWANReceive);
  //! [How to set onReceive callback]

  //! [How to set onJoin callback]
  LoRaWAN.onJoin(eventLoRaWANJoin);
  //! [How to set onJoin callback]

  //! [How to set onJoinRequested callback]
  LoRaWAN.onJoinRequested(eventLoRaWANJoinRequested);
  //! [How to set onJoinRequested callback]

  //! [How to set onLinkChecked callback]
  LoRaWAN.onLinkChecked(eventLoRaWANLinkChecked);
  //! [How to set onLinkChecked callback]

  //! [How to set onDeviceTimeAnswered callback]
  LoRaWAN.onDeviceTimeAnswered(eventLoRaWANDeviceTimeAnswered, &System);
  //! [How to set onDeviceTimeAnswered callback]

  LoRaWAN.onLinkADRReqReceived(eventLoRaWANLinkADRReqReceived);
  LoRaWAN.onLinkADRAnsSent(eventLoRaWANLinkADRAnsSent);
  LoRaWAN.onDutyCycleReqReceived(eventLoRaWANDutyCycleReqReceived);
  LoRaWAN.onDutyCycleAnsSent(eventLoRaWANDutyCycleAnsSent);
  LoRaWAN.onRxParamSetupReqReceived(eventLoRaWANRxParamSetupReqReceived);
  LoRaWAN.onRxParamSetupAnsSent(eventLoRaWANRxParamSetupAnsSent);
  LoRaWAN.onDevStatusReqReceived(eventLoRaWANDevStatusReqReceived);
  LoRaWAN.onDevStatusAnsSent(eventLoRaWANDevStatusAnsSent);
  LoRaWAN.onNewChannelReqReceived(eventLoRaWANNewChannelReqReceived);
  LoRaWAN.onNewChannelAnsSent(eventLoRaWANNewChannelAnsSent);
  LoRaWAN.onRxTimingSetupReqReceived(eventLoRaWANRxTimingSetupReqReceived);
  LoRaWAN.onRxTimingSetupAnsSent(eventLoRaWANRxTimingSetupAnsSent);

  LoRaWAN.setPublicNetwork(false);

  printChannelInformation(LoRaWAN);

#if (OVER_THE_AIR_ACTIVATION == 0)
  printf("ABP!\n");
  LoRaWAN.setABP(NwkSKey, AppSKey, DevAddr);
  LoRaWAN.setNetworkJoined(true);

  postTask(taskPeriodicSend, NULL);
#else
  // System.getEUI(devEui);
  Serial.printf(
    "* DevEUI: %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n",
    devEui[0], devEui[1], devEui[2], devEui[3],
    devEui[4], devEui[5], devEui[6], devEui[7]
  );

  // ConfigMemory.read(appKey, 0, 16);
  Serial.printf(
    "* AppKey: %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
    appKey[0], appKey[1], appKey[2], appKey[3],
    appKey[4], appKey[5], appKey[6], appKey[7],
    appKey[8], appKey[9], appKey[10], appKey[11],
    appKey[12], appKey[13], appKey[14], appKey[15]
  );

  if (memcmp(appKey, "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 16) == 0) {
    /* The appKey is required to be entered by user.*/
    Serial.onReceive(eventAppKeyInput);
    Serial.inputKeyboard(keyBuf, sizeof(keyBuf) - 1);
    Serial.println(
      "* Enter a new appKey as a hexadecimal string "
      "[ex. 00112233445566778899aabbccddeeff]"
    );
  } else {
    Serial.println("* Press any key to enter a new appKey in 3 seconds...");
    timerKeyInput.onFired(taskBeginJoin, NULL);
    timerKeyInput.startOneShot(3000);
    Serial.onReceive(eventKeyInput);
  }

  Serial.listen();

#endif //OVER_THE_AIR_ACTIVATION

  pinMode(D4, OUTPUT);
  digitalWrite(D4, LOW);
}
