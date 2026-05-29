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

static void clear_input_buffer(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

static void wait_exit(int code) {
    printf("\nProgram will exit. Press Enter to close window...");
    fflush(stdout);
    clear_input_buffer();
    exit(code);
}

/* ---- big-endian helpers ---- */
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

/* ---- file I/O ---- */
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
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *out_size = sz;
    return buf;
}
static int write_file(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (fwrite(data, 1, size, f) != size) { fclose(f); return -1; }
    fclose(f);
    return 0;
}
static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

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
    if (last_sep) *last_sep = '\0'; else strcpy(buf, ".");
    return 0;
}

/* ---- AVB footer / transplant ---- */
static int read_avb_footer(const uint8_t *data, size_t len,
                           uint64_t *original_size, uint64_t *vbmeta_offset,
                           uint64_t *vbmeta_size) {
    if (len < AVB_FOOTER_SIZE) return 0;
    const uint8_t *footer = data + len - AVB_FOOTER_SIZE;
    if (memcmp(footer, AVB_FOOTER_MAGIC, 4) != 0) return 0;
    *original_size = be64(footer + 12);
    *vbmeta_offset = be64(footer + 20);
    *vbmeta_size   = be64(footer + 28);
    return 1;
}
static void create_avb_footer(uint8_t *footer, uint64_t original_size, 
                              uint64_t vbmeta_offset, uint64_t vbmeta_size) {
    memset(footer, 0, AVB_FOOTER_SIZE);
    memcpy(footer, AVB_FOOTER_MAGIC, 4);
    put_be32(footer + 4, 1);
    put_be32(footer + 8, 0);
    put_be64(footer + 12, original_size);
    put_be64(footer + 20, vbmeta_offset);
    put_be64(footer + 28, vbmeta_size);
}

static int transplant_vbmeta(const char *vbmeta_path, const char *source_image, const char *output_path) {
    size_t vbmeta_size;
    uint8_t *vbmeta_data = read_file(vbmeta_path, &vbmeta_size);
    if (!vbmeta_data) {
        fprintf(stderr, "Error: Failed to read vbmeta backup: %s\n", vbmeta_path);
        return -1;
    }
    size_t target_size;
    uint8_t *target_data = read_file(source_image, &target_size);
    if (!target_data) {
        fprintf(stderr, "Error: Failed to read source image: %s\n", source_image);
        free(vbmeta_data);
        return -1;
    }
    uint64_t original_size, existing_offset, existing_size;
    if (read_avb_footer(target_data, target_size, &original_size, &existing_offset, &existing_size)) {
        printf("  Target has existing VBMeta, original size: %llu\n", (unsigned long long)original_size);
    } else {
        original_size = target_size - vbmeta_size - AVB_FOOTER_SIZE;
        printf("  Target has no VBMeta, calculated original size: %llu\n", (unsigned long long)original_size);
    }
    uint64_t vbmeta_offset = original_size;
    uint64_t footer_offset = target_size - AVB_FOOTER_SIZE;
    uint64_t required = original_size + vbmeta_size + AVB_FOOTER_SIZE;
    if (required > target_size) {
        fprintf(stderr, "Error: Insufficient space! Need %llu, have %llu\n", (unsigned long long)required, (unsigned long long)target_size);
        free(vbmeta_data); free(target_data); return -1;
    }
    uint8_t *output = calloc(1, target_size);
    if (!output) { free(vbmeta_data); free(target_data); return -1; }
    memcpy(output, target_data, (size_t)original_size);
    memcpy(output + vbmeta_offset, vbmeta_data, vbmeta_size);
    create_avb_footer(output + footer_offset, original_size, vbmeta_offset, vbmeta_size);
    free(target_data); free(vbmeta_data);
    if (write_file(output_path, output, target_size) != 0) {
        fprintf(stderr, "Error: Failed to write output image: %s\n", output_path);
        free(output); return -1;
    }
    uint64_t v_orig, v_off, v_sz;
    if (read_avb_footer(output, target_size, &v_orig, &v_off, &v_sz)) {
        if (v_off + v_sz <= target_size && memcmp(output + v_off, AVB_MAGIC, 4) == 0) {
            printf("  [OK] VBMeta transplant verified!\n");
        } else {
            fprintf(stderr, "  [FAIL] VBMeta verification failed!\n");
            free(output); return -1;
        }
    }
    free(output);
    return 0;
}

