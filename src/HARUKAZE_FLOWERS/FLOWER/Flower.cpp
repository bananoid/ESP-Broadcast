#include "Flower.h"

// #include <LEDShader.h>
// #include <LEDShaderSynth.h>
// #include <LEDShaderDiffusion.h>

Flower *FlowerSingleton = new Flower();

void Flower::begin(int wifiChannel, ESPSortedBroadcast::PeerRecord *peerList, int nPeers, Scheduler *runner)
{
  // Peer
  ESPSortedBroadcast::Peer::begin(wifiChannel, peerList, nPeers);

#ifndef OLEDSCREEN_DISABLED
  // Screen
  screen = new OledScreen(8, 22);
  screen->begin();
  screen->println(FlowerSingleton->peerDescription.name, 0);
  screen->println(WiFi.macAddress(), 1);
  Serial.println(WiFi.macAddress());
  displayScreen.set(50 * TASK_MILLISECOND, TASK_FOREVER, [this]() {
    screen->displayScreen();
  });
  runner->addTask(displayScreen);
  displayScreen.enable();
#endif

  // Ping
  // ping.set(TASK_SECOND, TASK_FOREVER, [this]() {
  //   BaseMessage msg;
  //   msg.sourceId = peerDescription.id;
  //   msg.targetId = 1; //random(0, this->nPeers);
  //   broadcastData((uint8_t *)&msg, sizeof(BaseMessage));
  //   screen->println("S:" + String(msg.sourceId) + "->" + String(msg.targetId) + "[PING] " + random(3, 87), 4);
  //   Serial.println("S:" + String(msg.sourceId) + "->" + String(msg.targetId) + "[PING]");
  // });
  // runner->addTask(ping);
  // ping.enable();

  // LEDs
  // CRGB *leds;
  int numOffLeds = 24;

  leds = new CRGB[numOffLeds];
  FastLED.addLeds<WS2812B, PIN_LED_RING_TOP, GRB>(leds, numOffLeds);
  FastLED.addLeds<WS2812B, PIN_LED_RING_BOTTOM, GRB>(leds, numOffLeds);

  // for (int i = 0; i < numOffLeds; i++)
  // {
  //   /* code */
  //   leds[i] = CRGB::Blue;
  // }
  // FastLED.show();
  ledRenderer.begin(leds, numOffLeds);
  ledSynth = new LEDSynth::LEDSynth(numOffLeds, &ledRenderer, runner);

  // LEDSynth::LEDShaderSynth *shader;

  for (int i = 0; i < N_LAYERS; i++)
  {
    shaders[i] = ledSynth->addLEDShaderSynth();
  }
}

void Flower::receiveDataCB(const uint8_t *mac, const uint8_t *incomingData, int len)
{
  // Serial.println("Receiving Message!");
  String messageTypeName;

  uint8_t messageType = getMessageTypeFromData(incomingData);
  uint targetId;
  uint sourceId;

  // Serial.println(messageType);

  switch (messageType)
  {

  case PING:
  {
    BaseMessage pingMessage;
    memcpy(&pingMessage, incomingData, sizeof(pingMessage));
    targetId = pingMessage.targetId;
    sourceId = pingMessage.sourceId;
    messageTypeName = "ping";
    break;
  }
  case MSG_AUDIO_BANDS:
  {
    AudioBandsMessage msg;
    memcpy(&msg, incomingData, sizeof(msg));
    targetId = msg.targetId;
    sourceId = msg.sourceId;

#ifndef OLEDSCREEN_DISABLED
    messageTypeName = "Audio Bands";
    char c[22];
    sprintf(c, "L:%3d M:%3d H:%3d", int(msg.bandLowVal * 999), int(msg.bandMidVal * 999), int(msg.bandHighVal * 999));
    screen->println(c, 7);
#endif

    LEDSynth::LEDShaderSynth *shader;
    for (int i = 0; i < N_LAYERS; i++)
    {
      shader = shaders[i];

      msg.bandLowVal = msg.bandLowVal * msg.bandLowVal * msg.bandLowVal * 500;
      msg.bandMidVal = msg.bandMidVal * msg.bandMidVal * msg.bandMidVal * 300;
      msg.bandHighVal = msg.bandHighVal * msg.bandHighVal * msg.bandHighVal * 50;

      shader->setAudioBands(msg.bandLowVal, msg.bandMidVal, msg.bandHighVal);
    }

    break;
  }

  case MSG_LED_SYNTH_LAYER_COLOR:
  {
    LedSynthLayerColorMessage msg;
    memcpy(&msg, incomingData, sizeof(msg));
    uint8_t layer = msg.layer;
    LEDSynth::LEDShaderSynth *shader = shaders[layer];
    shader->targetState->hue = msg.hue;
    shader->targetState->hueRad = msg.hueRad;
    shader->targetState->scale = msg.scale;
    shader->targetState->speed = msg.speed * 0.01;

    if (msg.targetId == peerDescription.id)
    {
      for (int i = 0; i < N_LAYERS; i++)
      {
        shaders[i]->targetState->intensity = msg.intensity;
      }
    }

#ifndef OLEDSCREEN_DISABLED
    screen->println("Receiving layer:" + String(msg.layer) + " " + String(msg.intensity), 6);
#endif

    break;
  }

  case MSG_LED_SYNTH_LAYER_SHAPE:
  {
    LedSynthLayerShapeMessage msg;
    memcpy(&msg, incomingData, sizeof(msg));
    uint8_t layer = msg.layer;
    LEDSynth::LEDShaderSynth *shader = shaders[layer];
    shader->targetState->que = msg.que;
    shader->targetState->sym = msg.sym;
    shader->targetState->fmAmo = msg.fmAmo;
    shader->targetState->fmFrq = msg.fmFrq;
    break;
  }

  case MSG_LED_SYNTH_LAYER_AUDIO:
  {
    LedSynthLayerAudioMessage msg;
    memcpy(&msg, incomingData, sizeof(msg));
    uint8_t layer = msg.layer;
    LEDSynth::LEDShaderSynth *shader = shaders[layer];

    shader->audioAmpLowBand = msg.audioAmpLowBand;
    shader->audioAmpMidBand = msg.audioAmpMidBand;
    shader->audioAmpHighBand = msg.audioAmpHighBand;

    shader->audioInfluence = msg.audioInfluence;

    Serial.printf("audioInfluence %f\n", shader->audioInfluence);

    break;
  }

  case MSG_LED_SYNTH_GLOBAL:
  {
    LedSynthGlobalMessage msg;
    memcpy(&msg, incomingData, sizeof(msg));
    targetId = msg.targetId;
    sourceId = msg.sourceId;
    messageTypeName = "Led Global Msg";
    break;
  }
  default:
    targetId = -1;
    sourceId = -1;
    messageTypeName = "UNKNOWN";
    break;
  }
}

void Flower::registerReceiveDataCB()
{
  esp_now_register_recv_cb([](const uint8_t *macaddr, const uint8_t *incomingData, int len) {
    FlowerSingleton->receiveDataCB(macaddr, incomingData, len);
  });
}
