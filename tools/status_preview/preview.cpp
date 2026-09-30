#include "../../mac_status_display/src/main.cpp"
#include <cassert>
#include <iostream>

int main(int argc,char **argv) {
  if(argc!=2){std::cerr<<"usage: status-preview OUTPUT_DIRECTORY\n";return 2;}
  std::string directory=argv[1];
  TFT_eSPI fontCheck;
  fontCheck.setTextFont(2);
  fontCheck.drawString("%",0,0);
  for(int row=0;row<16;++row)for(int column=0;column<9;++column) {
    bool ink=column<8 && (chr_f16_25[row]&(0x80>>column));
    assert(fontCheck.pixels[row*240+column]==(ink?TFT_WHITE:TFT_BLACK));
  }
  setup();
  display.save(directory+"/waiting.ppm");
  macstatus::StatusFrame frame{};
  frame.cpuTenths=347;frame.memoryTenths=628;frame.cpuTemperatureTenths=510;
  frame.gpuTemperatureTenths=640;frame.codexRemainingTenths=170;
  frame.downloadBytesPerSecond=12500000;frame.uploadBytesPerSecond=347000;
  strcpy(frame.networkLocation,"US-CA");frame.brightnessPercent=50;frame.offlineBrightnessPercent=5;
  claudeUsage.fiveHourTenths=730;claudeUsage.weekTenths=420;claudeUsage.stale=false;
  drawFrame(frame);display.save(directory+"/live.ppm");
  size_t before=display.writes;drawFrame(frame);assert(display.writes==before);
  for(uint32_t rate:{0U,1023U,1024U,1023488U,1048575U,1073741823U,UINT32_MAX}) {
    frame.downloadBytesPerSecond=frame.uploadBytesPerSecond=rate;drawFrame(frame);
  }
  frame.cpuTenths=frame.memoryTenths=1000;frame.cpuTemperatureTenths=frame.gpuTemperatureTenths=1500;
  frame.codexRemainingTenths=1000;strcpy(frame.networkLocation,"US-WWW");drawFrame(frame);
  claudeUsage.fiveHourTenths=claudeUsage.weekTenths=1000;drawClaudeUsage();
  display.save(directory+"/maximum.ppm");
  frame.codexUsageStale=true;frame.networkLocationStale=true;drawFrame(frame);
  claudeUsage.stale=true;drawClaudeUsage();display.save(directory+"/cached.ppm");
  frame.cpuTemperatureTenths=frame.gpuTemperatureTenths=macstatus::kMissingTemperature;
  frame.codexRemainingTenths=macstatus::kMissingCodexUsage;strcpy(frame.networkLocation,"--");drawFrame(frame);
  claudeUsage=macstatus::ClaudeUsageFrame();drawClaudeUsage();display.save(directory+"/missing.ppm");
  previewMillis()=5000;loop();display.save(directory+"/offline-unsynced.ppm");
  assert(!offlineClock.valid());
  offlineDrawn=false;offlineClock.sync(86399,5000);loop();display.save(directory+"/offline.ppm");
  before=display.writes;loop();assert(display.writes==before);
  previewMillis()=6000;loop();assert(offlineClock.seconds()==0);display.save(directory+"/midnight.ppm");
  drawFrame(frame);assert(!offlineDrawn);display.save(directory+"/reconnected.ppm");
  // Auxiliary usage must never count as a system heartbeat.
  const char *payload="MSA1,250,700,0";
  snprintf(serialBuffer,sizeof(serialBuffer),"$%s*%04X",payload,
      macstatus::crc16Ccitt(reinterpret_cast<const uint8_t *>(payload),strlen(payload)));
  serialLength=strlen(serialBuffer);lastValidFrameAt=5000;previewMillis()=7000;
  processLine();assert(lastValidFrameAt==5000);assert(claudeUsage.fiveHourTenths==250);
  previewMillis()=10000;loop();assert(offlineDrawn);
  previewMillis()=23000;lastValidFrameAt=23000;drawFrame(frame);loop();
  assert(claudeUsage.fiveHourTenths==-1);
  // Timers must keep working over the uint32_t millis wrap boundary.
  lastValidFrameAt=UINT32_MAX-1000;previewMillis()=3500;loop();assert(offlineDrawn);
  std::cout<<"9 real-code previews; bounds, overlap, clock, reconnect and timer checks passed\n";
}
