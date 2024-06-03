/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#include <string>
#include <vector>

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/tools/command_line_flags.h"
#include "tensorflow/lite/tools/logging.h"

#include "systolicArrayDelegateTfLite.h"


namespace tflite {
namespace tools {

TfLiteDelegate* CreateSystolicArrayDelegateFromOptions(char** options_keys,
                                               char** options_values,
                                               size_t num_options) {
  SystolicArrayDelegateOptions options = TfLiteSystolicArrayDelegateOptionsDefault();
  LOG("[DELEGATE LOG] --- Creating the Systolic Array delegate with options\n");
  return TfLiteSystolicArrayDelegateCreate(&options);
}

}  // namespace tools
}  // namespace tflite

extern "C" {

// Defines two symbols that need to be exported to use the TFLite external
// delegate. See tensorflow/lite/delegates/external for details.
 TfLiteDelegate* tflite_plugin_create_delegate(
    char** options_keys, char** options_values, size_t num_options,
    void (*report_error)(const char*)) {
      LOG("[DELEGATE LOG] --- Creating the Systolic Array delegate\n");
  return tflite::tools::CreateSystolicArrayDelegateFromOptions(
      options_keys, options_values, num_options);
}

 void tflite_plugin_destroy_delegate(TfLiteDelegate* delegate) {
  LOG("[DELEGATE LOG] --- Destroying the Systolic Array delegate\n");
  TfLiteSystolicArrayDelegateDelete(delegate);
}

/*
* Fault injection routines from faultInjector.cpp
* exported to python
*/


size_t getTotalFaultPlacement(void)   {
    return 0; //SystolicArraySim::getTotalFaultPlacement();
}

bool SetInjectionPoint(uint32_t index)   {
  return true;
} 

bool EnableInjection(uint32_t index) {
  return false;
}



}  // extern "C"
