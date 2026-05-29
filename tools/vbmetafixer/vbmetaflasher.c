#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir_p(d) _mkdir(d)
#define PATH_SEP '\\'
#define PATH_FORMAT "%s\\%s"
#else
#include <unistd.h>
#include <sys/stat.h>
#include <locale.h>
#define mkdir_p(d) mkdir(d, 0755)
#define PATH_SEP '/'
#define PATH_FORMAT "%s/%s"
#endif

#define AVB_MAGIC "AVB0"
#define AVB_VBMETA_IMAGE_HEADER_SIZE 256
#define AVB_FOOTER_MAGIC "AVBf"
#define AVB_FOOTER_SIZE 64

#define MAX_PATH_LEN 512
#define MAX_CMD_LEN 2048
#define MAX_INPUT_LEN 256
#define MAX_PARTITION_NAME 128

static const char *fastboot_path = "fastboot";
static const char *adb_path = "adb";
static int is_chinese_locale = 0;

#ifdef _WIN32
static void set_console_utf8(void) {
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
}
#endif

static int detect_chinese_locale(void) {
#ifdef _WIN32
    LCID lcid = GetUserDefaultLCID();
    if (lcid == 0x0804 || lcid == 0x0404 || lcid == 0x0C04) return 1;
#else
    const char *loc = setlocale(LC_MESSAGES, "");
    if (loc && strstr(loc, "zh_")) return 1;
#endif
    return 0;
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t be64(const uint8_t *p) {
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (v >> 24) & 0xff; p[1] = (v >> 16) & 0xff; p[2] = (v >> 8) & 0xff; p[3] = v & 0xff;
}

static void put_be64(uint8_t *p, uint64_t v) {
    put_be32(p, (uint32_t)(v >> 32)); put_be32(p + 4, (uint32_t)v);
}

static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f); *out_size = (size_t)sz; return buf;
}

static int write_file(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (fwrite(data, 1, size, f) != size) { fclose(f); return -1; }
    fclose(f); return 0;
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; } return 0;
}

static int get_exe_dir(char *buf, size_t buf_size) {
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(NULL, buf, (DWORD)buf_size);
    if (len == 0 || len >= buf_size) return -1;
#else
    ssize_t len = readlink("/proc/self/exe", buf, buf_size - 1);
    if (len <= 0) return -1; buf[len] = '\0';
#endif
    char *last_sep = strrchr(buf, PATH_SEP);
#ifdef _WIN32
    if (!last_sep) last_sep = strrchr(buf, '/');
#endif
    if (last_sep) *last_sep = '\0'; else strcpy(buf, ".");
    return 0;
}

static int read_avb_footer(const uint8_t *data, size_t len, uint64_t *original_size, uint64_t *vbmeta_offset, uint64_t *vbmeta_size) {
    if (len < AVB_FOOTER_SIZE) return 0;
    const uint8_t *footer = data + len - AVB_FOOTER_SIZE;
    if (memcmp(footer, AVB_FOOTER_MAGIC, 4) != 0) return 0;
    *original_size = be64(footer + 12);
    *vbmeta_offset = be64(footer + 20);
    *vbmeta_size = be64(footer + 28);
    return 1;
}

static void create_avb_footer(uint8_t *footer, uint64_t original_size, uint64_t vbmeta_offset, uint64_t vbmeta_size) {
    memset(footer, 0, AVB_FOOTER_SIZE);
    memcpy(footer, AVB_FOOTER_MAGIC, 4);
    put_be32(footer + 4, 1); put_be32(footer + 8, 0);
    put_be64(footer + 12, original_size); put_be64(footer + 20, vbmeta_offset); put_be64(footer + 28, vbmeta_size);
}

