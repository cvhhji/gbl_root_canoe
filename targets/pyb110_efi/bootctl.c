#include <stddef.h>
#include <stdint.h>

extern int printf(const char *, ...);
extern int fflush(void *);
extern unsigned int getuid(void);
extern unsigned int alarm(unsigned int);
extern int __system_property_get(const char *, char *);
extern int strcmp(const char *, const char *);
extern size_t strlen(const char *);
extern void *memcpy(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern const char *dlerror(void);
extern void *malloc(size_t);
extern void free(void *);
extern int memcmp(const void *, const void *, size_t);
extern int open(const char *, int, ...);
extern int close(int);
extern long read(int, void *, size_t);
extern long write(int, const void *, size_t);
extern long pread(int, void *, size_t, long);
extern long lseek(int, long, int);
extern int fsync(int);
extern char *realpath(const char *, char *);
extern int getppid(void);
extern unsigned int sleep(unsigned int);
typedef void (*SignalHandler)(int);
extern SignalHandler signal(int, SignalHandler);

static uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }
static uint64_t le64(const uint8_t *p) { return le32(p) | (uint64_t)le32(p + 4) << 32; }
static void put16(uint8_t *p, uint16_t x) { p[0] = x; p[1] = x >> 8; }
static void put32(uint8_t *p, uint32_t x) { put16(p, x); put16(p + 2, x >> 16); }

static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffff;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (unsigned int j = 0; j < 8; j++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}

static void fat_set(uint8_t *table, uint16_t cluster, uint16_t value) {
    unsigned int offset = cluster + cluster / 2;
    uint16_t pair = le16(table + offset);
    put16(table + offset, cluster & 1 ? (pair & 15) | value << 4 : (pair & 0xf000) | value);
}

static void directory(uint8_t *p, const char *name, uint8_t attributes, uint16_t cluster, uint32_t size) {
    memcpy(p, name, 11); p[11] = attributes;
    put16(p + 26, cluster); put32(p + 28, size);
}

int CanoeBuildCarrier(const uint8_t *original, size_t length, const uint8_t *bds, size_t bds_size, uint8_t *image) {
    const size_t reserved = 24 * 1024 * 1024;
    const unsigned int sector = 4096, start = 6150, total = 8192;
    if (length != 32 * 1024 * 1024 || memcmp(original + 32768, "\1CD001\1", 7)) return 1;
    for (size_t i = 0; i < 512; i++) if (original[i]) return 2;
    uint32_t blocks = le32(original + 32768 + 80);
    uint32_t big = (uint32_t)original[32852] << 24 | (uint32_t)original[32853] << 16 | (uint32_t)original[32854] << 8 | original[32855];
    if (blocks != big || le16(original + 32896) != 2048 || original[32898] != 8 || original[32899] != 0 || blocks < 17 || blocks > reserved / 2048) return 3;
    for (size_t i = reserved; i < length; i++) if (original[i]) return 4;
    if (bds_size < 512 || bds_size > 2 * 1024 * 1024 || memcmp(bds, "MZ", 2)) return 5;
    size_t count = (bds_size + sector - 1) / sector;
    if (count + 2 > total - start) return 6;
    memcpy(image, original, length);
    memset(image + reserved, 0, length - reserved);
    uint8_t *b = image;
    memcpy(b, "\xeb\x3c\x90" "CANOETST", 11);
    put16(b + 11, sector); b[13] = 1; put16(b + 14, 6144); b[16] = 2;
    put16(b + 17, 512); put16(b + 19, total); b[21] = 0xf8; put16(b + 22, 1);
    put16(b + 24, 63); put16(b + 26, 255); put32(b + 28, 105920);
    b[36] = 0x80; b[38] = 0x29; put32(b + 39, crc32(bds, bds_size));
    memcpy(b + 43, "CANOETEST  ", 11); memcpy(b + 54, "FAT12   ", 8); b[510] = 0x55; b[511] = 0xaa;
    uint8_t *fat = image + reserved, *root = image + reserved + 2 * sector;
    memcpy(fat, "\xf8\xff\xff", 3);
    uint16_t cluster = 2;
    directory(root, "BDS     EFI", 0x20, cluster, (uint32_t)bds_size); root += 32;
    memcpy(image + (size_t)start * sector, bds, bds_size);
    for (size_t j = 0; j < count; j++, cluster++) fat_set(fat, cluster, j + 1 == count ? 0xfff : cluster + 1);
    directory(root, "SETTINGSBIN", 0x20, cluster, 3072); root += 32; fat_set(fat, cluster++, 0xfff);
    directory(root, "CANOETST   ", 0x20, cluster, 10);
    memcpy(image + ((size_t)start + cluster - 2) * sector, "CANOETEST1", 10); fat_set(fat, cluster, 0xfff);
    memcpy(fat + sector, fat, sector);
    return memcmp(image + 512, original + 512, reserved - 512) ? 7 : 0;
}

