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
  drawFrame(frame);display.save(directory+"/live.ppm");
  size_t before=display.writes;drawFrame(frame);assert(display.writes==before);
  for(uint32_t rate:{0U,1023U,1024U,1023488U,1048575U,1073741823U,UINT32_MAX}) {
    frame.downloadBytesPerSecond=frame.uploadBytesPerSecond=rate;drawFrame(frame);
  }
  frame.cpuTenths=frame.memoryTenths=1000;frame.cpuTemperatureTenths=frame.gpuTemperatureTenths=1500;
  frame.codexRemainingTenths=1000;strcpy(frame.networkLocation,"US-WWW");drawFrame(frame);
  display.save(directory+"/maximum.ppm");
  frame.codexUsageStale=true;frame.networkLocationStale=true;drawFrame(frame);
  display.save(directory+"/cached.ppm");
  frame.cpuTemperatureTenths=frame.gpuTemperatureTenths=macstatus::kMissingTemperature;
  frame.codexRemainingTenths=macstatus::kMissingCodexUsage;strcpy(frame.networkLocation,"--");drawFrame(frame);
  display.save(directory+"/missing.ppm");
  previewMillis()=5000;loop();display.save(directory+"/offline.ppm");
  drawFrame(frame);assert(!offlineDrawn);display.save(directory+"/reconnected.ppm");
  // Timers must keep working over the uint32_t millis wrap boundary.
  lastValidFrameAt=UINT32_MAX-1000;previewMillis()=3500;loop();assert(offlineDrawn);
  std::cout<<"7 real-code previews; bounds, text overlap, unchanged-frame, reconnect and timer checks passed\n";
}
