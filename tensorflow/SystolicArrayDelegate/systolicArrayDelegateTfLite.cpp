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
#include "systolicArrayDelegateTfLite.h"

#include <utility>

#include "tensorflow/lite/delegates/utils/simple_delegate.h"
#include "tensorflow/lite/c/builtin_op_data.h"
#include "tensorflow/lite/builtin_ops.h"
#include "tensorflow/lite/context_util.h"
#include "tensorflow/lite/context_util.h"
#include "tensorflow/lite/delegates/utils.h"
#include "tensorflow/lite/util.h"

#if __LOG__
#include "tensorflow/lite/kernels/internal/compatibility.h"
#endif /*__LOG__*/

/*fault injector*/
#include "faultInjector.h"

namespace tflite
{
  namespace SystolicArray_Delegate
  {

    // SystolicArray delegate kernel.
    class SystolicArrayDelegateKernel : public SimpleDelegateKernelInterface
    {
    public:
      explicit SystolicArrayDelegateKernel(const SystolicArrayDelegateOptions &options)
          : options_(options) {}

      TfLiteStatus Init(TfLiteContext *context,
                        const TfLiteDelegateParams *params) override
      {
        LOG("[DELEGATE LOG] --- Initializing the delegate kernel\n");
        return !options_.error_during_init ? kTfLiteOk : kTfLiteError;
      }

      TfLiteStatus Prepare(TfLiteContext *context, TfLiteNode *node) override
      {
        LOG("[DELEGATE LOG] --- Prepare the delegate kernel for the eval\n");
        return !options_.error_during_prepare ? kTfLiteOk : kTfLiteError;
      }

      TfLiteStatus Eval(TfLiteContext *context, TfLiteNode *node) override
      {
        LOG("[DELEGATE LOG] --- Hello sunshine, this should compute something\n");
        //must call this 
        //gemmFi(int transa, int transb, blas_arg_t * args, const void * cOriginal, size_t elemSize)
        /*
      auto& input_tensor_1 = context->tensors[inputs_[i][0]];
      auto& input_tensor_2 = context->tensors[inputs_[i][1]];
      auto& output_tensor = context->tensors[outputs_[i][0]];*/
        return !options_.error_during_invoke ? kTfLiteOk : kTfLiteError;
      }

    private:
      const SystolicArrayDelegateOptions options_;
    };

    // SystolicArrayDelegate implements the interface of SimpleDelegateInterface.
    // This holds the Delegate capabilities.
    class SystolicArrayDelegate : public SimpleDelegateInterface
    {
    public:
      explicit SystolicArrayDelegate(const SystolicArrayDelegateOptions &options)
          : options_(options) {}
      bool IsNodeSupportedByDelegate(const TfLiteRegistration *registration,
                                     const TfLiteNode *node,
                                     TfLiteContext *context) const override
      {
        /*this is called for each node in the ANN*/
        LOG("[DELEGATE LOG] --- is the node supported by the delegate?\n");
        // Only supports 2D convolutions
        if (kTfLiteBuiltinDepthwiseConv2d != registration->builtin_code &&
            kTfLiteBuiltinConv2d != registration->builtin_code)
          return false;
        // This delegate only supports float32 types.
        for (int i = 0; i < node->inputs->size; ++i)
        {
          auto &tensor = context->tensors[node->inputs->data[i]];
          if (tensor.type != kTfLiteFloat32)
            return false;
        }
        LOG("[DELEGATE LOG] --- It seems so\n");
        return true;
      }

      TfLiteStatus Initialize(TfLiteContext *context) override
      {
        
        LOG("[DELEGATE LOG] --- Initializing the delegate interface\n");

        LOG("[DELEGATE LOG] --- Initializing the Systolic Array\n");
        if (TfFiInit(8)){
          return kTfLiteError;  
        }
        return kTfLiteOk;
      }

      const char *Name() const override
      {
        static constexpr char kName[] = "SystolicArrayDelegate";
        return kName;
      }

      std::unique_ptr<SimpleDelegateKernelInterface> CreateDelegateKernelInterface()
          override
      {
        return std::make_unique<SystolicArrayDelegateKernel>(options_);
      }

      SimpleDelegateInterface::Options DelegateOptions() const override
      {
        // Use default options.
        return SimpleDelegateInterface::Options();
      }

