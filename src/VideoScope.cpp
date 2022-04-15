#include "windows.h"
#include <string.h>
#include "avisynth.h"

#define VIDEOSCOPE_VERSION 1_2

/*********************************************************************/
/*********************************************************************/
//  VideoScope class
//
class VideoScope : public GenericVideoFilter
{
//private class-wide variable declarations
  const VideoInfo src_vi;
  bool DrawSide;
  bool DrawBottom;
  bool DrawFull;
  bool DrawTickMarks;
  bool DrawFrameUV;//skip the overhead if not needed
  bool has_at_least_v8; // passing frame property support
//  bool UsingYUY2;
//  bool UsingRGB;

  enum eFrameMode {//types of full-frame displays
    fblank, fcolormap, fdistY, fdistU, fdistV, fUV, fLast
  };
  eFrameMode FrameMode;

  enum eHistoType {
    hFirst, hY, hU, hV, hYUV, hUV, hLast //first/last = input out of range
  };
  eHistoType HistoTypeSide, HistoTypeBottom;

  void BlackOutCorner(unsigned char* p, int pitch, int width, int height);

  int* UVmap; //array [256][256]

public:
  VideoScope(PClip _child, const char* _DrawMode, bool _TickMarks, const char* _HistoTypeSide, const char* _HistoTypeBottom, const char* _FrameType, IScriptEnvironment* env);
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
  ~VideoScope();

};

/*********************************************************************/
/*********************************************************************/
//  Constructor
//
VideoScope::VideoScope(PClip _child, const char* _DrawMode, bool _TickMarks, const char* _HistoTypeSide, const char* _HistoTypeBottom, const char* _FrameType, IScriptEnvironment* env)
  : GenericVideoFilter(_child), src_vi(vi)
{
  // Check frame property support
  has_at_least_v8 = true;
  try { env->CheckVersion(8); }
  catch (const AvisynthError&) { has_at_least_v8 = false; }

  if (!vi.IsYUY2())
    env->ThrowError("VideoScope: YUY2 data only");

/************ process user parameters ************/
  bool user_error=false;

  //DrawMode
  if(     0==_strcmpi(_DrawMode, "side"))   {DrawSide=true; DrawBottom=false; DrawFull=false;}
  else if(0==_strcmpi(_DrawMode, "bottom")) {DrawSide=false; DrawBottom=true; DrawFull=false;}
  else if(0==_strcmpi(_DrawMode, "both"))   {DrawSide=true; DrawBottom=true; DrawFull=true;}
  else {user_error=true; goto throw_user_error;}

  //DrawTickMarks
  if(_TickMarks) DrawTickMarks=true; else DrawTickMarks=false;

  //HistoTypeSide
  if(     0==_strcmpi(_HistoTypeSide, "Y"))   HistoTypeSide=hY;
  else if(0==_strcmpi(_HistoTypeSide, "U"))   HistoTypeSide=hU;
  else if(0==_strcmpi(_HistoTypeSide, "V"))   HistoTypeSide=hV;
  else if(0==_strcmpi(_HistoTypeSide, "YUV")) HistoTypeSide=hYUV;
  else if(0==_strcmpi(_HistoTypeSide, "UV"))  HistoTypeSide=hUV;
  else {user_error=true; goto throw_user_error;}

  //HistoTypeBottom
  if(     0==_strcmpi(_HistoTypeBottom, "Y"))   HistoTypeBottom=hY;
  else if(0==_strcmpi(_HistoTypeBottom, "U"))   HistoTypeBottom=hU;
  else if(0==_strcmpi(_HistoTypeBottom, "V"))   HistoTypeBottom=hV;
  else if(0==_strcmpi(_HistoTypeBottom, "YUV")) HistoTypeBottom=hYUV;
  else if(0==_strcmpi(_HistoTypeBottom, "UV"))  HistoTypeBottom=hUV;
  else {user_error=true; goto throw_user_error;}

  //FrameType
  if(     0==_strcmpi(_FrameType, "blank"))   FrameMode=fblank;
  else if(0==_strcmpi(_FrameType, "colormap"))FrameMode=fcolormap;
  else if(0==_strcmpi(_FrameType, "Y"))       FrameMode=fdistY;
  else if(0==_strcmpi(_FrameType, "U"))       FrameMode=fdistU;
  else if(0==_strcmpi(_FrameType, "V"))       FrameMode=fdistV;
  else if(0==_strcmpi(_FrameType, "UV"))      FrameMode=fUV;
  else {user_error=true; goto throw_user_error;}

throw_user_error:
  if(user_error) //help text
    env->ThrowError(
    "\nVideoScope help text\n"
    "Version 1.2\n\n"
    "VideoScope(DrawMode,TickMarks,HistoTypeSide,HistoTypeBottom,FrameType)\n"
    "DrawMode:  side  bottom  both\n"
    "TickMarks: true false\n"
    "HistoTypeSide:   Y  U  V  YUV  UV\n"
    "HistoTypeBottom: Y  U  V  YUV  UV\n"
    "FrameType: blank  colormap  Y  U  V  UV"
    );

/*****************************************/
  if(DrawSide) vi.width += 256;
  if(DrawBottom) vi.height += 256;

  if(DrawFull && FrameMode==fUV) {
    DrawFrameUV=true;
    UVmap = new int[65536];
    if(!UVmap)
      env->ThrowError("VideoScope memory error frameUV");
  }
  else DrawFrameUV=false;
}

