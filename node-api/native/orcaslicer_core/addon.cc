#include <napi.h>
#include <string>

// Forward declaration from orca_minimal.cpp
namespace OrcaMinimal {
  std::string slice_to_gcode_minimal(const std::string& threemf_path, const std::string& config_json);
}

// Main addon function: exposes slice_to_gcode(path, configJson)
// Integrates with OrcaSlicer engine via orca_minimal.cpp
Napi::Value slice_to_gcode(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2 || !info[0].IsString() || !info[1].IsString()) {
    Napi::TypeError::New(env, "Expected (threeMfPath: string, configJson: string)").ThrowAsJavaScriptException();
    return env.Null();
  }
  std::string path = info[0].As<Napi::String>().Utf8Value();
  std::string config = info[1].As<Napi::String>().Utf8Value();

  try {
    // Call Orca minimal integration
    std::string gcode = OrcaMinimal::slice_to_gcode_minimal(path, config);
    return Napi::String::New(env, gcode);
  } catch (const std::exception& e) {
    Napi::Error::New(env, std::string("Slicing failed: ") + e.what()).ThrowAsJavaScriptException();
    return env.Null();
  }
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set(Napi::String::New(env, "slice_to_gcode"), Napi::Function::New(env, slice_to_gcode));
  return exports;
}

NODE_API_MODULE(orcaslicer_core, Init)

