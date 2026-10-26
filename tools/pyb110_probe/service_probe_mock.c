#define main ProbeMain
#include "service_probe.c"
#undef main

static unsigned int test_case;
static unsigned int faults;
static unsigned int environment_calls;
static unsigned int controller_calls;
static unsigned int query_calls;
static unsigned int release_calls;

void *memset(void *destination, int value, size_t size) {
    unsigned char *out = destination;
    for (size_t i = 0; i < size; ++i) out[i] = (unsigned char)value;
    return destination;
}

size_t strlen(const char *value) {
    size_t size = 0;
    while (value[size]) ++size;
    return size;
}

int strcmp(const char *left, const char *right) {
    while (*left && *left == *right) { ++left; ++right; }
    return (unsigned char)*left - (unsigned char)*right;
}

int printf(const char *format, ...) { (void)format; return 0; }
int fflush(void *stream) { (void)stream; return 0; }
unsigned int getuid(void) { return test_case == 5 ? 2000 : 0; }
unsigned int alarm(unsigned int seconds) { (void)seconds; return 0; }

int __system_property_get(const char *name, char *value) {
    const char *source;
    if (!strcmp(name, "ro.product.model")) source = test_case == 6 ? "OTHER" : "PYB110";
    else source = test_case == 7 ? "OTHER" : "PYB110_17.0.0.155(CN01)";
    size_t size = strlen(source);
    for (size_t i = 0; i <= size; ++i) value[i] = source[i];
    return (int)size;
}

static int32_t application_invoke(void *context, uint32_t operation, ObjectArg *args, uint32_t counts) {
    if (context != (void *)3 || operation != 0xffffu || args || counts) faults |= 1;
    ++release_calls;
    return 0;
}

static int32_t controller_invoke(void *context, uint32_t operation, ObjectArg *args, uint32_t counts) {
    if (context != (void *)2) faults |= 2;
    if (operation == 0xffffu) {
        if (args || counts) faults |= 4;
        ++release_calls;
        return 0;
    }
    ++query_calls;
    if (operation != 2 || counts != 0x1011 || !args || args[0].buffer.size != strlen("qcom.tz.uefisecapp") || strcmp(args[0].buffer.data, "qcom.tz.uefisecapp") || args[1].buffer.size != 4 || args[2].object.invoke) {
        faults |= 8;
        return -1;
    }
    if (test_case == 4) return 23;
    *(uint32_t *)args[1].buffer.data = 1;
    args[2].object = (Object){application_invoke, (void *)3};
    return 0;
}

static int32_t environment_invoke(void *context, uint32_t operation, ObjectArg *args, uint32_t counts) {
    if (context != (void *)1) faults |= 16;
    if (operation == 0xffffu) {
        if (args || counts) faults |= 32;
        ++release_calls;
        return 0;
    }
    ++controller_calls;
    if (operation != 0 || counts != 0x1001 || !args || args[0].buffer.size != 4 || *(uint32_t *)args[0].buffer.data != 0x7a || args[1].object.invoke) {
        faults |= 64;
        return -1;
    }
    if (test_case == 3) return -32;
    args[1].object = (Object){controller_invoke, (void *)2};
    return 0;
}

static int32_t get_environment_mock(Object *object) {
    ++environment_calls;
    if (test_case == 2) return -1;
    if (test_case != 9) *object = (Object){environment_invoke, (void *)1};
    return 0;
}

void *dlopen(const char *name, int flags) {
    if (strcmp(name, "/vendor/lib64/libminkdescriptor.so") || flags != 2) faults |= 128;
    return test_case == 1 ? NULL : (void *)4;
}

void *dlsym(void *library, const char *name) {
    if (library != (void *)4 || strcmp(name, "TZCom_getClientEnvObject")) faults |= 256;
    return test_case == 8 ? NULL : (void *)get_environment_mock;
}

const char *dlerror(void) { return "mock loader error"; }

#if defined(_WIN32)
#define PROBE_EXPORT __declspec(dllexport)
#else
#define PROBE_EXPORT __attribute__((visibility("default")))
#endif

PROBE_EXPORT unsigned int RunEnvProbeTest(unsigned int number) {
    test_case = number;
    faults = environment_calls = controller_calls = query_calls = release_calls = 0;
    int status = ProbeMain(number == 10 ? 2 : 1, NULL);
    int expected_status = number == 0 ? 0 : (number == 1 || number == 8 ? 3 : (number == 5 || number == 6 || number == 7 || number == 10 ? 2 : 1));
    if (status != expected_status) faults |= 512;
    if ((number == 1 || number == 5 || number == 6 || number == 7 || number == 8 || number == 10) && (environment_calls || controller_calls || query_calls || release_calls)) faults |= 1024;
    if ((number == 2 || number == 9) && (controller_calls || query_calls || release_calls)) faults |= 2048;
    if (number == 3 && (query_calls || release_calls != 1)) faults |= 4096;
    if (number == 4 && release_calls != 2) faults |= 8192;
    if (number == 0 && (environment_calls != 1 || controller_calls != 1 || query_calls != 1 || release_calls != 3)) faults |= 16384;
    return faults;
}
