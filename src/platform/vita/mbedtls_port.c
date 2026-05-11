#include <stdlib.h>
#include <time.h>

#include "mbedtls/platform.h"
#include "mbedtls/platform_time.h"
#include <psa/crypto_driver_random.h>

#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/rng.h>
#endif

mbedtls_ms_time_t mbedtls_ms_time(void) {
#ifdef __vita__
    const SceUInt64 usec = sceKernelGetProcessTimeWide();
    return (mbedtls_ms_time_t)(usec / 1000);
#else
    return (mbedtls_ms_time_t)(time(NULL) * 1000);
#endif
}

int mbedtls_platform_get_entropy(psa_driver_get_entropy_flags_t flags,
                                 size_t *estimate_bits,
                                 unsigned char *output,
                                 size_t output_size) {
    if (flags != PSA_DRIVER_GET_ENTROPY_FLAGS_NONE) {
        return PSA_ERROR_NOT_SUPPORTED;
    }

#ifdef __vita__
    if (sceKernelGetRandomNumber(output, output_size) < 0) {
        return PSA_ERROR_HARDWARE_FAILURE;
    }
#else
    for (size_t i = 0; i < output_size; ++i) {
        output[i] = (unsigned char)(rand() & 0xFF);
    }
#endif

    *estimate_bits = output_size * 8;
    return PSA_SUCCESS;
}