static int transplant_vbmeta(const char *vbmeta_path, const char *source_image, const char *output_path) {
    size_t vbmeta_size;
    uint8_t *vbmeta_data = read_file(vbmeta_path, &vbmeta_size);
    if (!vbmeta_data) {
        fprintf(stderr, is_chinese_locale ? "读取VBMeta失败: %s\n" : "Failed to read VBMeta: %s\n", vbmeta_path);
        return -1;
    }
    size_t target_size;
    uint8_t *target_data = read_file(source_image, &target_size);
    if (!target_data) {
        fprintf(stderr, is_chinese_locale ? "读取镜像失败: %s\n" : "Failed to read image: %s\n", source_image);
        free(vbmeta_data); return -1;
    }
    uint64_t original_size;
    uint64_t existing_offset, existing_size;
    if (read_avb_footer(target_data, target_size, &original_size, &existing_offset, &existing_size)) {
        printf(is_chinese_locale ? "  检测到已有AVB Footer，原始数据大小: %llu\n" : "  Existing AVB Footer found: %llu\n", (unsigned long long)original_size);
    } else {
        original_size = target_size - vbmeta_size - AVB_FOOTER_SIZE;
        printf(is_chinese_locale ? "  无AVB Footer，计算原始大小: %llu\n" : "  No AVB Footer: %llu\n", (unsigned long long)original_size);
    }
    uint64_t vbmeta_offset = original_size;
    uint64_t footer_offset = target_size - AVB_FOOTER_SIZE;
    uint64_t required = original_size + vbmeta_size + AVB_FOOTER_SIZE;
    if (required > target_size) {
        fprintf(stderr, is_chinese_locale ? "空间不足\n" : "Not enough space\n");
        free(vbmeta_data); free(target_data); return -1;
    }
    uint8_t *output = calloc(1, target_size);
    if (!output) { free(vbmeta_data); free(target_data); return -1; }
    memcpy(output, target_data, (size_t)original_size);
    memcpy(output + vbmeta_offset, vbmeta_data, vbmeta_size);
    create_avb_footer(output + footer_offset, original_size, vbmeta_offset, vbmeta_size);
    free(target_data); free(vbmeta_data);
    if (write_file(output_path, output, target_size) != 0) {
        fprintf(stderr, is_chinese_locale ? "写入失败\n" : "Write failed\n");
        free(output); return -1;
    }
    free(output);
    return 0;
}

static void strip_slot_suffix(const char *partition, char *base, size_t base_size) {
    size_t len = strlen(partition);
    if (len > 3 && !strcmp(partition + len - 3, "_ab")) snprintf(base, base_size, "%.*s", (int)(len - 3), partition);
    else if (len > 2 && (!strcmp(partition + len - 2, "_a") || !strcmp(partition + len - 2, "_b"))) snprintf(base, base_size, "%.*s", (int)(len - 2), partition);
    else snprintf(base, base_size, "%s", partition);
}

static int reboot_fastboot(void) {
    char cmd[MAX_CMD_LEN];
    printf("\n>>> %s\n", is_chinese_locale ? "重启设备到 Fastboot..." : "Rebooting to Fastboot...");
    snprintf(cmd, sizeof(cmd), "%s reboot bootloader", adb_path);
    printf("Execute: %s\n", cmd);
    return system(cmd);
}

static int flash_partition(const char *partition, const char *image_path) {
    char cmd[MAX_CMD_LEN];
    printf("\n>>> %s %s...\n", is_chinese_locale ? "刷写分区" : "Flashing", partition);
    snprintf(cmd, sizeof(cmd), "%s flash %s \"%s\"", fastboot_path, partition, image_path);
    printf("Execute: %s\n", cmd);
    return system(cmd);
}

