#include <jni.h>

#include <GLES3/gl3.h>
#include <android/log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>

#include "nanovg.h"
#define NANOVG_GLES3_IMPLEMENTATION
#include "nanovg_gl.h"

namespace {

constexpr const char* kTag = "NanoVG";
constexpr float kPi = 3.14159265358979323846f;

NVGcolor color(unsigned char r, unsigned char g, unsigned char b,
               unsigned char a = 255)
{
    return nvgRGBA(r, g, b, a);
}

void fillRect(NVGcontext* vg, float x, float y, float w, float h,
              NVGcolor value)
{
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    nvgFillColor(vg, value);
    nvgFill(vg);
}

void roundRect(NVGcontext* vg, float x, float y, float w, float h,
               float radius, NVGcolor value)
{
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, radius);
    nvgFillColor(vg, value);
    nvgFill(vg);
}

void circle(NVGcontext* vg, float x, float y, float radius, NVGcolor value)
{
    nvgBeginPath(vg);
    nvgCircle(vg, x, y, radius);
    nvgFillColor(vg, value);
    nvgFill(vg);
}

void text(NVGcontext* vg, float x, float y, float size, NVGcolor value,
          const char* string, int align = NVG_ALIGN_LEFT | NVG_ALIGN_TOP)
{
    nvgFontFace(vg, "sans");
    nvgFontSize(vg, size);
    nvgTextAlign(vg, align);
    nvgFillColor(vg, value);
    nvgText(vg, x, y, string, nullptr);
}

void star(NVGcontext* vg, float cx, float cy, float outer, float inner,
          int points = 7)
{
    nvgBeginPath(vg);
    for (int i = 0; i < points * 2; ++i) {
        const float radius = i % 2 == 0 ? outer : inner;
        const float angle = -kPi * 0.5f + i * kPi / points;
        const float x = cx + std::cos(angle) * radius;
        const float y = cy + std::sin(angle) * radius;
        if (i == 0) nvgMoveTo(vg, x, y); else nvgLineTo(vg, x, y);
    }
    nvgClosePath(vg);
}

void background(NVGcontext* vg, float width, float height, NVGcolor glow)
{
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, width, height);
    nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, width, height,
        color(7, 11, 27), color(20, 14, 46)));
    nvgFill(vg);
    circle(vg, width * 0.88f, height * 0.16f,
           std::min(width, height) * 0.35f, glow);
}

struct Grid {
    float x = 14.0f;
    float y = 70.0f;
    float gap = 9.0f;
    float width = 0.0f;
    float height = 0.0f;
    int columns = 2;
};

Grid grid(float width, float height, int count)
{
    Grid result;
    result.columns = width > height ? count : 2;
    const int rows = (count + result.columns - 1) / result.columns;
    result.width = (width - 28.0f - result.gap * (result.columns - 1))
        / result.columns;
    result.height = (height - result.y - 16.0f - result.gap * (rows - 1))
        / rows;
    return result;
}

void cell(const Grid& layout, int index, float& x, float& y)
{
    x = layout.x + (index % layout.columns) * (layout.width + layout.gap);
    y = layout.y + (index / layout.columns) * (layout.height + layout.gap);
}

void panel(NVGcontext* vg, const Grid& layout, int index, const char* label)
{
    float x, y;
    cell(layout, index, x, y);
    roundRect(vg, x, y, layout.width, layout.height, 14, color(18, 28, 54, 235));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, layout.width, layout.height, 14);
    nvgStrokeWidth(vg, 1);
    nvgStrokeColor(vg, color(103, 126, 181, 112));
    nvgStroke(vg);
    text(vg, x + 11, y + 9, 9, color(124, 151, 209), label);
}

void heading(NVGcontext* vg, float width, const char* title,
             const char* subtitle, NVGcolor accent)
{
    text(vg, 18, 14, 24, color(244, 248, 255), title);
    text(vg, 19, 45, 10.5f, color(139, 163, 211), subtitle);
    roundRect(vg, width - 52, 18, 34, 8, 4, accent);
}