static void strip_slot_suffix(const char *partition, char *base, size_t base_size) {
    size_t len = strlen(partition);
    if (len > 3 && strcmp(partition + len - 3, "_ab") == 0) {
        snprintf(base, base_size, "%.*s", (int)(len - 3), partition);
    } else if (len > 2 && (strcmp(partition + len - 2, "_a") == 0 || strcmp(partition + len - 2, "_b") == 0)) {
        snprintf(base, base_size, "%.*s", (int)(len - 2), partition);
    } else {
        snprintf(base, base_size, "%s", partition);
    }
}

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
        fprintf(stderr, "Error: Backup tool not found -> %s\n", backup_bin);
        return -1;
    }
    printf("==========================================================\n");
    printf(" Notice: No local backup found. VBMeta backup is REQUIRED!\n");
    printf("==========================================================\n\n");
    printf("Please ensure:\n");
    printf("  1. Device is powered on and booted into Android system\n");
    printf("  2. USB Cable is connected and USB Debugging is enabled\n");
    printf("  3. ADB environment is working and Shell has Root access\n\n");
    printf("Press [Enter] to start backup...");
    fflush(stdout);
    clear_input_buffer();
    snprintf(cmd, sizeof(cmd), "\"%s\" -o \"%s\"", backup_bin, vbmetas_dir);
    printf("\nCalling backup tool, please wait...\n");
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "\n[FAIL] Backup tool aborted (code=%d). Check phone status.\n", ret);
        return -1;
    }
    return 0;
}

static int check_and_run_backup(const char *exe_dir) {
    char marker[MAX_PATH_LEN];
    snprintf(marker, sizeof(marker), "%s%cfinish_backup", exe_dir, PATH_SEP);
    if (file_exists(marker)) return 0;
    if (run_backup(exe_dir) != 0) return -1;
    FILE *f = fopen(marker, "w");
    if (f) { fprintf(f, "done\n"); fclose(f); }
    printf("\n[SUCCESS] Backup complete! Marker created.\n\n");
    return 0;
}

static int flash_partition(const char *partition, const char *image_path) {
    char cmd[MAX_CMD_LEN];
    snprintf(cmd, sizeof(cmd), "%s flash %s \"%s\"", fastboot_path, partition, image_path);
    printf("$ %s\n", cmd);
    int ret = system(cmd);
    if (ret != 0) fprintf(stderr, "Flash failed (code=%d)\n", ret);
    return ret;
}

static void read_line(const char *prompt, char *buf, size_t size) {
    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buf, (int)size, stdin))
        buf[strcspn(buf, "\r\n")] = '\0';
}