/*********************************************************************/
// destructor
//
VideoScope::~VideoScope()
{
  if(DrawFrameUV) delete []UVmap;
}

/*********************************************************************/
/*********************************************************************/
//  GetFrame
//
PVideoFrame __stdcall VideoScope::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame dst = has_at_least_v8 ? env->NewVideoFrameP(vi, &src) : env->NewVideoFrame(vi);
  unsigned char* dstWrite = dst->GetWritePtr();

  const unsigned char* dstHome = dst->GetReadPtr();
  const int dstPitch = dst->GetPitch();
  const int dstRowSize = dst->GetRowSize();
  const int dstHeight = dst->GetHeight();
  const int dstWidth = vi.width;

  const unsigned char* srcHome = src->GetReadPtr();
  const int srcPitch = src->GetPitch();
  const int srcRowSize = src->GetRowSize();
  const int srcHeight = src->GetHeight();
  const int srcWidth = src_vi.width;

  int y, x, i; //'global' locals
  unsigned char *histLinePtr, *writePtr;
  const unsigned char *linePtr, *readPtr;
  int framesumY[256]={0};
  int framesumU[256]={0};
  int framesumV[256]={0};

  const int Scale = 3000;
  int rowScale = Scale / srcWidth;
  int colScale = Scale / srcHeight;

  if(DrawFrameUV) {for(i=0; i<65536; i++) UVmap[i]=0;}

/*********************************************************************/
// copy the original frame
//
  env->BitBlt(dstWrite, dstPitch, srcHome, srcPitch, srcRowSize, srcHeight);