void featureScene(NVGcontext* vg, float width, float height, float elapsed)
{
    background(vg, width, height, color(45, 205, 226, 22));
    heading(vg, width, "NanoVG Android", "GLES 3 | live feature matrix",
            color(64, 222, 202));
    Grid layout = grid(width, height, 8);
    const char* labels[] = {"TEXT + UTF-8", "PATH + STROKE",
        "CLIP PATH + GRADIENT", "ARCS + ROUND CAPS", "SAVE + TRANSFORM",
        "SHADOW + BLEND", "IMAGE + SAMPLING", "AA + MOTION"};
    for (int i = 0; i < 8; ++i) panel(vg, layout, i, labels[i]);

    float x, y;
    cell(layout, 0, x, y);
    text(vg, x + 11, y + 34, 27, color(245, 248, 255), "Aa 123");
    text(vg, x + 11, y + 72, 15, color(225, 236, 255), "中文 NanoVG");
    cell(layout, 1, x, y);
    star(vg, x + layout.width * 0.5f, y + layout.height * 0.6f, 42, 18, 7);
    nvgFillPaint(vg, nvgLinearGradient(vg, x, y, x + layout.width,
                                      y + layout.height,
                                      color(255, 192, 80), color(240, 82, 158)));
    nvgFill(vg);
    nvgStrokeWidth(vg, 2); nvgStrokeColor(vg, color(255, 238, 192)); nvgStroke(vg);
    cell(layout, 2, x, y);
    nvgSave(vg);
    // NanoVG only exposes rectangular scissoring, unlike WhatsCanvas clipPath.
    nvgScissor(vg, x + 18, y + 35, layout.width - 36, layout.height - 50);
    for (int i = -4; i <= 5; ++i) {
        nvgBeginPath(vg); nvgMoveTo(vg, x + i * 22, y + layout.height);
        nvgLineTo(vg, x + 90 + i * 22, y + 35);
        nvgStrokeWidth(vg, 5); nvgStrokeColor(vg, color(255,255,255,90)); nvgStroke(vg);
    }
    nvgRestore(vg);
    cell(layout, 3, x, y);
    nvgBeginPath(vg); nvgArc(vg, x + layout.width * .5f, y + layout.height * .6f,
        42, -.85f * kPi, (.3f + .5f * std::sin(elapsed)) * kPi, NVG_CW);
    nvgStrokeWidth(vg, 12); nvgLineCap(vg, NVG_ROUND);
    nvgStrokeColor(vg, color(67,220,198)); nvgStroke(vg);
    cell(layout, 4, x, y);
    nvgSave(vg); nvgTranslate(vg, x + layout.width*.5f, y + layout.height*.6f);
    nvgRotate(vg, elapsed*.45f); roundRect(vg, -38, -38, 76, 76, 14,
                                           color(97,121,246)); nvgRestore(vg);
    cell(layout, 5, x, y);
    circle(vg, x + layout.width*.43f, y + layout.height*.6f, 34, color(71,211,229,205));
    nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
    circle(vg, x + layout.width*.58f, y + layout.height*.65f, 34, color(178,87,238,205));
    nvgGlobalCompositeOperation(vg, NVG_SOURCE_OVER);
    cell(layout, 6, x, y);
    for (int row = 0; row < 5; ++row) for (int col = 0; col < 5; ++col)
        fillRect(vg, x+14+col*18, y+42+row*18, 18, 18,
                 ((row+col)&1) ? color(78,218,206) : color(32,98,230));
    cell(layout, 7, x, y);
    const float pulse = .5f + .5f*std::sin(elapsed*2.2f);
    roundRect(vg, x+14, y+layout.height-40, layout.width-28, 12, 6, color(7,12,28));
    roundRect(vg, x+14, y+layout.height-40, (layout.width-28)*(.18f+.78f*pulse),
              12, 6, color(71,222,204));
}