static void select_reboot_target(void) {
    char opt[16], cmd[MAX_CMD_LEN];
    while (1) {
        if (is_chinese_locale) {
            printf("\n============ 重启选项 ============\n");
            printf("1 = 重启到 Recovery\n2 = 重启到 FastbootD\n3 = 重启到 Bootloader\n4 = 重启到系统\n0 = 不重启\n选项: ");
        } else {
            printf("\n============ Reboot ============\n");
            printf("1 = Recovery\n2 = FastbootD\n3 = Bootloader\n4 = System\n0 = Skip\nOption: ");
        }
        fflush(stdout); fgets(opt, sizeof(opt), stdin); opt[strcspn(opt, "\r\n")] = 0;

        if (!strcmp(opt, "0")) { printf("%s\n", is_chinese_locale ? "不重启" : "Skip reboot"); break; }
        else if (!strcmp(opt, "1")) snprintf(cmd, sizeof(cmd), "%s reboot recovery", fastboot_path);
        else if (!strcmp(opt, "2")) snprintf(cmd, sizeof(cmd), "%s reboot fastboot", fastboot_path);
        else if (!strcmp(opt, "3")) snprintf(cmd, sizeof(cmd), "%s reboot bootloader", fastboot_path);
        else if (!strcmp(opt, "4")) snprintf(cmd, sizeof(cmd), "%s reboot", fastboot_path);
        else { printf("%s\n", is_chinese_locale ? "无效输入" : "Invalid"); continue; }
        
        system(cmd); break;
    }
}

static void read_line(const char *prompt, char *buf, size_t size) {
    printf("%s", prompt); fflush(stdout);
    if (fgets(buf, size, stdin)) buf[strcspn(buf, "\r\n")] = 0;
}

static int run_backup(const char *exe_dir) {
    char backup_bin[MAX_PATH_LEN], vbmetas_dir[MAX_PATH_LEN], cmd[MAX_CMD_LEN];

#ifdef _WIN32
    snprintf(backup_bin, sizeof(backup_bin), "%s\\bin\\vbmetabackup.exe", exe_dir);
    snprintf(vbmetas_dir, sizeof(vbmetas_dir), "%s\\vbmetas", exe_dir);
#else
    snprintf(backup_bin, sizeof(backup_bin), "%s/bin/vbmetabackup", exe_dir);
    snprintf(vbmetas_dir, sizeof(vbmetas_dir), "%s/vbmetas", exe_dir);
#endif

    if (!file_exists(backup_bin)) {
        fprintf(stderr, "\n%s: %s\n", is_chinese_locale ? "错误：工具不存在" : "Error: tool missing", backup_bin);
        printf("%s", is_chinese_locale ? "按回车退出" : "Enter to exit"); fflush(stdout); getchar(); return -1;
    }

    printf("==========================================================\n");
    printf("%s\n", is_chinese_locale ? "未检测到备份，首次使用需备份VBMeta" : "VBMeta backup required");
    printf("==========================================================\n\n");

    if (is_chinese_locale) {
        printf("1. 设备进入安卓系统\n2. 开启USB调试 + Root权限\n\n按回车开始备份...");
    } else {
        printf("1. Device in Android\n2. USB debug + root\n\nPress Enter to backup...");
    }
    fflush(stdout); getchar();

    // 🔥 核心修复：Windows 路径全 \ + 双引号
    snprintf(cmd, sizeof(cmd), "\"%s\" -o \"%s\"", backup_bin, vbmetas_dir);
    printf("\nExecute: %s\n", cmd);

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "\n%s\n", is_chinese_locale ? "备份失败！" : "Backup failed!");
        printf("%s", is_chinese_locale ? "按回车退出" : "Enter to exit"); fflush(stdout); getchar(); return -1;
    }
    return 0;
}

static int check_and_run_backup(const char *exe_dir) {
    char marker[MAX_PATH_LEN];
#ifdef _WIN32
    snprintf(marker, sizeof(marker), "%s\\finish_backup", exe_dir);
#else
    snprintf(marker, sizeof(marker), "%s/finish_backup", exe_dir);
#endif
    if (file_exists(marker)) return 0;
    if (run_backup(exe_dir) != 0) return -1;
    FILE *f = fopen(marker, "w"); if (f) { fprintf(f, "ok\n"); fclose(f); }
    printf("\n✅ %s\n\n", is_chinese_locale ? "备份完成！" : "Backup done!");
    return 0;
}

