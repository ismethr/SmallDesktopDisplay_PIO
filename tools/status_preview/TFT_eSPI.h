#pragma once
// Host raster adapter for the real status-screen drawing code. Uses the exact
// bundled TFT_eSPI bitmaps; hardware inversion/color order still needs a panel.
#include "Arduino.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>
#include "Font16.c"
#include "glcdfont.c"
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5
class TFT_eSPI {
  int fontId = 1, scale = 1, datum = TL_DATUM;
  uint16_t foreground = TFT_WHITE;
  struct Label { int x, y, w, h; std::string text; };
  std::vector<Label> labels;
  static bool intersects(const Label &a, const Label &b) {
    return a.x < b.x+b.w && a.x+a.w > b.x && a.y < b.y+b.h && a.y+a.h > b.y;
  }
public:
  std::array<uint16_t, 240*240> pixels{};
  size_t writes = 0;
  void begin() {}
  void invertDisplay(int) {}
  void setRotation(int) {}
  void setTextDatum(int value) { datum = value; }
  void setTextFont(int value) { fontId = value; }
  void setTextSize(int value) { scale = value; }
  void setTextColor(uint16_t value, uint16_t) { foreground = value; }
  void drawPixel(int x,int y,uint16_t color) {
    if(x<0 || x>=240 || y<0 || y>=240) throw std::runtime_error("pixel outside 240x240 screen");
    pixels[y*240+x]=color; ++writes;
  }
  void fillRect(int x,int y,int w,int h,uint16_t color) {
    labels.erase(std::remove_if(labels.begin(),labels.end(),[&](const Label &a){return x<=a.x && y<=a.y && x+w>=a.x+a.w && y+h>=a.y+a.h;}),labels.end());
    for(int j=y;j<y+h;++j)for(int i=x;i<x+w;++i)drawPixel(i,j,color);
  }
  void fillScreen(uint16_t color) { labels.clear(); fillRect(0,0,240,240,color); }
  void drawFastHLine(int x,int y,int w,uint16_t c) { for(int i=0;i<w;++i)drawPixel(x+i,y,c); }
  void drawFastVLine(int x,int y,int h,uint16_t c) { for(int i=0;i<h;++i)drawPixel(x,y+i,c); }
  void drawRect(int x,int y,int w,int h,uint16_t c) {drawFastHLine(x,y,w,c);drawFastHLine(x,y+h-1,w,c);drawFastVLine(x,y,h,c);drawFastVLine(x+w-1,y,h,c);}
  void drawLine(int x,int y,int endX,int endY,uint16_t c) {
    int dx=std::abs(endX-x),sx=x<endX?1:-1,dy=-std::abs(endY-y),sy=y<endY?1:-1,error=dx+dy;
    for(;;){drawPixel(x,y,c);if(x==endX&&y==endY)break;int e=2*error;if(e>=dy){error+=dy;x+=sx;}if(e<=dx){error+=dx;y+=sy;}}
  }
  void fillCircle(int x,int y,int r,uint16_t c) {for(int j=-r;j<=r;++j)for(int i=-r;i<=r;++i)if(i*i+j*j<=r*r)drawPixel(x+i,y+j,c);}
  void drawCircle(int x,int y,int r,uint16_t c) {for(int j=-r;j<=r;++j)for(int i=-r;i<=r;++i)if(std::abs(std::sqrt(double(i*i+j*j))-r)<.6)drawPixel(x+i,y+j,c);}
  void fillTriangle(int x1,int y1,int x2,int y2,int x3,int y3,uint16_t c) {
    auto edge=[](int a,int b,int d,int e,int x,int y){return (x-a)*(e-b)-(y-b)*(d-a);};
    for(int y=std::min({y1,y2,y3});y<=std::max({y1,y2,y3});++y)for(int x=std::min({x1,x2,x3});x<=std::max({x1,x2,x3});++x){int a=edge(x1,y1,x2,y2,x,y),b=edge(x2,y2,x3,y3,x,y),d=edge(x3,y3,x1,y1,x,y);if((a>=0&&b>=0&&d>=0)||(a<=0&&b<=0&&d<=0))drawPixel(x,y,c);}
  }
  void fillRoundRect(int x,int y,int w,int h,int r,uint16_t c) {
    r=std::min({r,w/2,h/2});
    for(int j=0;j<h;++j)for(int i=0;i<w;++i){int dx=std::max({r-i,0,i-(w-r-1)}),dy=std::max({r-j,0,j-(h-r-1)});if(dx*dx+dy*dy<=r*r)drawPixel(x+i,y+j,c);}
  }
  void drawRoundRect(int x,int y,int w,int h,int r,uint16_t c) {fillRoundRect(x,y,w,h,r,c);fillRoundRect(x+1,y+1,w-2,h-2,std::max(0,r-1),TFT_BLACK);}
  int textWidth(const char *text) const {int w=0;for(const unsigned char *p=(const unsigned char *)text;*p;++p)w+=fontId==2?widtbl_f16[*p-32]:6;return w*scale;}
  int drawString(const char *text,int x,int y) {
    int width=textWidth(text),height=(fontId==2?16:8)*scale;
    if(datum%3==1)x-=width/2;else if(datum%3==2)x-=width;
    if(datum>=3)y-=height/2;
    Label label{x,y,width,height,text};
    if(x<0||y<0||x+width>240||y+height>240)throw std::runtime_error("text outside screen: "+label.text);
    for(const auto &other:labels)if(intersects(label,other))throw std::runtime_error("overlapping text: "+label.text+" / "+other.text);
    labels.push_back(label);
    int start=x;
    for(const unsigned char *p=(const unsigned char *)text;*p;++p){
      int w=fontId==2?widtbl_f16[*p-32]:6,h=fontId==2?16:8;
      // Font 2's advance includes a trailing pixel: TFT_eSPI deliberately
      // uses +6, not +7, when deriving bitmap row bytes (notably for '%').
      int rowBytes=(w+6)/8;
      for(int j=0;j<h;++j)for(int i=0;i<w;++i){
        bool bit=fontId==2?(i<rowBytes*8&&(chrtbl_f16[*p-32][j*rowBytes+i/8]&(0x80>>(i%8)))!=0):(i<5&&(font[*p*5+i]&(1<<j))!=0);
        if(bit)for(int a=0;a<scale;++a)for(int b=0;b<scale;++b)drawPixel(x+i*scale+b,y+j*scale+a,foreground);
      }
      x+=w*scale;
    }return x-start;
  }
  void save(const std::string &path) const {
    std::ofstream out(path,std::ios::binary);out<<"P6\n240 240\n255\n";
    for(uint16_t c:pixels){char rgb[]={char(((c>>11)&31)*255/31),char(((c>>5)&63)*255/63),char((c&31)*255/31)};out.write(rgb,3);}
    if(!out)throw std::runtime_error("cannot save preview");
  }
};