/*********************************************************************/
///////////////////////////////////////////////////////////////////////
//  side Histo
//
#define COLOR_LOW 80
#define COLOR_NEUT 128
#define COLOR_HIGH 176
#define COLOR_U 144
#define COLOR_V 112

  if(DrawSide) {
    for(y=0; y<srcHeight; y++) {
      int Y[256]={0};
      int U[256]={0};
      int V[256]={0};
      readPtr = srcHome + y * srcPitch;
      for(x=0; x<srcRowSize; x+=4) { // gather row stats
        Y[readPtr[x]]++;
        U[readPtr[x+1]]++;
        Y[readPtr[x+2]]++;
        V[readPtr[x+3]]++;
        if(DrawFrameUV) UVmap[readPtr[x+3]*256+readPtr[x+1]]++;
      }
      //keep full frame stats so we don't have to do it over
      for(x=0; x<256; x++) {
        framesumY[x] += Y[x];
        framesumU[x] += U[x];
        framesumV[x] += V[x];
      }
/************** Draw side ******************/

      switch(HistoTypeSide) {

      case hYUV : //new YUV - draws UV on top of Y
        {
          int xu,xv;

          // **** Y ****
          writePtr = dstWrite + y * dstPitch + srcWidth*2;
          for(x=0; x<256; x++) {
            if(i = Y[x]) {
              i = 48 + i * rowScale;
              i = i > 255 ? 255 : i;
            }
            *writePtr++ = i;
            *writePtr++ = 128;
          }

          //**** UV ****
          writePtr = dstWrite + y * dstPitch + srcWidth*2;
          for(x=0; x<256; x+=2) {
            xu = x; xv = x+256;
            if(i = U[x]+U[x+1]) {
              i = 48 + i * rowScale;
              i = i > 232 ? 232 : i;
              writePtr[xu] = i; //writePtr[x * 2 / 2]
              writePtr[xu+1] = COLOR_U;
            }
            if(i = V[x]+V[x+1]) {
              i = 48 + i * rowScale;
              i = i > 232 ? 232 : i;
              writePtr[xv] = i;
              writePtr[xv+1] = COLOR_V;
            }
          }
        }//end case hYUV
        break;

/***********************************************************/
      case hUV :
        {
          writePtr = dstWrite + y * dstPitch + srcWidth*2;
          for(x=0; x<256; x+=2) {
            if(i = U[x]+U[x+1]) {
              i = 48 + i * rowScale;
              i = i > 232 ? 232 : i;
            }
            *writePtr++ = i;
            *writePtr++ = COLOR_U;
          }

          for(x=0; x<256; x+=2) {
            if(i = V[x]+V[x+1]) {
              i = 48 + i * rowScale;
              i = i > 232 ? 232 : i;
            }
            *writePtr++ = i;
            *writePtr++ = COLOR_V;
          }
        }
        break;

      case hU :
        {
          writePtr = dstWrite + y * dstPitch + srcWidth*2;
          for(x=0; x<256; x++) {
            if(i = U[x]) {
              i = 48 + i * rowScale;
              i = i > 240 ? 240 : i;
            }
            *writePtr++ = i;
            *writePtr++ = COLOR_U;
          }// end for(x) (draw a line)
        }
        break;

      case hV :
        {
          writePtr = dstWrite + y * dstPitch + srcWidth*2;
          for(x=0; x<256; x++) {
            if(i = V[x]) {
              i = 48 + i * rowScale;
              i = i > 240 ? 240 : i;
            }
            *writePtr++ = i;
            *writePtr++ = COLOR_V;
          }// end for(x) (draw a line)
        }
        break;

      case hY :
      default :
        {
          writePtr = dstWrite + y * dstPitch + srcWidth*2;
          for(x=0; x<256; x++) {
            if(i = Y[x]) {
              i = 48 + i * rowScale;
              i = i > 255 ? 255 : i;
            }
            *writePtr++ = i;
            *writePtr++ = 128;
          }
        }

      }// end switch(HistoTypeSide)
    }// end for(y) (draw side histo)

/***********************************************************/
    if(DrawTickMarks) { // side YUY2 tick marks
      for(y=8; y < src->GetHeight(); y+=16) {
        histLinePtr = dstWrite + y*dstPitch + srcWidth*2;
        unsigned char u,v;
        for(x=0; x<256; x+=16) {
          histLinePtr[x*2] = 192;
          if(x & 64) {u=96; v=160;} else {u=160; v=96;}
          histLinePtr[x*2+1] = u;
          histLinePtr[x*2+3] = v;
          if((y & 63) == 56) { //every 8
            histLinePtr[x*2+16] = 192;
            histLinePtr[x*2+17] = u;
            histLinePtr[x*2+19] = v;
          }
          else if((y & 63)==24) {
            histLinePtr[x*2+8] = 192;
            histLinePtr[x*2+9] = u;
            histLinePtr[x*2+11] = v;
            histLinePtr[x*2+24] = 192;
            histLinePtr[x*2+25] = u;
            histLinePtr[x*2+27] = v;
          }
        }
      }// end for(y)
    }// end if(DrawTickMarks) (side)
  }//end if(DrawSide) (YUY2)

