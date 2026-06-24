// ============================================================================
// edgeai_tflite.h - TFLite Micro inference for the Edge AI module.
//
//   GERCEK sinir agi cikarimi. Sadece TFLite Micro kutuphanesi lib_deps'te
//   varsa derlenir (#if __has_include). Yoksa stub'lar devreye girer ve
//   mod_edgeai.h prototip-eslestirmeye duser - build her durumda yesil kalir.
//
//   Model: float32 girdi [1,24] (EAI_FEAT), float32 softmax cikti [1,N].
//   Bkz. edgeai_model.h (model byte dizisi + uretim talimati).
//
//   NOT (surum hassasiyeti): asagidaki API guncel TFLite Micro icindir
//   (MicroMutableOpResolver + 4-argumanli MicroInterpreter, ErrorReporter yok).
//   Cok eski bir Arduino TFLite paketi kullanirsan interpreter ctor'una 5.
//   arguman (error_reporter) ve AllOpsResolver gerekebilir - o satirlar
//   asagida yorum olarak isaretli.
// ============================================================================
#pragma once

#if __has_include(<TensorFlowLite_ESP32.h>) || __has_include("tensorflow/lite/micro/micro_interpreter.h")
  #define EAI_HAVE_TFLITE 1
#endif

#ifdef EAI_HAVE_TFLITE
  #if __has_include(<TensorFlowLite_ESP32.h>)
    #include <TensorFlowLite_ESP32.h>
  #endif
  #include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
  #include "tensorflow/lite/micro/micro_interpreter.h"
  #include "tensorflow/lite/schema/schema_generated.h"
#endif

#include "edgeai_model.h"

#ifdef EAI_HAVE_TFLITE

namespace eaitf {
  static const tflite::Model*      model   = nullptr;
  static tflite::MicroInterpreter* interp  = nullptr;
  static constexpr int             ARENA   = 18 * 1024;   // tiny MLP -> ~a few KB
  alignas(16) static uint8_t       arena[ARENA];
  static bool init = false, ok = false;
}

static void eaiTfliteSetup() {
  if (eaitf::init) return;
  eaitf::init = true;
  if (g_edgeai_model_len == 0) { eaitf::ok = false; return; }   // placeholder model

  eaitf::model = tflite::GetModel(g_edgeai_model);
  if (eaitf::model->version() != TFLITE_SCHEMA_VERSION) { eaitf::ok = false; return; }

  // Ops used by the Dense+ReLU+Softmax MLP. Bump the template count if you add
  // layers/ops in the training script.
  static tflite::MicroMutableOpResolver<4> resolver;
  resolver.AddFullyConnected();
  resolver.AddRelu();
  resolver.AddSoftmax();
  resolver.AddReshape();

  // Modern TFLM (4-arg). Eski paket icin: ...(model, resolver, arena, ARENA, &reporter)
  static tflite::MicroInterpreter inst(eaitf::model, resolver, eaitf::arena, eaitf::ARENA);
  eaitf::interp = &inst;
  eaitf::ok = (eaitf::interp->AllocateTensors() == kTfLiteOk);
}

static bool eaiTfliteReady() { eaiTfliteSetup(); return eaitf::ok; }

// feat -> model -> (label, confidence). Returns false if model unavailable.
static bool eaiTfliteInfer(const float* feat, int featLen, int& label, float& conf) {
  if (!eaiTfliteReady()) return false;
  TfLiteTensor* in = eaitf::interp->input(0);
  int n = in->dims->data[in->dims->size - 1];
  if (in->type != kTfLiteFloat32) return false;             // train script exports float
  for (int i = 0; i < n; i++) in->data.f[i] = (i < featLen) ? feat[i] : 0.0f;

  if (eaitf::interp->Invoke() != kTfLiteOk) return false;

  TfLiteTensor* out = eaitf::interp->output(0);
  int m = out->dims->data[out->dims->size - 1];
  int   best = 0; float bv = out->data.f[0];
  for (int i = 1; i < m; i++) if (out->data.f[i] > bv) { bv = out->data.f[i]; best = i; }
  label = best; conf = bv;
  return true;
}

#else   // ---- library not present: harmless stubs, prototype matcher is used ----

static bool eaiTfliteReady() { return false; }
static bool eaiTfliteInfer(const float*, int, int&, float&) { return false; }

#endif
