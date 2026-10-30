#include "inference.h"
#include "model_data.h"
#include "model_params.h"

#include <math.h>
#include <zephyr/sys/printk.h>
#include <cmsis_core.h>

#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
#include <tensorflow/lite/schema/schema_generated.h>

namespace {
    /* Memory arena for the activations and intermediate tensors:
     * the simple planner needs ~10.5 KB for this graph, 14 KB leaves
     * margin for future model changes
     */
    constexpr int kTensorArenaSize = 14 * 1024;
    alignas(16) uint8_t tensor_arena[kTensorArenaSize];

    const tflite::Model* model = nullptr;
    tflite::MicroInterpreter* interpreter = nullptr;
    TfLiteTensor* input_tensor = nullptr;
    TfLiteTensor* output_tensor = nullptr;

    /* Cycles of the last Invoke, as counted by the DWT */
    uint32_t last_cycles = 0;
}

static inline int8_t quantize_sample(float val)
{
    int32_t q = static_cast<int32_t>(roundf(val * kInputScaleInv)) + kInputZeroPoint;
    if (q < -128) return -128;
    if (q > 127)  return 127;
    return static_cast<int8_t>(q);
}

extern "C" int inference_init(void)
{
    model = tflite::GetModel(g_model);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        printk("TFLM: Schema version mismatch (%d vs %d)\n", 
               model->version(), TFLITE_SCHEMA_VERSION);
        return -1;
    }

    static tflite::MicroMutableOpResolver<8> resolver;
    resolver.AddConv2D();
    resolver.AddMaxPool2D();
    resolver.AddMean();
    resolver.AddFullyConnected();
    resolver.AddSoftmax();
    resolver.AddReshape();
    resolver.AddExpandDims();  

    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, kTensorArenaSize);
    interpreter = &static_interpreter;

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        printk("TFLM: AllocateTensors failed (missing op registration or arena too small)\n");
        return -2;
    }

    input_tensor = interpreter->input(0);
    output_tensor = interpreter->output(0);

    /* Arm the DWT cycle counter: TRCENA first, then the counter, which
     * ticks once per CPU clock cycle (96 MHz)
     */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    printk("TFLM: init done, tensor arena used: %u bytes\n",
           interpreter->arena_used_bytes());

    return 0;
}

extern "C" int inference_run(const float raw_data[CAPTURE_SAMPLES][IMU_CHANNELS], 
                            inference_result_t *result)
{
    if (!interpreter || !input_tensor || !output_tensor) {
        return -1;
    }

    /* 1. Preprocessing: normalization + int8 quantization */
    int8_t* in_ptr = input_tensor->data.int8;
    size_t flat_idx = 0;

    for (size_t i = 0; i < CAPTURE_SAMPLES; i++) {
        for (size_t c = 0; c < IMU_CHANNELS; c++) {
            float normalized = (raw_data[i][c] - norm_mean[c]) / norm_std[c];
            in_ptr[flat_idx++] = quantize_sample(normalized);
        }
    }

    /* 2. Inference, timed by the DWT cycle counter */
    uint32_t t0 = DWT->CYCCNT;
    if (interpreter->Invoke() != kTfLiteOk) {
        printk("TFLM: Invoke failed\n");
        return -2;
    }
    last_cycles = DWT->CYCCNT - t0;

    /* 3. Output de-quantization */
    const int8_t* out_ptr = output_tensor->data.int8;
    result->prob_A = (static_cast<int32_t>(out_ptr[0]) - kOutputZeroPoint) * kOutputScale;
    result->prob_IDLE = (static_cast<int32_t>(out_ptr[1]) - kOutputZeroPoint) * kOutputScale;

    result->predicted_class = (result->prob_A >= result->prob_IDLE) ? 0 : 1;

    return 0;
}

uint32_t inference_last_cycles(void)
{
    return last_cycles;
}