void textScene(NVGcontext* vg, float width, float height, float elapsed)
{
    background(vg, width, height, color(45,205,226,22));
    heading(vg, width, "Text stress matrix",
            "fallback | layout | glyph effects", color(64,222,202));
    Grid layout = grid(width, height, 4);
    const char* labels[] = {"MIXED SCRIPT + FALLBACK", "WRAP + ALIGN",
                            "BASELINE + SHADOW", "TRANSFORMED TEXT"};
    for (int i=0;i<4;++i) panel(vg,layout,i,labels[i]);
    float x,y;
    cell(layout,0,x,y);
    text(vg,x+12,y+36,28,color(255,255,255),"Aa 中日 한글 123");
    text(vg,x+12,y+78,18,color(234,241,255),"中文 é fi NanoVG");
    for (int i=0;i<7;++i) text(vg,x+12,y+115+i*15,10.5f,color(145,170,216),
                               "Fallback and glyph atlas stress line");
    cell(layout,1,x,y);
    for (int i=0;i<12;++i) text(vg,x+12,y+36+i*16,11,color(222,232,250),
                                "Narrow text layout and alignment");
    cell(layout,2,x,y);
    for (int i=0;i<9;++i) {
        nvgFontBlur(vg, i%3==0 ? 2.0f : 0.0f);
        text(vg,x+layout.width*.5f,y+40+i*23,18,color(62,220,199),"Middle",
             NVG_ALIGN_CENTER|NVG_ALIGN_TOP);
    }
    nvgFontBlur(vg,0);
    cell(layout,3,x,y);
    for (int i=0;i<10;++i) {
        nvgSave(vg); nvgTranslate(vg,x+layout.width*.5f,y+40+i*21);
        nvgRotate(vg,.08f*std::sin(elapsed+i));
        text(vg,0,0,12,color(250,213,100),"LETTER SPACING",
             NVG_ALIGN_CENTER|NVG_ALIGN_TOP); nvgRestore(vg);
    }
}

void geometryScene(NVGcontext* vg, float width, float height, float elapsed)
{
    background(vg,width,height,color(120,91,241,22));
    heading(vg,width,"Geometry stress matrix",
            "fill rules | clips | joins | precision",color(147,103,247));
    Grid layout=grid(width,height,4);
    const char* labels[]={"EVEN-ODD + CONCAVE","NESTED CLIP","JOINS + CAPS","SUBPIXEL + ARC"};
    for(int i=0;i<4;++i) panel(vg,layout,i,labels[i]);
    float x,y;
    cell(layout,0,x,y);
    for(int i=0;i<10;++i){ star(vg,x+layout.width*.5f,y+layout.height*.58f,
        70-i*5,28-i*2,9); nvgFillColor(vg,color(250-i*8,90+i*8,154));
        nvgPathWinding(vg,(i&1)?NVG_HOLE:NVG_SOLID); nvgFill(vg); }
    cell(layout,1,x,y); nvgSave(vg);
    nvgScissor(vg,x+20,y+35,layout.width-40,layout.height-55);
    for(int i=-8;i<=8;++i){nvgBeginPath(vg);nvgMoveTo(vg,x+i*18,y+layout.height);
        nvgLineTo(vg,x+120+i*18,y+30);nvgStrokeWidth(vg,9);
        nvgStrokeColor(vg,color(101,218,237));nvgStroke(vg);} nvgRestore(vg);
    cell(layout,2,x,y);
    for(int i=0;i<8;++i){nvgBeginPath(vg);nvgMoveTo(vg,x+15,y+60+i*24);
        nvgLineTo(vg,x+layout.width*.3f,y+35+i*24);nvgLineTo(vg,x+layout.width-15,y+60+i*24);
        nvgStrokeWidth(vg,2+i);nvgLineJoin(vg,i&1?NVG_MITER:NVG_ROUND);
        nvgStrokeColor(vg,color(248,209,91));nvgStroke(vg);}
    cell(layout,3,x,y);
    for(int i=-9;i<=9;++i){nvgBeginPath(vg);nvgMoveTo(vg,x+15,y+layout.height*.55f+i*6.25f);
        nvgLineTo(vg,x+layout.width-15,y+layout.height*.55f+i*6.25f);
        nvgStrokeWidth(vg,.75f);nvgStrokeColor(vg,color(132,162,224,170));nvgStroke(vg);}
    nvgBeginPath(vg);nvgArc(vg,x+layout.width*.5f,y+layout.height*.55f,43,
        -2.2f+elapsed*.05f,2.1f+elapsed*.05f,NVG_CW);nvgStrokeWidth(vg,7);
    nvgStrokeColor(vg,color(66,224,199));nvgStroke(vg);
}