int CanoeBuildOption(uint8_t *header, const uint8_t *table, size_t table_size, uint8_t *out, size_t *size) {
    if (memcmp(header, "EFI PART", 8) || le32(header + 12) < 92 || le32(header + 12) > 4096 || le64(header + 24) != 1) return 1;
    uint32_t saved = le32(header + 16); put32(header + 16, 0);
    uint32_t actual = crc32(header, le32(header + 12)); put32(header + 16, saved);
    if (actual != saved || le32(header + 84) < 128 || le32(header + 84) > 4096 || le32(header + 80) > 512) return 2;
    if ((uint64_t)le32(header + 80) * le32(header + 84) != table_size || crc32(table, table_size) != le32(header + 88)) return 3;
    const uint8_t *entry = NULL; uint32_t number = 0;
    for (uint32_t i = 0; i < le32(header + 80); i++) {
        const uint8_t *e = table + (size_t)i * le32(header + 84);
        const uint8_t name[] = {'D',0,'R',0,'I',0,'V',0,'E',0,'R',0,0,0};
        if (!memcmp(e + 56, name, sizeof(name))) { if (entry) return 4; entry = e; number = i + 1; }
    }
    const uint8_t type[] = {0x38,0x68,0x4a,0,0x2a,6,0xdf,0x44,0x81,0x52,0x4f,0x34,0x0c,5,0x22,0x56};
    if (!entry || number != 11 || memcmp(entry, type, 16) || le64(entry + 32) != 105920 || le64(entry + 40) != 114111 || le64(entry + 48)) return 5;
    const char *description = "Canoe EFI test", *path = "\\BDS.EFI";
    size_t descbytes = (strlen(description) + 1) * 2, pathbytes = (strlen(path) + 1) * 2;
    size_t total = 6 + descbytes + 42 + 4 + pathbytes + 4;
    if (*size < total) return 6;
    memset(out, 0, total); put32(out, 1); put16(out + 4, (uint16_t)(42 + 4 + pathbytes + 4));
    for (size_t i = 0; i < strlen(description); i++) put16(out + 6 + i * 2, description[i]);
    uint8_t *p = out + 6 + descbytes;
    p[0] = 4; p[1] = 1; put16(p + 2, 42); put32(p + 4, number); memcpy(p + 8, entry + 32, 8);
    uint64_t sectors = 8192; memcpy(p + 16, &sectors, 8); memcpy(p + 24, entry + 16, 16); p[40] = 2; p[41] = 2;
    p += 42; p[0] = 4; p[1] = 4; put16(p + 2, (uint16_t)(4 + pathbytes));
    for (size_t i = 0; i < strlen(path); i++) put16(p + 4 + i * 2, path[i]);
    p += 4 + pathbytes; memcpy(p, "\x7f\xff\4\0", 4); *size = total;
    return 0;
}

#ifndef CANOE_HOST_TEST