int main(void) {
#ifdef _WIN32
    set_console_utf8();
#endif
    is_chinese_locale = detect_chinese_locale();

    char exe_dir[MAX_PATH_LEN];
    if (get_exe_dir(exe_dir, sizeof(exe_dir))) {
        fprintf(stderr, "%s\n", is_chinese_locale ? "获取目录失败" : "Dir error");
        printf("%s", is_chinese_locale ? "回车退出" : "Enter to exit"); fflush(stdout); getchar(); return 1;
    }
    printf("%s: %s\n\n", is_chinese_locale ? "运行目录" : "Directory", exe_dir);

    if (check_and_run_backup(exe_dir)) return 1;

    char part[MAX_INPUT_LEN], img[MAX_PATH_LEN], out_img[MAX_PATH_LEN], vbmeta[MAX_PATH_LEN], base[MAX_PARTITION_NAME], temp[MAX_PATH_LEN], choice[32];

#ifdef _WIN32
    snprintf(temp, sizeof(temp), "%s\\temp", exe_dir);
#else
    snprintf(temp, sizeof(temp), "%s/temp", exe_dir);
#endif
    mkdir_p(temp);

    while (1) {
        printf("==================== %s ====================\n", is_chinese_locale ? "新任务" : "NEW TASK");
        read_line(is_chinese_locale ? "输入分区名(boot/vbmeta): " : "Partition(boot/vbmeta): ", part, sizeof(part));
        if (!part[0]) { printf("%s\n", is_chinese_locale ? "退出" : "Exit"); break; }

        read_line(is_chinese_locale ? "输入镜像路径: " : "Image path: ", img, sizeof(img));
        if (!img[0] || !file_exists(img)) { fprintf(stderr, "%s\n", is_chinese_locale ? "镜像不存在" : "No image"); continue; }

        strip_slot_suffix(part, base, sizeof(base));
#ifdef _WIN32
        snprintf(vbmeta, sizeof(vbmeta), "%s\\vbmetas\\%s.vbmeta", exe_dir, base);
        snprintf(out_img, sizeof(out_img), "%s\\temp\\%s.img", exe_dir, part);
#else
        snprintf(vbmeta, sizeof(vbmeta), "%s/vbmetas/%s.vbmeta", exe_dir, base);
        snprintf(out_img, sizeof(out_img), "%s/temp/%s.img", exe_dir, part);
#endif

        printf("\n%s...\n", is_chinese_locale ? "修补中" : "Patching");
        if (transplant_vbmeta(vbmeta, img, out_img)) { fprintf(stderr, "%s\n", is_chinese_locale ? "修补失败" : "Patch failed"); continue; }
        printf("✅ %s: %s\n", is_chinese_locale ? "修补完成" : "Patched", out_img);

        printf("\n%s", is_chinese_locale ? "按回车重启到Fastboot" : "Enter to reboot Fastboot"); fflush(stdout); getchar();
        reboot_fastboot();

        if (!flash_partition(part, out_img)) {
            printf("\n✅ %s\n", is_chinese_locale ? "刷写成功！" : "Flash success!");
            select_reboot_target();
        } else {
            fprintf(stderr, "\n❌ %s\n", is_chinese_locale ? "刷写失败" : "Flash failed");
        }

        remove(out_img);
        printf("\n----------------------------------------\n");
        read_line(is_chinese_locale ? "继续?(y/n): " : "Continue?(y/n): ", choice, sizeof(choice));
        if (!strcasecmp(choice, "n") || !strcasecmp(choice, "no")) break;
    }

    printf("\n%s", is_chinese_locale ? "按回车关闭" : "Enter to close"); fflush(stdout); getchar();
    return 0;
}
