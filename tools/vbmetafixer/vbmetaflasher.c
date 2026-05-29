#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir_p(d) _mkdir(d)
#define PATH_SEP '\\'
#else
#include <unistd.h>
#include <sys/stat.h>
#include <locale.h>
#define mkdir_p(d) mkdir(d, 0755)
#define PATH_SEP '/'
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
    if (lcid == 0x0804 || lcid == 0x0404 || lcid == 0x0C04) {
        return 1;
    }
#else
    const char *loc = setlocale(LC_MESSAGES, "");
    if (loc && strstr(loc, "zh_")) {
        return 1;
    }
#endif
    return 0;
}

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
    p[2] = (v >> 8) & 0xff;
    p[3] = v & 0xff;
}

static void put_be64(uint8_t *p, uint64_t v) {
    put_be32(p, (uint32_t)(v >> 32));
    put_be32(p + 4, (uint32_t)v);
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
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f); return NULL;
    }
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

static int write_file(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (fwrite(data, 1, size, f) != size) {
        fclose(f); return -1;
    }
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
    DWORD len = GetModuleFileNameA(NULL, buf, (DWORD)buf_size);
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
    if (last_sep) *last_sep = '\0';
    else strcpy(buf, ".");
    return 0;
}

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
        printf(is_chinese_locale ? "  检测到已有AVB Footer，原始数据大小: %llu\n" : "  Existing AVB Footer found, original size: %llu\n",
               (unsigned long long)original_size);
    } else {
        original_size = target_size - vbmeta_size - AVB_FOOTER_SIZE;
        printf(is_chinese_locale ? "  无AVB Footer，计算原始数据大小: %llu\n" : "  No AVB Footer, calculated original size: %llu\n",
               (unsigned long long)original_size);
    }
    uint64_t vbmeta_offset = original_size;
    uint64_t footer_offset = target_size - AVB_FOOTER_SIZE;
    uint64_t required = original_size + vbmeta_size + AVB_FOOTER_SIZE;
    if (required > target_size) {
        fprintf(stderr, is_chinese_locale ? "空间不足: 需要 %llu 字节，当前 %llu 字节\n" : "Not enough space: need %llu bytes, total %llu bytes\n",
                (unsigned long long)required, (unsigned long long)target_size);
        free(vbmeta_data); free(target_data); return -1;
    }
    uint8_t *output = calloc(1, target_size);
    if (!output) { free(vbmeta_data); free(target_data); return -1; }
    memcpy(output, target_data, (size_t)original_size);
    memcpy(output + vbmeta_offset, vbmeta_data, vbmeta_size);
    create_avb_footer(output + footer_offset, original_size, vbmeta_offset, vbmeta_size);
    free(target_data); free(vbmeta_data);
    if (write_file(output_path, output, target_size) != 0) {
        fprintf(stderr, is_chinese_locale ? "写入修补镜像失败: %s\n" : "Failed to write patched image: %s\n", output_path);
        free(output); return -1;
    }
    uint64_t v_orig, v_off, v_sz;
    if (read_avb_footer(output, target_size, &v_orig, &v_off, &v_sz)) {
        if (v_off + v_sz <= target_size && memcmp(output + v_off, AVB_MAGIC, 4) == 0) {
            free(output); return 0;
        }
    }
    free(output);
    fprintf(stderr, is_chinese_locale ? "VBMeta校验失败\n" : "VBMeta verification failed\n");
    return -1;
}

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

static int reboot_fastboot(void) {
    char cmd[MAX_CMD_LEN];
    printf("\n>>> %s\n", is_chinese_locale ? "正在重启设备进入 Fastboot 模式..." : "Rebooting device to Fastboot...");
    snprintf(cmd, sizeof(cmd), "%s reboot bootloader", adb_path);
    printf("Execute: %s\n", cmd);
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "⚠️  %s\n", is_chinese_locale ? "自动进入Fastboot失败，请手动进入！" : "Auto reboot failed, please enter Fastboot manually!");
    }
    return ret;
}

static int flash_partition(const char *partition, const char *image_path) {
    char cmd[MAX_CMD_LEN];
    printf("\n>>> %s %s ...\n", is_chinese_locale ? "开始刷写分区" : "Flashing partition", partition);
    snprintf(cmd, sizeof(cmd), "%s flash %s \"%s\"", fastboot_path, partition, image_path);
    printf("Execute: %s\n", cmd);
    return system(cmd);
}

static void read_line(const char *prompt, char *buf, size_t size) {
    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buf, (int)size, stdin)) buf[strcspn(buf, "\r\n")] = '\0';
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
        fprintf(stderr, "\n%s: %s\n", is_chinese_locale ? "错误: 备份工具不存在" : "Error: Backup tool not found", backup_bin);
        printf("%s", is_chinese_locale ? "按回车退出..." : "Press Enter to exit...");
        fflush(stdout); getchar(); return -1;
    }

    printf("==========================================================\n");
    printf("%s\n", is_chinese_locale ? "未检测到VBMeta备份，必须先完成备份才能继续！" : "No VBMeta backup found. Backup is required first!");
    printf("==========================================================\n\n");
    if (is_chinese_locale) {
        printf("请确保：\n");
        printf("  1. 设备正常进入安卓系统\n");
        printf("  2. USB已连接、开启USB调试并拥有ROOT权限\n\n");
        printf("按回车开始备份...");
    } else {
        printf("Please make sure:\n");
        printf("  1. Device is in Android system\n");
        printf("  2. USB connected, USB Debug & Root enabled\n\n");
        printf("Press Enter to start backup...");
    }
    fflush(stdout); getchar();

    // ==============================================
    // 🔥 核心修复：给路径加双引号，支持空格/中文/括号
    // ==============================================
    snprintf(cmd, sizeof(cmd), "\"%s\" -o \"%s\"", backup_bin, vbmetas_dir);
    printf("\nExecute: %s\n", cmd);

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "\n%s\n", is_chinese_locale ? "错误: 备份执行失败！" : "Error: Backup failed!");
        printf("%s", is_chinese_locale ? "按回车退出..." : "Press Enter to exit...");
        fflush(stdout); getchar(); return -1;
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
    printf("\n✅ %s\n\n", is_chinese_locale ? "备份完成！" : "Backup completed!");
    return 0;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    set_console_utf8();