static uint8_t *read_file(const char *path, size_t *size, size_t maximum) {
    int fd = open(path, 0x80000 | 0x20000);
    if (fd < 0) return NULL;
    long length = lseek(fd, 0, 2);
    uint8_t *data = NULL;
    if (length < 1 || (uint64_t)length > maximum || lseek(fd, 0, 0) != 0) goto done;
    data = malloc((size_t)length);
    if (!data) goto done;
    size_t used = 0;
    while (used < (size_t)length) {
        long got = read(fd, data + used, (size_t)length - used);
        if (got <= 0) { free(data); data = NULL; goto done; }
        used += got;
    }
    *size = used;
done:
    close(fd); return data;
}

static int write_file(const char *path, const void *data, size_t size) {
    int fd = open(path, 1 | 0x40 | 0x80 | 0x80000 | 0x20000, 0600);
    if (fd < 0) return 1;
    size_t used = 0;
    while (used < size) {
        long n = write(fd, (const uint8_t *)data + used, size - used);
        if (n <= 0) { close(fd); return 1; }
        used += n;
    }
    int r = fsync(fd); if (close(fd)) r = -1; return r != 0;
}

typedef struct { uint8_t *buffer; } QseeHandle;
static QseeHandle *service;
static int (*qsee_send)(QseeHandle *, void *, uint32_t, void *, uint32_t);
static int (*qsee_shutdown)(QseeHandle **);
static const uint8_t global_guid[16] = {0x61,0xdf,0xe4,0x8b,0xca,0x93,0xd2,0x11,0xaa,0x0d,0,0xe0,0x98,3,0x2b,0x8c};
static const char lookup_only_path[] = "/canoe_attach_only/" "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
enum { SHARED_SIZE = 16384, MAX_VARIABLE_SIZE = 8192 };
#define VARIABLE_NOT_FOUND ((int)0x8000000eu)
_Static_assert(sizeof(lookup_only_path) - 1 > 80, "The absent-app path must reject before any file load");

typedef struct {
    uint64_t flags;
    void *address;
    size_t size;
    int relro_fd, library_fd;
    int64_t offset;
    void *namespace;
} LibraryExtInfo;

_Static_assert(sizeof(LibraryExtInfo) == 48, "Android linker ABI mismatch");

static const char *linker_error(void) {
    const char *error = dlerror();
    return error ? error : "Linker did not return an error";
}

static int connect_service(void) {
    void *support = dlopen("/system/lib64/libvndksupport.so", 2);
    if (!support) { printf("vendor_loader_error=%s\n", linker_error()); return 1; }
    void *(*get_namespace)(const char *) = (void *(*)(const char *))dlsym(support, "android_get_exported_namespace");
    void *(*load_ext)(const char *, int, const LibraryExtInfo *) = (void *(*)(const char *, int, const LibraryExtInfo *))dlsym(support, "android_dlopen_ext");
    if (!get_namespace || !load_ext) { printf("vendor_loader_symbol_error=%s\n", linker_error()); return 1; }
    LibraryExtInfo info = {0};
    info.flags = 0x200;
    info.namespace = get_namespace("sphal");
    if (!info.namespace) { printf("vendor_namespace_missing=sphal\n"); return 1; }
    void *library = load_ext("/vendor/lib64/libQSEEComAPI_compat.so", 2, &info);
    if (!library) { printf("vendor_library_error=%s\n", linker_error()); return 1; }
    int (*start)(QseeHandle **, const char *, const char *, uint32_t) = (int (*)(QseeHandle **, const char *, const char *, uint32_t))dlsym(library, "QSEECom_start_app_impl");
    qsee_send = (int (*)(QseeHandle *, void *, uint32_t, void *, uint32_t))dlsym(library, "QSEECom_send_cmd_impl");
    qsee_shutdown = (int (*)(QseeHandle **))dlsym(library, "QSEECom_shutdown_app_impl");
    if (!start || !qsee_send || !qsee_shutdown) { printf("vendor_service_symbol_error=%s\n", linker_error()); return 1; }
    int r = start(&service, lookup_only_path, "qcom.tz.uefisecapp", SHARED_SIZE);
    printf("attach_existing=%d handle=%d buffer=%d\n", r, service != NULL, service && service->buffer != NULL);
    if (r || !service || !service->buffer) {
        printf("Normal reboot required; the original variable service is unavailable.\n");
        return 1;
    }
    return 0;
}