/*********************************************************************/
///////////////////////////////////////////////////////////////////////
//  bottom Histo
//
  if(DrawBottom) {
    for(x=0; x<srcRowSize; x+=4) {
      int Y1[256]={0};
      int U[256]={0};
      int Y2[256]={0};
      int V[256]={0};
      linePtr = srcHome + x;
      for(y=0; y<srcHeight; y++) {// gather column stats
        Y1[*linePtr]++;
         U[*(linePtr+1)]++;
        Y2[*(linePtr+2)]++;
         V[*(linePtr+3)]++;
        linePtr += srcPitch;
      }

///////////////////////////////////////////////////////////////////////
      switch(HistoTypeBottom) {
      case hU :
        {
          histLinePtr = dstWrite + ((dstHeight-1) * dstPitch); //bottom
          for(int z=0; z<256; z++) {
            if(i = U[z]) {
              i = 48 + i * colScale;
              i = i > 232 ? 232 : i;
            }
            histLinePtr[x] = i;
            histLinePtr[x+1] = COLOR_U;
            histLinePtr[x+2] = i;
            histLinePtr[x+3] = COLOR_U;

            histLinePtr -= dstPitch;
          }
        }//end case hU
        break;

      case hV :
        {
          histLinePtr = dstWrite + ((dstHeight-1) * dstPitch); //bottom
          for(int z=0; z<256; z++) {
            if(i = V[z]) {
              i = 48 + i * colScale;
              i = i > 232 ? 232 : i;
            }
            histLinePtr[x] = i;
            histLinePtr[x+1] = COLOR_V;
            histLinePtr[x+2] = i;
            histLinePtr[x+3] = COLOR_V;

            histLinePtr -= dstPitch;
          }
        }//end case hV
        break;

      case hUV :
        {
          histLinePtr = dstWrite + ((dstHeight-1) * dstPitch); //U ptr
          writePtr = histLinePtr - 128*dstPitch; //V pointer
          for(int z=0; z<256; z+=2) {
            //**** U ****
            if(i = U[z]+U[z+1]) {//half scale
              i = 48 + i * colScale;
              i = i > 232 ? 232 : i;
            }
            histLinePtr[x] = i;
            histLinePtr[x+1] = COLOR_U;
            histLinePtr[x+2] = i;
            histLinePtr[x+3] = COLOR_U;
            //**** V ****
            if(i = V[z]+V[z+1]) {
              i = 48 + i * colScale;
              i = i > 232 ? 232 : i;
            }
            writePtr[x] = i;
            writePtr[x+1] = COLOR_V;
            writePtr[x+2] = i;
            writePtr[x+3] = COLOR_V;

            histLinePtr -= dstPitch;
            writePtr -= dstPitch;
          }
        }
        break;

      case hYUV :
        {
          //**** Y ****
          int z;
          histLinePtr = dstWrite + ((dstHeight-1) * dstPitch); //bottom
          for(z=0; z<256; z++) {
            if(i = Y1[z]) {
              i = 48 + i * colScale;
              i = i > 250 ? 250 : i;
            }
            histLinePtr[x] = i;
            histLinePtr[x+1] = 128;
            if(i = Y2[z]) {
              i = 48 + i * colScale;
              i = i > 250 ? 250 : i;
            }
            histLinePtr[x+2] = i;
            histLinePtr[x+3] = 128;

            histLinePtr -= dstPitch;
          }
          //**** UV ****
          histLinePtr = dstWrite + ((dstHeight-1) * dstPitch); //U ptr
          writePtr = histLinePtr - 128*dstPitch; //V pointer
          for(z=0; z<256; z+=2) {
            //**** U ****
            if(i = U[z]+U[z+1]) {//half scale
              i = 48 + i * colScale;
              i = i > 232 ? 232 : i;
              histLinePtr[x] = i;
              histLinePtr[x+1] = COLOR_U;
              histLinePtr[x+2] = i;
              histLinePtr[x+3] = COLOR_U;
            }
            //**** V ****
            if(i = V[z]+V[z+1]) {
              i = 48 + i * colScale;
              i = i > 232 ? 232 : i;
              writePtr[x] = i;
              writePtr[x+1] = COLOR_V;
              writePtr[x+2] = i;
              writePtr[x+3] = COLOR_V;
            }
            histLinePtr -= dstPitch;
            writePtr -= dstPitch;
          }
        }// end case bottom hYUV
        break;

      case hY :
      default :
        {
          histLinePtr = dstWrite + ((dstHeight-1) * dstPitch); //bottom
          for(int z=0; z<256; z++) {
            if(i = Y1[z]) {
              i = 48 + i * colScale;
              i = i > 250 ? 250 : i;
            }
            histLinePtr[x] = i;
            histLinePtr[x+1] = 128;
            if(i = Y2[z]) {
              i = 48 + i * colScale;
              i = i > 250 ? 250 : i;
            }
            histLinePtr[x+2] = i;
            histLinePtr[x+3] = 128;

            histLinePtr -= dstPitch;
          }
        }//end case hY
      }//end switch(HistoTypeBottom)
    }// end for(x)

/***********************************************************/
    if(DrawTickMarks) { // bottom YUY2 tick marks
      for(x=8; x < srcWidth; x+=16) {
        histLinePtr = dstWrite + (dstHeight-1)*dstPitch + x*2; //bottom
        unsigned char u,v, *p;
        for(y=0; y<256; y+=16) {
          p=histLinePtr - y*dstPitch;
          if(y & 64) {u=96; v=160;} else {u=160; v=96;}
          *p=192;
          *(p+1)=u;
          *(p+3)=v;

          if((x & 63) == 56) {//extra ticks
            p -= 8*dstPitch;
            *p=192;
            *(p+1)=u;
            *(p+3)=v;
          }
          else if((x & 63)==24) {//triple tick
            p -= 4*dstPitch;
            *p=192;
            *(p+1)=u;
            *(p+3)=v;
            p -= 8*dstPitch;
            *p=192;
            *(p+1)=u;
            *(p+3)=v;
          }
        }
      }
    }//end if(DrawTickMarks) (bottom)
  }//end if(DrawBottom) (YUY2)