void compositingScene(NVGcontext* vg,float width,float height,float elapsed)
{
    background(vg,width,height,color(238,91,161,18));
    heading(vg,width,"Compositing stress matrix",
            "basic blends only; no saveLayer filters",color(244,103,164));
    Grid layout=grid(width,height,4);
    const char* labels[]={"CUTOUT APPROXIMATION","LIGHTER BLEND","FROSTED APPROXIMATION","ALPHA LAYERS"};
    for(int i=0;i<4;++i){panel(vg,layout,i,labels[i]);float x,y;cell(layout,i,x,y);
        const float tileSize=18;
        for(float ty=y+29;ty<y+layout.height-8;ty+=tileSize)
            for(float tx=x+8;tx<x+layout.width-8;tx+=tileSize)
                fillRect(vg,tx,ty,tileSize,tileSize,
                    (static_cast<int>((tx+ty)/tileSize)&1)?color(42,64,103,190):color(24,39,75,190));
    }
    float x,y; cell(layout,0,x,y);
    roundRect(vg,x+24,y+49,layout.width-48,layout.height-72,24,color(66,222,201));
    circle(vg,x+layout.width*.55f,y+layout.height*.62f,
           std::min(layout.width,layout.height)*.18f,color(18,28,54));
    cell(layout,1,x,y); nvgGlobalCompositeOperation(vg,NVG_LIGHTER);
    circle(vg,x+layout.width*.42f,y+layout.height*.62f,42,color(250,183,65,210));
    circle(vg,x+layout.width*.58f,y+layout.height*.62f,42,color(74,212,232,200));
    nvgGlobalCompositeOperation(vg,NVG_SOURCE_OVER);
    cell(layout,2,x,y); const float drift=std::sin(elapsed*1.7f)*9;
    NVGpaint glass=nvgBoxGradient(vg,x+21+drift,y+47,layout.width-42,
        layout.height-69,20,12,color(226,239,255,100),color(226,239,255,20));
    nvgBeginPath(vg);nvgRoundedRect(vg,x+21+drift,y+47,layout.width-42,
        layout.height-69,20);nvgFillPaint(vg,glass);nvgFill(vg);
    cell(layout,3,x,y);
    for(int i=0;i<12;++i) roundRect(vg,x+22+i*.7f,y+48+i*.7f,
        layout.width-44-i*1.4f,layout.height-70-i*1.4f,18,
        color(235,242,251,static_cast<unsigned char>(18+i*12)));
}

class Renderer {
public:
    explicit Renderer(std::string scene) : scene_(std::move(scene)) {}

    bool surfaceCreated() { return true; }

    bool resize(int width, int height, float density)
    {
        if(width<=0||height<=0) return false;
        release(); width_=width; height_=height;
        density_=std::isfinite(density)&&density>0?density:1;
        vg_=nvgCreateGLES3(NVG_ANTIALIAS|NVG_STENCIL_STROKES);
        if(!vg_) return false;
        int sans=nvgCreateFont(vg_,"sans","/system/fonts/Roboto-Regular.ttf");
        int cjk=nvgCreateFont(vg_,"cjk","/system/fonts/NotoSansCJK-Regular.ttc");
        if(sans>=0&&cjk>=0) nvgAddFallbackFontId(vg_,sans,cjk);
        __android_log_print(ANDROID_LOG_INFO,kTag,
            "Renderer ready: scene=%s size=%dx%d dpr=%.2f sans=%d cjk=%d",
            scene_.c_str(),width_,height_,density_,sans,cjk);
        return sans>=0;
    }