static uint32_t *request_variable(const char *name, uint32_t command, uint32_t header) {
    size_t length = strlen(name);
    if (!service || !service->buffer || length > 126 || !length) return NULL;
    memset(service->buffer, 0, SHARED_SIZE);
    uint32_t *request = (uint32_t *)service->buffer;
    request[0] = command;
    request[2] = header;
    request[3] = (uint32_t)(length + 1) * 2;
    request[4] = header + request[3];
    request[5] = 16;
    uint16_t *wide = (uint16_t *)(service->buffer + header);
    for (size_t i = 0; i < length; i++) wide[i] = (uint8_t)name[i];
    memcpy(service->buffer + request[4], global_guid, sizeof(global_guid));
    request[1] = request[4] + 16;
    return request;
}

static int exchange(uint32_t *request, uint32_t capacity, uint32_t **reply) {
    uint32_t input_size = (request[1] + 63) & ~63u;
    uint32_t offset = input_size < 512 ? 512 : input_size;
    capacity = (capacity + 63) & ~63u;
    if (offset > SHARED_SIZE || capacity > SHARED_SIZE - offset) return -1;
    *reply = (uint32_t *)(service->buffer + offset);
    return qsee_send(service, request, input_size, *reply, capacity);
}

static int get_variable(const char *name, uint8_t *data, uint32_t *size, uint32_t *attributes) {
    if (*size > MAX_VARIABLE_SIZE || (*size && !data)) return -1;
    uint32_t *request = request_variable(name, 0x8000, 28), *response = NULL;
    if (!request) return -1;
    request[6] = *size;
    uint32_t capacity = (24 + *size + 63) & ~63u;
    int r = exchange(request, capacity, &response);
    printf("get %s: transport=%d status=%08x\n", name, r, response ? response[2] : 0xffffffffu);
    fflush(NULL);
    if (r || !response || response[0] != 0x8000 || response[1] < 24 || response[1] > capacity) return -1;
    if (response[2]) return (int)response[2];
    if (response[4] < 24 || response[4] > response[1] || response[5] > response[1] - response[4] || response[5] > *size) return -1;
    memcpy(data, (uint8_t *)response + response[4], response[5]);
    *size = response[5];
    *attributes = response[3];
    return 0;
}

static int set_variable(const char *name, const uint8_t *data, uint32_t size, uint32_t attributes) {
    if (strcmp(name, "BootNext") && strcmp(name, "Boot6006")) return -1;
    if (size > 512 || (size ? !data || attributes != 7 : attributes != 0)) return -1;
    if (!strcmp(name, "BootNext") && size && (size != 2 || le16(data) != 0x6006)) return -1;
    uint32_t *request = request_variable(name, 0x8001, 36), *response = NULL;
    if (!request) return -1;
    request[6] = attributes;
    request[7] = request[1];
    request[8] = size;
    if (size) memcpy(service->buffer + request[7], data, size);
    request[1] += size;
    int r = exchange(request, 64, &response);
    printf("set %s: transport=%d status=%08x\n", name, r, response ? response[2] : 0xffffffffu);
    fflush(NULL);
    if (r || !response || response[0] != 0x8001 || response[1] < 20 || response[1] > 64) return -1;
    return (int)response[2];
}

static int unsigned_boot_allowed(void) {
    uint8_t data[1]; uint32_t size = 1, attributes = 0;
    if (get_variable("SecureBoot", data, &size, &attributes) || size != 1 || data[0]) return 0;
    size = 1;
    return !get_variable("SetupMode", data, &size, &attributes) && size == 1 && data[0] == 1;
}

static volatile int stop_requested;

static void stop_service(int number) { (void)number; stop_requested = 1; }

static int hold_service(const char *ready) {
    int parent = getppid();
    signal(1, stop_service); signal(2, stop_service); signal(15, stop_service);
    if (parent <= 1 || write_file(ready, "ready\n", 6)) return 1;
    alarm(0);
    while (!stop_requested && getppid() == parent) sleep(1);
    return 0;
}