    private:
      const SystolicArrayDelegateOptions options_;
    };

  } // namespace SystolicArray_Delegate



TfLiteRegistration GetDelegateKernelRegistration(
    SimpleDelegateInterface* delegate) {
  TfLiteRegistration kernel_registration;
  kernel_registration.profiling_string = nullptr;
  kernel_registration.builtin_code = kTfLiteBuiltinDelegate;
  kernel_registration.custom_name = delegate->Name();
  kernel_registration.version = 1;
  kernel_registration.free = [](TfLiteContext* context, void* buffer) -> void {
    delete reinterpret_cast<SimpleDelegateKernelInterface*>(buffer);
  };
  kernel_registration.init = [](TfLiteContext* context, const char* buffer,
                                size_t length) -> void* {
    const TfLiteDelegateParams* params =
        reinterpret_cast<const TfLiteDelegateParams*>(buffer);
    if (params == nullptr) {
      return nullptr;
    }
    auto* delegate =
        reinterpret_cast<SimpleDelegateInterface*>(params->delegate->data_);
    std::unique_ptr<SimpleDelegateKernelInterface> delegate_kernel(
        delegate->CreateDelegateKernelInterface());
    if (delegate_kernel->Init(context, params) != kTfLiteOk) {
      return nullptr;
    }
    return delegate_kernel.release();
  };
  kernel_registration.prepare = [](TfLiteContext* context,
                                   TfLiteNode* node) -> TfLiteStatus {
    if (node->user_data == nullptr) {
      return kTfLiteError;
    }
    SimpleDelegateKernelInterface* delegate_kernel =
        reinterpret_cast<SimpleDelegateKernelInterface*>(node->user_data);
    return delegate_kernel->Prepare(context, node);
  };
  kernel_registration.invoke = [](TfLiteContext* context,
                                  TfLiteNode* node) -> TfLiteStatus {
    SimpleDelegateKernelInterface* delegate_kernel =
        reinterpret_cast<SimpleDelegateKernelInterface*>(node->user_data);

    return delegate_kernel->Eval(context, node);
  };

  return kernel_registration;
}



  TfLiteStatus Prepare(TfLiteContext *context,
                       struct TfLiteDelegate *base_delegate)
  {
    LOG("[DELEGATE LOG] --- Preparing for execution, execution graph substitution\n");
    auto* delegate =
      reinterpret_cast<SimpleDelegateInterface*>(base_delegate->data_);
  auto delegate_options = delegate->DelegateOptions();
  if (delegate_options.max_delegated_partitions <= 0)
    delegate_options.max_delegated_partitions = std::numeric_limits<int>::max();

  TF_LITE_ENSURE_STATUS(delegate->Initialize(context));
  delegates::IsNodeSupportedFn node_supported_fn =
      [=](TfLiteContext* context, TfLiteNode* node,
          TfLiteRegistration* registration,
          std::string* unsupported_details) -> bool {
    return delegate->IsNodeSupportedByDelegate(registration, node, context);
  };
  // TODO(b/149484598): Update to have method that gets all supported nodes.
  delegates::GraphPartitionHelper helper(context, node_supported_fn);
  TF_LITE_ENSURE_STATUS(helper.Partition(nullptr));

  std::vector<int> supported_nodes = helper.GetNodesOfFirstNLargestPartitions(
      delegate_options.max_delegated_partitions,
      delegate_options.min_nodes_per_partition);
  TfLiteRegistration delegate_kernel_registration =
      GetDelegateKernelRegistration(delegate);

  return context->ReplaceNodeSubsetsWithDelegateKernels(
      context, delegate_kernel_registration,
      BuildTfLiteIntArray(supported_nodes).get(), base_delegate);
}

  TfLiteDelegate *CreateSystolicArrayDelegate(
      std::unique_ptr<SimpleDelegateInterface> simple_delegate, int64_t flag)
  {
    if (simple_delegate == nullptr)
    {
      return nullptr;
    }
    auto delegate = new TfLiteDelegate();
    delegate->Prepare = Prepare;
    delegate->flags = flag;
    delegate->CopyFromBufferHandle = nullptr;
    delegate->CopyToBufferHandle = nullptr;
    delegate->FreeBufferHandle = nullptr;
    delegate->data_ = simple_delegate.release();
    return delegate;
  }

  void DeleteSystolicArrayDelegate(TfLiteDelegate *delegate)
  {
    if (!delegate)
      return;
    SimpleDelegateInterface *simple_delegate =
        reinterpret_cast<SimpleDelegateInterface *>(delegate->data_);
    delete simple_delegate;
    delete delegate;
  }

} // namespace tflite

SystolicArrayDelegateOptions TfLiteSystolicArrayDelegateOptionsDefault()
{
  SystolicArrayDelegateOptions options;
  options.allowed_builtin_code = 0;
  // Report error during init.
  options.error_during_init = 0;
  // Report error during prepare.
  options.error_during_prepare = 0;
  // Report error during invoke.
  options.error_during_invoke = 0;

  // Just assign an invalid builtin code so that this SystolicArray test delegate will
  // not support any node by default.
  options.allowed_builtin_code = -1;
  return options;
}

// Creates a new delegate instance that need to be destroyed with
// `TfLiteSystolicArrayDelegateDelete` when delegate is no longer used by TFLite.
// When `options` is set to `nullptr`, the above default values are used:
TfLiteDelegate *TfLiteSystolicArrayDelegateCreate(const SystolicArrayDelegateOptions *options)
{
  std::unique_ptr<tflite::SystolicArray_Delegate::SystolicArrayDelegate> SystolicArray(
      new tflite::SystolicArray_Delegate::SystolicArrayDelegate(
          options ? *options : TfLiteSystolicArrayDelegateOptionsDefault()));
  return tflite::CreateSystolicArrayDelegate(std::move(SystolicArray), 0);
}

// Destroys a delegate created with `TfLiteSystolicArrayDelegateCreate` call.
void TfLiteSystolicArrayDelegateDelete(TfLiteDelegate *delegate)
{
  TfFiClose();
  tflite::DeleteSystolicArrayDelegate(delegate);
}