    void render(float elapsed)
    {
        if(!vg_) return;
        glViewport(0,0,width_,height_); glDisable(GL_DEPTH_TEST);
        glClearColor(.03f,.04f,.09f,1); glClear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
        const auto start=std::chrono::steady_clock::now();
        const float logicalWidth=width_/density_, logicalHeight=height_/density_;
        const float scale=std::min(logicalWidth/400.0f,logicalHeight/800.0f);
        const float offsetX=(logicalWidth-400.0f*scale)*.5f;
        nvgBeginFrame(vg_,logicalWidth,logicalHeight,density_);
        nvgSave(vg_); nvgTranslate(vg_,offsetX,0); nvgScale(vg_,scale,scale);
        if(scene_=="text_stress") textScene(vg_,400,800,elapsed);
        else if(scene_=="geometry_stress") geometryScene(vg_,400,800,elapsed);
        else if(scene_=="compositing_stress") compositingScene(vg_,400,800,elapsed);
        else featureScene(vg_,400,800,elapsed);
        nvgRestore(vg_);
        const auto recorded=std::chrono::steady_clock::now();
        nvgEndFrame(vg_);
        const auto ended=std::chrono::steady_clock::now();
        const auto recordUs=std::chrono::duration_cast<std::chrono::microseconds>(recorded-start).count();
        const auto flushUs=std::chrono::duration_cast<std::chrono::microseconds>(ended-recorded).count();
        const int second=static_cast<int>(elapsed);
        if(second!=lastSecond_&&second%5==0){lastSecond_=second;
            __android_log_print(ANDROID_LOG_INFO,kTag,
                "Frame stats: scene=%s recordCpu=%lldus flushCpu=%lldus",
                scene_.c_str(),static_cast<long long>(recordUs),static_cast<long long>(flushUs));}
        if(!first_){first_=true;__android_log_print(ANDROID_LOG_INFO,kTag,
            "First frame ready: scene=%s size=%dx%d",scene_.c_str(),width_,height_);}
    }

    void release(){if(vg_){nvgDeleteGLES3(vg_);vg_=nullptr;}first_=false;lastSecond_=-1;}
    ~Renderer(){release();}
private:
    NVGcontext* vg_=nullptr; std::string scene_; int width_=0,height_=0;
    float density_=1; bool first_=false; int lastSecond_=-1;
};

Renderer* from(jlong handle){return reinterpret_cast<Renderer*>(static_cast<std::uintptr_t>(handle));}
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_whatscanvas_demo_WhatsCanvasRenderer_nativeNanoCreate(JNIEnv* env,jobject,jstring name)
{
    std::string scene="feature_showcase";
    if(name){const char* value=env->GetStringUTFChars(name,nullptr);if(value){scene=value;env->ReleaseStringUTFChars(name,value);}}
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(new Renderer(std::move(scene))));
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_whatscanvas_demo_WhatsCanvasRenderer_nativeNanoSurfaceCreated(JNIEnv*,jobject,jlong h)
{return from(h)&&from(h)->surfaceCreated()?JNI_TRUE:JNI_FALSE;}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_whatscanvas_demo_WhatsCanvasRenderer_nativeNanoResize(JNIEnv*,jobject,jlong h,jint w,jint he,jfloat d)
{return from(h)&&from(h)->resize(w,he,d)?JNI_TRUE:JNI_FALSE;}
extern "C" JNIEXPORT void JNICALL
Java_com_whatscanvas_demo_WhatsCanvasRenderer_nativeNanoRender(JNIEnv*,jobject,jlong h,jfloat e)
{if(from(h))from(h)->render(e);}
extern "C" JNIEXPORT void JNICALL
Java_com_whatscanvas_demo_WhatsCanvasRenderer_nativeNanoDestroy(JNIEnv*,jobject,jlong h)
{delete from(h);}