int main(int argc, char **argv) {
    char exe_dir[MAX_PATH_LEN];
    if (get_exe_dir(exe_dir, sizeof(exe_dir)) != 0) {
        fprintf(stderr, "Error: Failed to get executable directory.\n");
        wait_exit(1);
    }
    printf("Working directory: %s\n", exe_dir);

    const char *partition_arg = NULL;
    const char *image_arg = NULL;
    int positional = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            fastboot_path = argv[++i];
        } else {
            if (positional == 0) partition_arg = argv[i];
            else if (positional == 1) image_arg = argv[i];
            else { wait_exit(1); }
            positional++;
        }
    }

    if (check_and_run_backup(exe_dir) != 0) {
        wait_exit(1);
    }

    while (1) {
        char partition_buf[MAX_INPUT_LEN] = {0};
        char image_buf[MAX_PATH_LEN] = {0};
        const char *current_partition = partition_arg;
        const char *current_image = image_arg;

        printf("\n--- Starting Patch & Flash Session ---\n");
        if (!current_partition) {
            read_line("Enter target partition name (e.g. boot_a, boot_ab): ", partition_buf, sizeof(partition_buf));
            if (partition_buf[0] == '\0') { printf("Error: Partition cannot be empty!\n"); goto loop_end; }
            current_partition = partition_buf;
        }
        if (!current_image) {
            read_line("Enter path to the image file to patch: ", image_buf, sizeof(image_buf));
            if (image_buf[0] == '\0') { printf("Error: Image path cannot be empty!\n"); goto loop_end; }
            current_image = image_buf;
        }
        if (!file_exists(current_image)) {
            fprintf(stderr, "Error: Image file not found: %s\n", current_image);
            goto loop_end;
        }

        char base_name[MAX_INPUT_LEN];
        strip_slot_suffix(current_partition, base_name, sizeof(base_name));
        printf("Partition: %s (Base: %s)\n", current_partition, base_name);

        char vbmeta_path[MAX_PATH_LEN];
        snprintf(vbmeta_path, sizeof(vbmeta_path), "%s%cvbmetas%c%s.vbmeta", exe_dir, PATH_SEP, PATH_SEP, base_name);

        const char *flash_image = current_image;
        char temp_image[MAX_PATH_LEN] = {0};

        if (file_exists(vbmeta_path)) {
            printf("Found matching vbmeta backup: %s\n", vbmeta_path);
            printf("Transplanting VBMeta into image...\n");
            char temp_dir[MAX_PATH_LEN];
            snprintf(temp_dir, sizeof(temp_dir), "%s%ctemp", exe_dir, PATH_SEP);
            mkdir_p(temp_dir);
            snprintf(temp_image, sizeof(temp_image), "%s%c%s.img", temp_dir, PATH_SEP, current_partition);

            if (transplant_vbmeta(vbmeta_path, current_image, temp_image) != 0) {
                fprintf(stderr, "Error: VBMeta transplant failed!\n");
                remove(temp_image);
                goto loop_end;
            }
            printf("\n>> [SUCCESS] Image patched successfully -> %s <<\n", temp_image);
            flash_image = temp_image;
        } else {
            printf("Notice: No backup found for '%s', will flash original image.\n", base_name);
        }

        printf("\nMake sure your device is connected in Fastboot Mode.\n");
        printf("Press [Enter] to start flashing...");
        fflush(stdout);
        clear_input_buffer();

        printf("\nFlashing %s -> %s ...\n", flash_image, current_partition);
        int flash_ret = flash_partition(current_partition, flash_image);
        if (temp_image[0]) remove(temp_image);

        if (flash_ret == 0) {
            printf("\n>> [SUCCESS] Fastboot flash completed! <<\n");
        } else {
            printf("\n>> [FAIL] Fastboot flash encountered errors. <<\n");
        }

        while (1) {
            printf("\n=============================\n");
            printf(" Select next action:\n");
            printf(" 1. Reboot to System\n");
            printf(" 2. Reboot to Recovery\n");
            printf(" 3. Reboot to Userspace Fastboot (Fastbootd)\n");
            printf(" 4. Reboot to Bootloader\n");
            printf(" 5. Do not reboot, patch/flash another image\n");
            printf("=============================\n");
            char choice[16];
            read_line("Enter option (1-5): ", choice, sizeof(choice));

            if (strcmp(choice, "1") == 0) {
                char cmd[MAX_CMD_LEN]; snprintf(cmd, sizeof(cmd), "%s reboot", fastboot_path); system(cmd); break;
            } else if (strcmp(choice, "2") == 0) {
                char cmd[MAX_CMD_LEN]; snprintf(cmd, sizeof(cmd), "%s reboot recovery", fastboot_path); system(cmd); break;
            } else if (strcmp(choice, "3") == 0) {
                char cmd[MAX_CMD_LEN]; snprintf(cmd, sizeof(cmd), "%s reboot fastboot", fastboot_path); system(cmd); break;
            } else if (strcmp(choice, "4") == 0) {
                char cmd[MAX_CMD_LEN]; snprintf(cmd, sizeof(cmd), "%s reboot-bootloader", fastboot_path); system(cmd); break;
            } else if (strcmp(choice, "5") == 0) {
                goto next_loop;
            } else {
                printf("Invalid selection, try again.\n");
            }
        }
        printf("\nReboot command sent.");

    loop_end:
        while (1) {
            printf("\nOptions:\n [C] Continue to patch next image\n [E] Exit program\n");
            char exit_choice[16];
            read_line("Choose (C/E): ", exit_choice, sizeof(exit_choice));
            if (exit_choice[0] == 'c' || exit_choice[0] == 'C') { break; }
            else if (exit_choice[0] == 'e' || exit_choice[0] == 'E') { printf("Exiting...\n"); return 0; }
            else { printf("Invalid choice.\n"); }
        }
    next_loop:
        partition_arg = NULL;
        image_arg = NULL;
    }
    return 0;
}