/*********************************************************************/
///////////////////////////////////////////////////////////////////////
//  full frame histo
//
  if(DrawFull) { //add VirtualDub style histo here later
    unsigned char color=COLOR_NEUT;
    int *colorsum=framesumY;

    switch (FrameMode) {

    case fUV :
    case fcolormap :
      {
        histLinePtr=dstWrite + srcWidth*2 + (dstHeight-1)*dstPitch;
        for(y=0; y<256; y++) {
          for(x=0; x<256; x+=2) {
            histLinePtr[x*2] = 128;
            histLinePtr[x*2+1] = 96+(x>>2); // x = u = blue
            histLinePtr[x*2+2] = 128;
            histLinePtr[x*2+3] = 96+(y>>2); // y = v = red
          }
          histLinePtr -= dstPitch;
        }
      }//end case colormap

      if(FrameMode==fUV) goto FallTo_fUV;
      break;
FallTo_fUV:

      {//UV map
        int* UVmapy;
        double UVscale = 500000.0 / double(srcWidth * srcHeight);
        _RPT1(0,"UVscale = %6f\n",UVscale);
        histLinePtr=dstWrite + srcWidth*2 + (dstHeight-1)*dstPitch;
        for(y=0; y<256; y++) {
          UVmapy = &UVmap[y*256];
          for(x=0; x<255; x++) {
            if(i=UVmapy[x]) {
              i = static_cast<int>(double(i) * UVscale);
              i=i > 255 ? 255 : i;
              histLinePtr[x*2] = i;
  //			histLinePtr[x*2+1] = 128;
            }
          }
          histLinePtr -= dstPitch;
        }
      }//end fUV

      //crosshair for UV map
      if(DrawFrameUV) {
        //horizontal
        histLinePtr=dstWrite + srcWidth*2 + (dstHeight-1)*dstPitch;
        histLinePtr -= 128*dstPitch;
        for(x=0; x<256; x++) {
          histLinePtr[x*2]=0;
          histLinePtr[x*2+2]=0;
        }
        //vertical
        histLinePtr=dstWrite + srcWidth*2 + (dstHeight-1)*dstPitch;
        histLinePtr += 256;
        for(y=0; y<256; y++) {
          *histLinePtr = 0;
          histLinePtr -= dstPitch;
        }
      }//end crosshair

      break;

    case fdistV :
      color = COLOR_V;
      colorsum = framesumV;
      goto DrawOneColor;

    case fdistU :
      color = COLOR_U;
      colorsum = framesumU;
      goto DrawOneColor;

    case fdistY :
      color=COLOR_NEUT;
      colorsum = framesumY;

DrawOneColor:
      {
        // point to top left corner
        histLinePtr = dstWrite + srcWidth*2 + srcHeight*dstPitch;
        BlackOutCorner(histLinePtr, dstPitch, 256, 256);
        // point to bottom left corner
        histLinePtr = dstWrite + srcWidth*2 + (dstHeight-1)*dstPitch;
        int max=0;
        double scaleY=0;
        for(x=0; x<256; x++) //find max count and set scale accordingly
          max = max > colorsum[x] ? max : colorsum[x];
        scaleY = double(max) / 254.0;

        y=0;
        for(x=0; x<256; x++) {
          int old_y = y;
          y = int( double(colorsum[x]) / scaleY );
          i = y > old_y ? +1 : -1;

          if(old_y < y) {
            for(int j=old_y; j<=y; j+=i) {
              writePtr = histLinePtr + 2*x - j*dstPitch;
              writePtr[0] = 192;
              writePtr[1] = color;
              if(x & 1) writePtr[-1] = color;
              else writePtr[3] = color;
            }
          } else {
            for(int j=old_y; j>=y; j+=i) {
              writePtr = histLinePtr + 2*x - j*dstPitch;
              writePtr[0] = 192;
              writePtr[1] = color;
              if(x & 1) writePtr[-1] = color;
              else writePtr[3] = color;
            }
          }

        }
      }//end case fdistY

      break;

    case fblank :
    default : //blank
      {
        histLinePtr=dstWrite + srcWidth*2 + (dstHeight-1)*dstPitch;
        for(y=0; y<256; y++) {
          for(x=0; x<256; x+=2) {
            histLinePtr[x*2] = 0;
            histLinePtr[x*2+1] = 128; // x = u = blue
            histLinePtr[x*2+2] = 0;
            histLinePtr[x*2+3] = 128; // y = v = red
          }
          histLinePtr -= dstPitch;
        }
      }//end blank (default)

    }//end switch(FrameMode)
  }//if(DrawFull)

