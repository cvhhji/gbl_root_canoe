#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir_p(d) _mkdir(d)
#define PATH_SEP '\\'
#else
#include <unistd.h>
#include <sys/stat.h>
#define mkdir_p(d) mkdir(d, 0755)
#define PATH_SEP '/'
#endif

#define AVB_MAGIC "AVB0"
#define AVB_VBMETA_IMAGE_HEADER_SIZE 256
#define AVB_FOOTER_MAGIC "AVBf"
#define AVB_FOOTER_SIZE 64

#define MAX_PATH_LEN 512
#define MAX_CMD_LEN 1024
#define MAX_INPUT_LEN 256

static const char *fastboot_path = "fastboot";
static const char *adb_path = "adb";

/* ---- Endian Conversion ---- */
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static uint64_t be64(const uint8_t *p) {
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (v >> 24) & 0xff;
    p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8)  & 0xff;
    p[3] = v & 0xff;
}

static void put_be64(uint8_t *p, uint64_t v) {
    put_be32(p, (uint32_t)(v >> 32));
    put_be32(p + 4, (uint32_t)v);
}

/* ---- File Read & Write ---- */
static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *buf = malloc(sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = sz;
    return buf;
}

static int write_file(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (fwrite(data, 1, size, f) != size) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* ---- Get Executable Directory ---- */
static int get_exe_dir(char *buf, size_t buf_size) {
#ifdef _WIN32
    extern unsigned long __stdcall GetModuleFileNameA(void*, char*, unsigned long);
    unsigned long len = GetModuleFileNameA(NULL, buf, (unsigned long)buf_size);
    if (len == 0 || len >= buf_size) return -1;
#else
    ssize_t len = readlink("/proc/self/exe", buf, buf_size - 1);
    if (len <= 0) return -1;
    buf[len] = '\0';
#endif
    char *last_sep = strrchr(buf, PATH_SEP);
#ifdef _WIN32
    if (!last_sep) last_sep = strrchr(buf, '/');
#endif
    if (last_sep)
        *last_sep = '\0';
    else
        strcpy(buf, ".");
    return 0;
}

/* ---- AVB Footer Operations ---- */
static int read_avb_footer(const uint8_t *data, size_t len,
                           uint64_t *original_size, uint64_t *vbmeta_offset,
                           uint64_t *vbmeta_size) {
    if (len < AVB_FOOTER_SIZE)
        return 0;
    const uint8_t *footer = data + len - AVB_FOOTER_SIZE;
    if (memcmp(footer, AVB_FOOTER_MAGIC, 4) != 0)
        return 0;
    *original_size = be64(footer + 12);
    *vbmeta_offset = be64(footer + 20);
    *vbmeta_size   = be64(footer + 28);
    return 1;
}

static void create_avb_footer(uint8_t *footer,
                               uint64_t original_size, uint64_t vbmeta_offset,
                               uint64_t vbmeta_size) {
    memset(footer, 0, AVB_FOOTER_SIZE);
    memcpy(footer, AVB_FOOTER_MAGIC, 4);
    put_be32(footer + 4, 1);
    put_be32(footer + 8, 0);
    put_be64(footer + 12, original_size);
    put_be64(footer + 20, vbmeta_offset);
    put_be64(footer + 28, vbmeta_size);
}

static int transplant_vbmeta(const char *vbmeta_path, const char *source_image,
                              const char *output_path) {
    size_t vbmeta_size;
    uint8_t *vbmeta_data = read_file(vbmeta_path, &vbmeta_size);
    if (!vbmeta_data) {
        fprintf(stderr, "Failed to read VBMeta: %s\n", vbmeta_path);
        return -1;
    }
    size_t target_size;
    uint8_t *target_data = read_file(source_image, &target_size);
    if (!target_data) {
        fprintf(stderr, "Failed to read image: %s\n", source_image);
        free(vbmeta_data);
        return -1;
    }
    uint64_t original_size;
    uint64_t existing_offset, existing_size;
    if (read_avb_footer(target_data, target_size,
                        &original_size, &existing_offset, &existing_size)) {
        printf("  Existing AVB Footer detected, original data size: %llu\n",
               (unsigned long long)original_size);
    } else {
        original_size = target_size - vbmeta_size - AVB_FOOTER_SIZE;
        printf("  No AVB Footer found, calculated original data size: %llu\n",
               (unsigned long long)original_size);
    }
    uint64_t vbmeta_offset = original_size;
    uint64_t footer_offset = target_size - AVB_FOOTER_SIZE;
    uint64_t required = original_size + vbmeta_size + AVB_FOOTER_SIZE;
    if (required > target_size) {
        fprintf(stderr, "Insufficient space: required %llu bytes, current %llu bytes\n",
                (unsigned long long)required, (unsigned long long)target_size);
        free(vbmeta_data);
        free(target_data);
        return -1;
    }
    uint8_t *output = calloc(1, target_size);
    if (!output) {
        free(vbmeta_data);
        free(target_data);
        return -1;
    }
    memcpy(output, target_data, (size_t)original_size);
    memcpy(output + vbmeta_offset, vbmeta_data, vbmeta_size);
    create_avb_footer(output + footer_offset, original_size, vbmeta_offset, vbmeta_size);
    free(target_data);
    free(vbmeta_data);
    if (write_file(output_path, output, target_size) != 0) {
        fprintf(stderr, "Failed to write patched image\n");
        free(output);
        return -1;
    }
    uint64_t v_orig, v_off, v_sz;
    if (read_avb_footer(output, target_size, &v_orig, &v_off, &v_sz)) {
        if (v_off + v_sz <= target_size && memcmp(output + v_off, AVB_MAGIC, 4) == 0) {
            free(output);
            return 0;
        }
    }
    free(output);
    fprintf(stderr, "VBMeta verification failed\n");
    return -1;
}

/* ---- Remove Slot Suffix _a/_b/_ab ---- */
static void strip_slot_suffix(const char *partition, char *base, size_t base_size) {
    size_t len = strlen(partition);
    if (len > 3 && strcmp(partition + len - 3, "_ab") == 0) {
        snprintf(base, base_size, "%.*s", (int)(len - 3), partition);
    } else if (len > 2 && (strcmp(partition + len - 2, "_a") == 0 ||
                           strcmp(partition + len - 2, "_b") == 0)) {
        snprintf(base, base_size, "%.*s", (int)(len - 2), partition);
    } else {
        snprintf(base, base_size, "%s", partition);
    }
}

/* ---- Reboot to Fastboot Mode ---- */
static int reboot_fastboot(void) {
    char cmd[MAX_CMD_LEN];
    printf("\n>>> Rebooting device into Fastboot mode...\n");
    snprintf(cmd, sizeof(cmd), "%s reboot bootloader", adb_path);
    printf("Execute command: %s\n", cmd);
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "⚠️  Auto entry to Fastboot failed, please enter manually!\n");
    }
    return ret;
}

