#pragma once

#include <pthread.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include "platform/Application.h"

static pthread_t s_workletThreadId = 0;

EM_JS(uint32_t, initWebAudio, (pthread_t* pthreadPtr, rp::Application* app), {
  // Create the context
  Module.audioCtx = new AudioContext();

  // Initialize the pthread shared by all AudioWorkletNodes in this context
  PThread.initAudioWorkletPThread(Module.audioCtx, pthreadPtr).then(function() {
    out("Audio worklet PThread context initialized!")
  }, function(err) {
    out("Audio worklet PThread context initialization failed: " + [err, err.stack]);
  });

  // Creates an AudioWorkletNode and connects it to the output once it's created
  PThread.createAudioWorkletNode(
    Module.audioCtx,
    'native-passthrough-processor',
    {
      numberOfInputs: 0,
      numberOfOutputs : 1,
      outputChannelCount : [2],
      processorOptions: {
        app
      }
    }
  ).then(function(workletNode) {
    // Connect the worklet to the audio context output
    out("Audio worklet node created! Tap/click on the window if you don't hear audio!");
    workletNode.connect(Module.audioCtx.destination);
  }, function(err) {
    out("Audio worklet node creation failed: " + [err, err.stack]);
  });

  // To make this example usable we setup a resume on user interaction as browsers
  // all require the user to interact with the page before letting audio play
  if (window && window.addEventListener) {
    var opts = { capture: true, passive : true };
    window.addEventListener("touchstart", function() { Module.audioCtx.resume() }, opts);
    window.addEventListener("mousedown", function() { Module.audioCtx.resume() }, opts);
    window.addEventListener("keydown", function() { Module.audioCtx.resume() }, opts);
    window.addEventListener("drop", function() { Module.audioCtx.resume() }, opts);
  }

  return Module.audioCtx.sampleRate;
});

// This is the native code audio generator - it outputs an interleaved stereo buffer
// containing a simple, continuous sine wave.
EMSCRIPTEN_KEEPALIVE extern "C" float* generateAudio(rp::Application* app, unsigned int numSamples) {
  assert(numSamples == 128); // Audio worklet quantum size is always 128
  static float outputBuffer[128*2]; // This is where we generate our data into
  static float wavePos = 0; // This is the generator wave position [0, 2*PI)

  app->onAudio(nullptr, outputBuffer, numSamples);


  /*const float PI2 = 3.14159f * 2.0f; // Very approximate :)
  const float MAXAMP = 0.2f; // 20% so it's not too loud

  float* out = outputBuffer;
  while(numSamples > 0) {
    // Amplitude at current position
    float a = sinf(wavePos) * MAXAMP;

    // Advance position, keep it in [0, 2*PI) range to avoid running out of float precision
    wavePos += (1.0f/48000.0f) * 440.0f * PI2;
    if(wavePos > PI2) {
      wavePos -= PI2;
    }

    // Set both left and right samples to the same value
    out[0] = a;
    out[1] = a;
    out += 2;

    numSamples -= 1;
  }*/

  return outputBuffer;
}