static int absent(const char *name) {
    uint8_t data[8192]; uint32_t size = sizeof(data), attributes;
    return get_variable(name, data, &size, &attributes) == VARIABLE_NOT_FOUND;
}

static int equals(const char *name, const uint8_t *expected, size_t length) {
    uint8_t data[8192]; uint32_t size = sizeof(data), attributes;
    return !get_variable(name, data, &size, &attributes) && attributes == 7 && size == length && !memcmp(data, expected, length);
}

static int one_shot_only(void) {
    uint8_t order[8192]; uint32_t size = sizeof(order), attributes;
    int r = get_variable("BootOrder", order, &size, &attributes);
    if (r == VARIABLE_NOT_FOUND) return 1;
    if (r || attributes != 7 || !size || size % 2) return 0;
    for (uint32_t i = 0; i < size; i += 2) if (le16(order + i) == 0x6006) return 0;
    return 1;
}

static int save_variable(const char *name, const char *directory_path) {
    uint8_t data[8200] = {0}; uint32_t size = sizeof(data) - 8, attributes = 0;
    int r = get_variable(name, data + 8, &size, &attributes);
    if (r != 0 && r != VARIABLE_NOT_FOUND) return 1;
    put32(data, r == VARIABLE_NOT_FOUND ? 0xffffffff : attributes);
    put32(data + 4, r == VARIABLE_NOT_FOUND ? 0 : size);
    char path[4096]; size_t dirbytes = strlen(directory_path), namebytes = strlen(name);
    if (dirbytes + namebytes + 6 >= sizeof(path)) return 1;
    memcpy(path, directory_path, dirbytes); path[dirbytes++] = '/';
    memcpy(path + dirbytes, name, namebytes); memcpy(path + dirbytes + namebytes, ".bin", 5);
    return write_file(path, data, r == VARIABLE_NOT_FOUND ? 8 : size + 8);
}

static int disarm(const uint8_t *option, size_t length) {
    const uint8_t next[] = {6, 0x60};
    if (equals("BootNext", next, 2)) {
        if (set_variable("BootNext", NULL, 0, 0) || !absent("BootNext")) return 1;
    } else if (!absent("BootNext")) {
        uint8_t data[2]; uint32_t size = 2, attr;
        if (get_variable("BootNext", data, &size, &attr) || size != 2) return 1;
        printf("Leaving another BootNext unchanged.\n");
    }
    if (absent("Boot6006")) return 0;
    if (!equals("Boot6006", option, length)) return 1;
    return set_variable("Boot6006", NULL, 0, 0) || !absent("Boot6006");
}

static int load_option(const char *disk, uint8_t *option, size_t *bytes) {
    int fd = open(disk, 0x80000 | 0x20000);
    uint8_t header[4096]; uint8_t *table = NULL; int result = 1;
    if (fd < 0) return 1;
    if (pread(fd, header, sizeof(header), 4096) != sizeof(header) || memcmp(header, "EFI PART", 8)) goto done;
    uint32_t count = le32(header + 80), stride = le32(header + 84);
    if (!count || count > 512 || stride < 128 || stride > 4096 || le64(header + 72) > 256) goto done;
    size_t size = (size_t)count * stride;
    table = malloc(size); if (!table) goto done;
    if (pread(fd, table, size, (long)(le64(header + 72) * 4096)) != (long)size) goto done;
    result = CanoeBuildOption(header, table, size, option, bytes);
done:
    free(table); close(fd); return result;
}

static int make_option(const char *disk, const char *output) {
    uint8_t option[512]; size_t bytes = sizeof(option);
    return load_option(disk, option, &bytes) || write_file(output, option, bytes);
}

static int validate_option(const uint8_t *option, size_t bytes) {
    uint8_t current[512]; size_t size = sizeof(current);
    return load_option("/dev/block/sdf", current, &size) || bytes != size || memcmp(option, current, bytes);
}

