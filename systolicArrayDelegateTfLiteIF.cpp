/*
This class represents the capabilities of the delegate, 
which operations are supported, and a factory class for 
creating a kernel which encapsulates the delegated graph. 
For more details, see the interface defined in this C++ header file.
 The comments in the code explain each API in detail.*/


#include <tensorflow/lite/c/builtin_op_data.h>
#include <tensorflow/lite/builtin_ops.h>
#include <tensorflow/lite/context_util.h>
#include <tensorflow/c/c_api.h>
//#include <tensorflow/lite/c/c_api_internal.h>
#include <tensorflow/lite/c/common.h>

#include "tensorflow/lite/delegates/utils/dummy_delegate/dummy_delegate.h"

#include <utility>

#include "tensorflow/lite/delegates/utils/simple_delegate.h"

#include <vector>
#include <time.h>
//#include "tensorflow/lite/kernels/internal/kernel_util.h"
#include "systolicArrayDelegateTfLite.h"

#ifdef __cplusplus
extern "C" {
#endif

// MyDelegate implements the interface of SimpleDelegateInterface.
// This holds the Delegate capabilities.
class SystolicArray : public tflite::SimpleDelegateInterface {
 public:
  bool IsNodeSupportedByDelegate(const TfLiteRegistration* registration,
                                 const TfLiteNode* node,
                                 TfLiteContext* context) const   {
    // Only supports 2D convolutions
    if (kTfLiteBuiltinDepthwiseConv2d != registration->builtin_code &&
        kTfLiteBuiltinConv2d != registration->builtin_code)
      return false;
    // This delegate only supports float32 types.
    for (int i = 0; i < node->inputs->size; ++i) {
      auto& tensor = context->tensors[node->inputs->data[i]];
      if (tensor.type != kTfLiteFloat32) return false;
    }
    return true;
  }

  TfLiteStatus Initialize(TfLiteContext* context)   { return kTfLiteOk; }

  const char* Name() const   {
    static constexpr char kName[] = "Systolicarray";
    return kName;
  }

  std::unique_ptr<tflite::SimpleDelegateKernelInterface> CreateDelegateKernelInterface()
       override {
    return std::make_unique<tflite::SystolicArrayKernel>();
  }
};



// instantiate the delegate, it returns null if there is an error 
TfLiteDelegate * tflite_plugin_create_delegate() 
//char** argv , char** argv2, size_t argc, void (*report_error)(const char *) ) 
  {
  TfLiteDelegate* delegate = new TfLiteDelegate;

  delegate->data_ = nullptr;
  delegate->flags = kTfLiteDelegateFlagsNone;
//  delegate->Prepare = &DelegatePrepare;
  
  return delegate;
}


void tflite_plugin_destroy_delegate(void  * delegate_op , void * argtypes) {
// destroy the delegate   
TfLiteDelegate * delegate= (TfLiteDelegate *) delegate_op;

free(delegate);
}

#ifdef __cplusplus
} // extern "C"
#endif