/* ---- Flash Partition ---- */
static int flash_partition(const char *partition, const char *image_path) {
    char cmd[MAX_CMD_LEN];
    printf("\n>>> Start flashing partition %s ...\n", partition);
    snprintf(cmd, sizeof(cmd), "%s flash %s \"%s\"", fastboot_path, partition, image_path);
    printf("Execute command: %s\n", cmd);
    return system(cmd);
}

/* ---- Read User Input Line ---- */
static void read_line(const char *prompt, char *buf, size_t size) {
    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buf, (int)size, stdin))
        buf[strcspn(buf, "\r\n")] = '\0';
}

/* ---- Check and Execute Backup ---- */
static int run_backup(const char *exe_dir) {
    char backup_bin[MAX_PATH_LEN];
    char vbmetas_dir[MAX_PATH_LEN];
    char cmd[MAX_CMD_LEN];

#ifdef _WIN32
    snprintf(backup_bin, sizeof(backup_bin), "%s\\bin\\vbmetabackup.exe", exe_dir);
#else
    snprintf(backup_bin, sizeof(backup_bin), "%s/bin/vbmetabackup", exe_dir);
#endif
    snprintf(vbmetas_dir, sizeof(vbmetas_dir), "%s%cvbmetas", exe_dir, PATH_SEP);

    if (!file_exists(backup_bin)) {
        fprintf(stderr, "\nERROR: Backup tool not found: %s\n", backup_bin);
        printf("Press Enter to exit...");
        getchar();
        return -1;
    }

    printf("==========================================================\n");
    printf("No VBMeta backup detected. Backup is required to proceed!\n");
    printf("==========================================================\n\n");
    printf("Please ensure:\n");
    printf("  1. Device is running Android system normally\n");
    printf("  2. USB connected, USB debugging enabled and ROOT granted\n\n");
    printf("Press Enter to start backup...");
    fflush(stdout);
    getchar();

    snprintf(cmd, sizeof(cmd), "%s -o %s", backup_bin, vbmetas_dir);
    printf("\nExecute: %s\n", cmd);

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "\nERROR: Backup execution failed!\n");
        printf("Press Enter to exit...");
        getchar();
        return -1;
    }
    return 0;
}

