#include <stddef.h>
#include <stdint.h>

typedef struct Object Object;
typedef union ObjectArg ObjectArg;
typedef int32_t (*Invoke)(void *, uint32_t, ObjectArg *, uint32_t);

struct Object {
    Invoke invoke;
    void *context;
};

union ObjectArg {
    struct {
        void *data;
        size_t size;
    } buffer;
    Object object;
};

extern int printf(const char *, ...);
extern int fflush(void *);
extern unsigned int getuid(void);
extern unsigned int alarm(unsigned int);
extern int __system_property_get(const char *, char *);
extern int strcmp(const char *, const char *);
extern size_t strlen(const char *);
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern const char *dlerror(void);

_Static_assert(sizeof(Object) == 16, "Object ABI mismatch");
_Static_assert(sizeof(ObjectArg) == 16, "ObjectArg ABI mismatch");

static void release(Object *object) {
    if (object->invoke) {
        object->invoke(object->context, 0xffffu, NULL, 0);
        object->invoke = NULL;
        object->context = NULL;
    }
}

int main(int argc, char **argv) {
    (void)argv;
    char model[128] = {0};
    char build[128] = {0};
    Object environment = {0};
    Object controller = {0};
    Object application = {0};
    uint32_t uid = 0x7a;
    uint32_t application_kind = 0xffffffffu;
    ObjectArg open_args[2] = {0};
    ObjectArg query_args[3] = {0};
    const char *name = "qcom.tz.uefisecapp";
    int status = 1;

    if (argc != 1 || getuid() != 0) {
        printf("result=root_required_or_invalid_arguments\n");
        return 2;
    }
    __system_property_get("ro.product.model", model);
    __system_property_get("ro.build.display.id", build);
    if (strcmp(model, "PYB110") || strcmp(build, "PYB110_17.0.0.155(CN01)")) {
        printf("result=unsupported_device\n");
        return 2;
    }
    alarm(12);
    printf("probe=CANOE_P1_SERVICE_QUERY_20261026\nmodel=%s\nbuild=%s\n", model, build);
    printf("stage=load_vendor_client\n");
    fflush(NULL);
    void *library = dlopen("/vendor/lib64/libminkdescriptor.so", 2);
    if (!library) {
        printf("result=vendor_library_unavailable\nerror=%s\n", dlerror());
        return 3;
    }
    int32_t (*get_environment)(Object *) = (int32_t (*)(Object *))dlsym(library, "TZCom_getClientEnvObject");
    if (!get_environment) {
        printf("result=vendor_symbol_unavailable\n");
        return 3;
    }
    printf("stage=get_client_environment\n");
    fflush(NULL);
    int32_t result = get_environment(&environment);
    printf("environment_result=%d\n", result);
    if (result || !environment.invoke) {
        printf("result=client_environment_unavailable\n");
        goto done;
    }
    open_args[0].buffer.data = &uid;
    open_args[0].buffer.size = sizeof(uid);
    printf("stage=open_application_controller\n");
    fflush(NULL);
    result = environment.invoke(environment.context, 0, open_args, 0x1001);
    controller = open_args[1].object;
    printf("controller_result=%d\n", result);
    if (result || !controller.invoke) {
        printf("result=application_controller_unavailable\n");
        goto done;
    }
    query_args[0].buffer.data = (void *)name;
    query_args[0].buffer.size = strlen(name);
    query_args[1].buffer.data = &application_kind;
    query_args[1].buffer.size = sizeof(application_kind);
    printf("stage=query_existing_uefi_application\n");
    fflush(NULL);
    result = controller.invoke(controller.context, 2, query_args, 0x1011);
    application = query_args[2].object;
    printf("application_result=%d\napplication_kind=%u\napplication_object_present=%d\n", result, application_kind, application.invoke != NULL);
    if (result || !application.invoke) {
        printf("result=existing_uefi_application_unavailable\n");
        goto done;
    }
    printf("result=existing_uefi_application_found\nvariable_commands_sent=0\n");
    status = 0;
done:
    fflush(NULL);
    release(&application);
    release(&controller);
    release(&environment);
    alarm(0);
    return status;
}