static int make_image(char **paths) {
    size_t size = 0, bds_size = 0; uint8_t *original = NULL, *bds = NULL, *image = NULL; int result = 1;
    original = read_file(paths[0], &size, 32 * 1024 * 1024);
    if (!original) goto done;
    bds = read_file(paths[1], &bds_size, 2 * 1024 * 1024); if (!bds) goto done;
    image = malloc(size); if (!image) goto done;
    int r = CanoeBuildCarrier(original, size, bds, bds_size, image);
    printf("carrier_layout=%d\n", r);
    if (r) goto done;
    result = write_file(paths[2], image, size);
done:
    free(original); free(image); free(bds); return result;
}

int main(int argc, char **argv) {
    char model[128] = {0}, build[128] = {0};
    if (argc < 2 || getuid() != 0) return 2;
    __system_property_get("ro.product.model", model);
    __system_property_get("ro.build.display.id", build);
    if (strcmp(model, "PYB110") || strcmp(build, "PYB110_17.0.0.155(CN01)")) return 2;
    if (!strcmp(argv[1], "option") && argc == 4) return make_option(argv[2], argv[3]);
    if (!strcmp(argv[1], "image") && argc == 5) return make_image(argv + 2);
    if (!strcmp(argv[1], "verify-option") && argc == 3) {
        size_t size = 0; uint8_t *option = read_file(argv[2], &size, 512);
        int r = !option || validate_option(option, size); free(option); return r;
    }
    int info = !strcmp(argv[1], "info") && argc == 2;
    int preflight = !strcmp(argv[1], "preflight") && argc == 2;
    int hold = !strcmp(argv[1], "hold") && argc == 3;
    int reserve = !strcmp(argv[1], "reserve") && argc == 3;
    int arm = !strcmp(argv[1], "arm") && argc == 3;
    int remove = !strcmp(argv[1], "disarm") && argc == 3;
    int snapshot = !strcmp(argv[1], "snapshot") && argc == 3;
    if (!hold && !info && !preflight && !reserve && !arm && !remove && !snapshot) return 2;
    alarm(20);
    printf("command=%s\n", argv[1]);
    fflush(NULL);
    int result = 1;
    if (connect_service()) goto done;
    if (hold) { result = hold_service(argv[2]); goto done; }
    if (snapshot) {
        if (save_variable("BootOrder", argv[2]) || save_variable("BootNext", argv[2]) || save_variable("Boot6006", argv[2]) || save_variable("SecureBoot", argv[2]) || save_variable("SetupMode", argv[2])) goto done;
        result = 0; goto done;
    }
    if (reserve || arm || remove) {
        size_t length = 0; uint8_t *option = read_file(argv[2], &length, 512);
        if (!option || validate_option(option, length)) { free(option); goto done; }
        const uint8_t next[] = {6, 0x60};
        if (reserve) {
            if (unsigned_boot_allowed() && one_shot_only() && absent("BootNext") && absent("Boot6006") && !set_variable("Boot6006", option, length, 7) && equals("Boot6006", option, length)) result = 0;
        } else if (arm) {
            if (unsigned_boot_allowed() && one_shot_only() && equals("Boot6006", option, length) && absent("BootNext") && !set_variable("BootNext", next, 2, 7) && equals("BootNext", next, 2)) result = 0;
        } else result = disarm(option, length);
        free(option); goto done;
    }
    const char *names[] = {"SecureBoot", "SetupMode", "BootNext", "BootOrder"};
    unsigned int found = 0;
    for (unsigned int i = 0; i < 4; i++) {
        uint8_t data[8192] = {0}; uint32_t size = sizeof(data), attributes = 0;
        int r = get_variable(names[i], data, &size, &attributes);
        if (!r) {
            found++;
            printf("%s=", names[i]);
            for (uint32_t j = 0; j < size; j++) printf("%02x", data[j]);
            printf("\n");
        }
    }
    result = info ? !found : !(unsigned_boot_allowed() && one_shot_only() && absent("BootNext") && absent("Boot6006"));
done:
    if (service && qsee_shutdown) {
        int r = qsee_shutdown(&service);
        printf("shutdown=%d handle=%d\n", r, service != NULL);
        if (r) result = 1;
    }
    alarm(0);
    fflush(NULL);
    return result;
}

#endif