static int check_and_run_backup(const char *exe_dir) {
    char marker[MAX_PATH_LEN];
    snprintf(marker, sizeof(marker), "%s%cfinish_backup", exe_dir, PATH_SEP);
    if (file_exists(marker))
        return 0;

    if (run_backup(exe_dir) != 0)
        return -1;

    FILE *f = fopen(marker, "w");
    if (f) {
        fprintf(f, "done\n");
        fclose(f);
    }
    printf("\n✅ Backup completed!\n\n");
    return 0;
}

int main(int argc, char **argv) {
    char exe_dir[MAX_PATH_LEN];
    if (get_exe_dir(exe_dir, sizeof(exe_dir)) != 0) {
        fprintf(stderr, "Failed to get program directory\n");
        printf("Press Enter to close window...");
        getchar();
        return 1;
    }
    printf("Program running directory: %s\n\n", exe_dir);

    // Check and run backup on first launch
    if (check_and_run_backup(exe_dir) != 0)
        return 1;

    char partition_buf[MAX_INPUT_LEN];
    char image_buf[MAX_PATH_LEN];
    char temp_image[MAX_PATH_LEN];
    char vbmeta_path[MAX_PATH_LEN];
    char base_name[MAX_INPUT_LEN];
    char temp_dir[MAX_PATH_LEN];
    char choice[32];

    // Create temporary directory
    snprintf(temp_dir, sizeof(temp_dir), "%s%ctemp", exe_dir, PATH_SEP);
    mkdir_p(temp_dir);

    // Loop for patching and flashing
    while (1) {
        printf("==================== New Task ====================\n");

        // 1. Input partition name
        read_line("Enter Fastboot partition name (e.g. boot_a / vbmeta_b): ", partition_buf, sizeof(partition_buf));
        if (partition_buf[0] == '\0') {
            printf("Partition name is empty, exiting program\n");
            break;
        }

        // 2. Input image full path
        read_line("Enter full path of image to patch: ", image_buf, sizeof(image_buf));
        if (image_buf[0] == '\0') {
            printf("Image path is empty, exiting program\n");
            break;
        }
        if (!file_exists(image_buf)) {
            fprintf(stderr, "Error: Image file does not exist!\n");
            printf("-----------------------------------------------\n");
            continue;
        }

        // 3. Match corresponding VBMeta backup
        strip_slot_suffix(partition_buf, base_name, sizeof(base_name));
        snprintf(vbmeta_path, sizeof(vbmeta_path), "%s%cvbmetas%c%s.vbmeta",
                 exe_dir, PATH_SEP, PATH_SEP, base_name);

        // Set temporary output image name
        snprintf(temp_image, sizeof(temp_image), "%s%c%s.img",
                 temp_dir, PATH_SEP, partition_buf);

        // 4. Start VBMeta patching
        printf("\nStart patching image...\n");
        if (transplant_vbmeta(vbmeta_path, image_buf, temp_image) != 0) {
            fprintf(stderr, "❌ Image patching failed!\n");
            printf("-----------------------------------------------\n");
            continue;
        }
        printf("✅ Image patched successfully: %s\n", temp_image);

        // 5. Press Enter to reboot into Fastboot
        printf("\nPress Enter to reboot device into Fastboot...");
        fflush(stdout);
        getchar();
        reboot_fastboot();

        // 6. Execute flashing
        int flash_ret = flash_partition(partition_buf, temp_image);
        if (flash_ret == 0) {
            printf("\n✅ Partition flashed successfully!\n");
        } else {
            fprintf(stderr, "\n❌ Flashing failed! Please check connection and partition name\n");
        }

        // Clean temporary file
        remove(temp_image);

        // 7. Ask to continue
        printf("\n-----------------------------------------------\n");
        read_line("Continue patching and flashing? (y/n): ", choice, sizeof(choice));
        printf("-----------------------------------------------\n");

        // Exit or continue
        if (strcmp(choice, "n") == 0 || strcmp(choice, "N") == 0 ||
            strcmp(choice, "no") == 0 || strcmp(choice, "NO") == 0) {
            printf("Exiting program\n");
            break;
        }
    }

    printf("\nPress Enter to close window...");
    getchar();
    return 0;
}