#endif
    is_chinese_locale = detect_chinese_locale();

    char exe_dir[MAX_PATH_LEN];
    if (get_exe_dir(exe_dir, sizeof(exe_dir)) != 0) {
        fprintf(stderr, "%s\n", is_chinese_locale ? "获取程序目录失败" : "Failed to get program directory");
        printf("%s", is_chinese_locale ? "按回车退出..." : "Press Enter to exit...");
        fflush(stdout); getchar(); return 1;
    }
    printf("%s: %s\n\n", is_chinese_locale ? "程序运行目录" : "Program directory", exe_dir);

    if (check_and_run_backup(exe_dir) != 0) return 1;

    char partition_buf[MAX_INPUT_LEN];
    char image_buf[MAX_PATH_LEN];
    char temp_image[MAX_PATH_LEN];
    char vbmeta_path[MAX_PATH_LEN];
    char base_name[MAX_PARTITION_NAME];
    char temp_dir[MAX_PATH_LEN];
    char choice[32];

    snprintf(temp_dir, sizeof(temp_dir), "%s%ctemp", exe_dir, PATH_SEP);
    mkdir_p(temp_dir);

    while (1) {
        printf("==================== %s ====================\n", is_chinese_locale ? "新任务" : "NEW TASK");

        read_line(is_chinese_locale ? "请输入Fastboot分区名(如 boot_a / vbmeta_b): " : "Enter partition name (e.g. boot_a / vbmeta_b): ",
                  partition_buf, sizeof(partition_buf));
        if (!partition_buf[0]) {
            printf("%s\n", is_chinese_locale ? "分区名为空，退出程序" : "Partition empty, exit program");
            break;
        }

        read_line(is_chinese_locale ? "请输入待修补镜像完整路径: " : "Enter full path of image to patch: ",
                  image_buf, sizeof(image_buf));
        if (!image_buf[0]) {
            printf("%s\n", is_chinese_locale ? "镜像路径为空，退出程序" : "Image path empty, exit program");
            break;
        }
        if (!file_exists(image_buf)) {
            fprintf(stderr, "%s\n", is_chinese_locale ? "错误: 镜像文件不存在！" : "Error: Image file not found!");
            printf("-----------------------------------------------\n");
            continue;
        }

        strip_slot_suffix(partition_buf, base_name, sizeof(base_name));
        snprintf(vbmeta_path, sizeof(vbmeta_path), "%s%cvbmetas%c%s.vbmeta",
                 exe_dir, PATH_SEP, PATH_SEP, base_name);

        snprintf(temp_image, sizeof(temp_image), "%s%c%s", temp_dir, PATH_SEP, partition_buf);
        strncat(temp_image, ".img", sizeof(temp_image) - strlen(temp_image) - 1);

        printf("\n%s...\n", is_chinese_locale ? "开始修补镜像" : "Patching image");
        if (transplant_vbmeta(vbmeta_path, image_buf, temp_image) != 0) {
            fprintf(stderr, "❌ %s\n", is_chinese_locale ? "镜像修补失败！" : "Patch failed!");
            printf("-----------------------------------------------\n");
            continue;
        }
        printf("✅ %s: %s\n", is_chinese_locale ? "镜像修补完成" : "Patch completed", temp_image);

        printf("\n%s", is_chinese_locale ? "按回车键，设备将重启进入 Fastboot..." : "Press Enter to reboot device to Fastboot...");
        fflush(stdout); getchar();
        reboot_fastboot();

        int flash_ret = flash_partition(partition_buf, temp_image);
        if (flash_ret == 0) {
            printf("\n✅ %s\n", is_chinese_locale ? "分区刷写成功！" : "Flash success!");
        } else {
            fprintf(stderr, "\n❌ %s\n", is_chinese_locale ? "分区刷写失败！请检查连接与分区名称" : "Flash failed! Check connection & partition name");
        }

        remove(temp_image);

        printf("\n-----------------------------------------------\n");
        read_line(is_chinese_locale ? "是否继续修补/刷写？(y/n): " : "Continue patching & flashing? (y/n): ",
                  choice, sizeof(choice));
        printf("-----------------------------------------------\n");

        if (!strcasecmp(choice, "n") || !strcasecmp(choice, "no")) {
            printf("%s\n", is_chinese_locale ? "已选择退出，程序结束" : "Exit selected, program terminated");
            break;
        }
    }

    printf("\n%s", is_chinese_locale ? "按回车键关闭窗口..." : "Press Enter to close window...");
    fflush(stdout); getchar();
    return 0;
}
