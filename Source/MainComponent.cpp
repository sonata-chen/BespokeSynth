#ifndef MAINCOMPONENT_H_INCLUDED
#define MAINCOMPONENT_H_INCLUDED

#ifdef BESPOKE_WINDOWS
#include <GL/glew.h>
#endif

#include "../JuceLibraryCode/JuceHeader.h"
#include "nanovg/nanovg.h"
#define NANOVG_GLES2_IMPLEMENTATION
#include "nanovg/nanovg_gl.h"
#include "ModularSynth.h"
#include "SynthGlobals.h"
#include "Push2Control.h"  //TODO(Ryan) remove
#include "PluginProcessor.h"

#ifdef JUCE_WINDOWS
#include <Windows.h>
#endif

//==============================================================================
/*
 This component lives inside our window, and this is where you should put all
 your controls and content.
 */


class MainContentComponent   : public AudioProcessorEditor,
                               public OpenGLRenderer,
                               public FileDragAndDropTarget,
                               private Timer
{
public:
   //==============================================================================
   MainContentComponent(BespokeAudioProcessor &p, ModularSynth &s)
   : AudioProcessorEditor(&p)
   , mProcessor(p)
   , mSynth(s)
   , mLastFpsUpdateTime(0)
   , mFrameCountAccum(0)
   , mPixelRatio(1)
   {
      setResizable(true,true);
      setOpaque (true);
      
      /* Set this flag to improve performance. */
      openGLContext.setComponentPaintingEnabled(false);
      
      openGLContext.setRenderer(this);
      openGLContext.attachTo(*this);
      openGLContext.setOpenGLVersionRequired(juce::OpenGLContext::openGL3_2);
      openGLContext.setContinuousRepainting(false);
      
      int screenWidth, screenHeight;
      {
         const MessageManagerLock lock;
         mPixelRatio = Desktop::getInstance().getDisplays().getMainDisplay().scale;
         auto bounds = Desktop::getInstance().getDisplays().getTotalBounds(true);
         screenWidth = bounds.getWidth();
         screenHeight = bounds.getHeight();
         ofLog() << "pixel ratio: " << mPixelRatio << " screen width: " << screenWidth << " screen height: " << screenHeight;
      }
      
      int width = 600;
      int height = 400;
      ofxJSONElement userPrefs;
      bool loaded = userPrefs.open(ModularSynth::GetUserPrefsPath());
      if (loaded)
      {
         width = userPrefs["width"].asInt();
         height = userPrefs["height"].asInt();
      }
      
      if (width + getPosition().x > screenWidth)
         width = screenWidth - getPosition().x;
      if (height + getPosition().y + 20 > screenHeight)
         height = screenHeight - getPosition().y - 20;
      
      setSize(width, height);
      setWantsKeyboardFocus(true);
      Desktop::setScreenSaverEnabled(false);
   }
   
   ~MainContentComponent()
   {
      openGLContext.detach();
   }
   
   void timerCallback() override
   {
      static bool sHasGrabbedFocus = false;
      if (!sHasGrabbedFocus && !hasKeyboardFocus(true) && isVisible())
      {
         grabKeyboardFocus();
         sHasGrabbedFocus = true;
      }
      
      mSynth.Poll();
      
      static int sRenderFrame = 0;
#if DEBUG || BESPOKE_LINUX
      if (sRenderFrame % 2 == 0)
#else
      if (true)
#endif
      {
         openGLContext.triggerRepaint();
      }
      ++sRenderFrame;
   }
   
   void init()
   {
#ifdef JUCE_WINDOWS
      glewInit();
#endif

      mVG = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
      mFontBoundsVG = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
      
      if (mVG == nullptr)
         printf("Could not init nanovg.\n");
      if (mFontBoundsVG == nullptr)
         printf("Could not init font bounds nanovg.\n");

      Push2Control::CreateStaticFramebuffer();
      
      mSynth.LoadResources(mVG, mFontBoundsVG);
      
      mSynth.SetGUI(this);

#ifdef JUCE_WINDOWS
      HRESULT hr;
      hr = CoInitializeEx(0, COINIT_MULTITHREADED);
#endif
      
      startTimerHz(60);
   }
   
   void newOpenGLContextCreated() override { init(); }
   
   void openGLContextClosing() override
   {
      nvgDeleteGLES2(mVG);
      nvgDeleteGLES2(mFontBoundsVG);
   }
   
   void renderOpenGL() override
   {
      if (mSynth.IsLoadingState())
         return;
      
      mSynth.LockRender(true);
      
      Point<int> mouse = Desktop::getMousePosition();
      mouse -= getScreenPosition();
      mSynth.MouseMoved(mouse.x, mouse.y);
      
      float width = getWidth();
      float height = getHeight();
      
      static float kMotionTrails = .4f;
      
      ofVec3f bgColor(.09f,.09f,.09f);
      glViewport(0, 0, width*mPixelRatio, height*mPixelRatio);
      glClearColor(bgColor.x,bgColor.y,bgColor.z,0);
      if (kMotionTrails <= 0)
         glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
      
      nvgBeginFrame(mVG, width, height, mPixelRatio);
      
      if (kMotionTrails > 0)
      {
         ofSetColor(bgColor.x*255,bgColor.y*255,bgColor.z*255,(1-kMotionTrails*(ofGetFrameRate()/60.0f))*255);
         ofFill();
         ofRect(0,0,width,height);
      }
      
      nvgLineCap(mVG, NVG_ROUND);
      nvgLineJoin(mVG, NVG_ROUND);
      static float sSpacing = -.3f;
      nvgTextLetterSpacing(mVG, sSpacing);
      nvgTextLetterSpacing(mFontBoundsVG, sSpacing);
      
      mSynth.Draw(mVG);
      
      nvgEndFrame(mVG);
      
      mSynth.PostRender();
      
      mSynth.LockRender(false);
      
      ++mFrameCountAccum;
      int64 time = Time::currentTimeMillis();
      
      const int64 kCalcFpsIntervalMs = 1000;
      if (time - mLastFpsUpdateTime >= kCalcFpsIntervalMs)
      {
         mSynth.UpdateFrameRate(mFrameCountAccum / (kCalcFpsIntervalMs / 1000.0f));
         
         mFrameCountAccum = 0;
         mLastFpsUpdateTime = time;
      }
   }
   
   //==============================================================================
   void paint(Graphics& g) override
   {
   }
   
   void resized() override
   {
      // This is called when the MainContentComponent is resized.
      // If you add any child components, this is where you should
      // update their positions.
   }
   
private:
   void mouseDown(const MouseEvent& e) override
   {
      mSynth.MousePressed(e.getMouseDownX(), e.getMouseDownY(), e.mods.isRightButtonDown() ? 2 : 1);
   }
   
   void mouseUp(const MouseEvent& e) override
   {
      mSynth.MouseReleased(e.getPosition().x, e.getPosition().y, e.mods.isRightButtonDown() ? 2 : 1);
   }
   
   void mouseDrag(const MouseEvent& e) override
   {
      mSynth.MouseDragged(e.getPosition().x, e.getPosition().y, e.mods.isRightButtonDown() ? 2 : 1);
   }
   
   void mouseMove(const MouseEvent& e) override
   {
      //Don't do mouse move in here, it really slows UI responsiveness in some scenarios. We do it when we render instead.
      //mSynth.MouseMoved(e.getPosition().x, e.getPosition().y);
   }
   
   void mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& wheel) override
   {
      if (!wheel.isInertial)
         mSynth.MouseScrolled(wheel.deltaX * 30, wheel.deltaY * 30);
   }
   
   void mouseMagnify(const MouseEvent& e, float scaleFactor) override
   {
      mSynth.MouseMagnify(e.getPosition().x, e.getPosition().y, scaleFactor);
   }
   
   bool keyPressed(const KeyPress& key) override
   {
      int keyCode = key.getTextCharacter();
      if (keyCode < 32)
         keyCode = key.getKeyCode();
      bool isRepeat = true;
      if (find(mPressedKeys.begin(), mPressedKeys.end(), keyCode) == mPressedKeys.end())
      {
         mPressedKeys.push_back(keyCode);
         isRepeat = false;
      }
      mSynth.KeyPressed(keyCode, isRepeat);
      return true;
   }
   
   bool keyStateChanged(bool isKeyDown) override
   {
      if (!isKeyDown)
      {
         for (int keyCode : mPressedKeys)
         {
            if (!KeyPress::isKeyCurrentlyDown(keyCode))
            {
               mPressedKeys.remove(keyCode);
               mSynth.KeyReleased(keyCode);
               break;
            }
         }
      }
      return true;
   }
   
   bool isInterestedInFileDrag(const StringArray& files) override
   {
      //TODO_PORT(Ryan)
      return true;
   }
   
   void filesDropped(const StringArray& files, int x, int y) override
   {
      vector<string> strFiles;
      for (auto file : files)
         strFiles.push_back(file.toStdString());
      mSynth.FilesDropped(strFiles, x, y);
   }
   
   ModularSynth& mSynth;
   
   /* 
    This reference is provided as a quick way for your editor to
    access the processor object that created it.
    
    Currently, this member is not be used but maybe
    it will be useful in the future.
   */
   BespokeAudioProcessor &mProcessor;
   
   OpenGLContext openGLContext;
   NVGcontext* mVG;
   NVGcontext* mFontBoundsVG;
   int64 mLastFpsUpdateTime;
   int mFrameCountAccum;
   list<int> mPressedKeys;
   double mPixelRatio;
   
   JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainContentComponent)
};

#endif  // MAINCOMPONENT_H_INCLUDED