//  _RPT0(0,"\n");
  return dst;
} // end GetFrame

/*********************************************************************/
/*********************************************************************/
// black out the full frame corner
//
void VideoScope::BlackOutCorner(unsigned char* p, int pitch, int width, int height)
{
  for(int y=0; y<width; y++) {
    for(int xx=0; xx<2*width; xx+=2) {
      p[xx] = 0;
      p[xx+1] = 128;
    }
    p += pitch;
  }
}

/*********************************************************************/
// add function (avisynth specific)
//
AVSValue __cdecl Create_VideoScope(AVSValue args, void* user_data, IScriptEnvironment* env)
{
  return new VideoScope(args[0].AsClip(), args[1].AsString("both"), args[2].AsBool(1), args[3].AsString("Y"), args[4].AsString("Y"), args[5].AsString("blank"), env);
}

const AVS_Linkage* AVS_linkage;

extern "C" __declspec(dllexport)
const char* __stdcall AvisynthPluginInit3(IScriptEnvironment * env, const AVS_Linkage* const vectors)
{
  AVS_linkage = vectors;
  env->AddFunction("VideoScope", "c[DrawMode]s[TickMarks]b[HistoTypeSide]s[HistoTypeBottom]s[FrameType]s", Create_VideoScope, 0);
  return "`VideoScope' VideoScope plugin";
}