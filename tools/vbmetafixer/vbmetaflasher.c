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

/* 清空输入缓冲区，防止 getchar() 或 fgets() 吞键 */
static void clear_input_buffer(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

/* 发生严重错误时暂停，防止双击运行时闪退 */
static void wait_exit(int code) {
    printf("\n程序即将退出。按回车键关闭窗口...");
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

/* ---- exe directory resolution ---- */

static int get_exe_dir(char *buf, size_t buf_size) {
#ifdef _WIN32
    /* GetModuleFileName */
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

/* ---- AVB footer / transplant ---- */

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
        fprintf(stderr, "错误：无法读取 vbmeta 备份文件: %s\n", vbmeta_path);
        return -1;
    }

    size_t target_size;
    uint8_t *target_data = read_file(source_image, &target_size);
    if (!target_data) {
        fprintf(stderr, "错误：无法读取要修补的源镜像: %s\n", source_image);
        free(vbmeta_data);
        return -1;
    }

    uint64_t original_size;
    uint64_t existing_offset, existing_size;
    if (read_avb_footer(target_data, target_size,
                        &original_size, &existing_offset, &existing_size)) {
        printf("  目标镜像已存在 VBMeta，原始数据大小: %llu\n",
               (unsigned long long)original_size);
    } else {
        original_size = target_size - vbmeta_size - AVB_FOOTER_SIZE;
        printf("  目标镜像无 VBMeta，计算出的原始大小: %llu\n",
               (unsigned long long)original_size);
    }

    uint64_t vbmeta_offset = original_size;
    uint64_t footer_offset = target_size - AVB_FOOTER_SIZE;
    uint64_t required = original_size + vbmeta_size + AVB_FOOTER_SIZE;

    if (required > target_size) {
        fprintf(stderr, "错误：镜像空间不足！需要 %llu 字节，实际仅有 %llu 字节\n",
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
        fprintf(stderr, "错误：写入修补后的镜像失败: %s\n", output_path);
        free(output);
        return -1;
    }

    /* 验证 */
    uint64_t v_orig, v_off, v_sz;
    if (read_avb_footer(output, target_size, &v_orig, &v_off, &v_sz)) {
        if (v_off + v_sz <= target_size &&
            memcmp(output + v_off, AVB_MAGIC, 4) == 0) {
            printf("  [成功] VBMeta 嵌入数据校验成功！\n");
        } else {
            fprintf(stderr, "  [失败] VBMeta 嵌入数据校验失败\n");
            free(output);
            return -1;
        }
    }

    free(output);
    return 0;
}

/* ---- partition name helpers ---- */

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

/* ---- backup check ---- */

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
        fprintf(stderr, "错误：未找到备份工具 -> %s\n", backup_bin);
        return -1;
    }

    printf("==========================================================\n");
    printf(" 提示：未检测到本地备份。在刷写前必须备份手机的 VBMeta！\n");
    printf("==========================================================\n\n");
    printf("请确保：\n");
    printf("  1. 手机已正常开机进入 Android 系统\n");
    printf("  2. 已连接 USB 数据线并开启 USB 调试\n");
    printf("  3. 电脑的 ADB 环境正常，且手机已授予 Root 权限\n\n");
    printf("按 [回车键] 开始执行备份...");
    fflush(stdout);
    clear_input_buffer(); // 等待用户按回车

    snprintf(cmd, sizeof(cmd), "\"%s\" -o \"%s\"", backup_bin, vbmetas_dir);
    printf("\n正在调用备份工具，请稍候...\n");
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "\n[失败] 备份工具执行中止 (错误码=%d)。请检查手机连接与Root状态。\n", ret);
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

    /* 创建标记 */
    FILE *f = fopen(marker, "w");
    if (f) {
        fprintf(f, "done\n");
        fclose(f);
    }
    printf("\n[成功] 备份完成！已创建标记文件。\n\n");
    return 0;
}

/* ---- flash ---- */

static int flash_partition(const char *partition, const char *image_path) {
    char cmd[MAX_CMD_LEN];
    snprintf(cmd, sizeof(cmd), "%s flash %s \"%s\"", fastboot_path, partition, image_path);
    printf("$ %s\n", cmd);
    int ret = system(cmd);
    if (ret != 0)
        fprintf(stderr, "刷写失败 (错误码=%d)\n", ret);
    return ret;
}

/* ---- input helpers ---- */

static void read_line(const char *prompt, char *buf, size_t size) {
    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buf, (int)size, stdin))
        buf[strcspn(buf, "\r\n")] = '\0';
}

/* ---- main ---- */

static void usage(const char *prog) {
    printf("用法: %s [-f fastboot路径] [分区名] [镜像路径]\n\n", prog);
    printf("  分区名      fastboot分区名称 (例如: boot_a, boot_ab, vbmeta_b)\n");
    printf("  镜像路径    需要修补并刷写的镜像文件路径\n");
    printf("  -f         手动指定 fastboot 可执行文件的路径 (默认: fastboot)\n\n");
    printf("若运行程序时未携带参数，将自动进入交互式引导界面。\n");
}

int main(int argc, char **argv) {
    char exe_dir[MAX_PATH_LEN];
    if (get_exe_dir(exe_dir, sizeof(exe_dir)) != 0) {
        fprintf(stderr, "错误：无法获取当前程序运行目录。\n");
        wait_exit(1);
    }
    printf("当前工作目录: %s\n", exe_dir);

    /* 解析静态命令行参数 */
    const char *partition_arg = NULL;
    const char *image_arg = NULL;
    int positional = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            fastboot_path = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            wait_exit(0);
        } else {
            if (positional == 0)
                partition_arg = argv[i];
            else if (positional == 1)
                image_arg = argv[i];
            else {
                usage(argv[0]);
                wait_exit(1);
            }
            positional++;
        }
    }

    /* 运行备份检查。失败时会拦截，防止闪退 */
    if (check_and_run_backup(exe_dir) != 0) {
        wait_exit(1);
    }

    /* ================= 主业务循环 ================= */
    while (1) {
        char partition_buf[MAX_INPUT_LEN] = {0};
        char image_buf[MAX_PATH_LEN] = {0};
        const char *current_partition = partition_arg;
        const char *current_image = image_arg;

        printf("\n--- 开始新一轮镜像修补与刷写 ---\n");

        /* 获取分区名 */
        if (!current_partition) {
            read_line("请输入分区名 (例如 boot_a, boot_ab): ", partition_buf, sizeof(partition_buf));
            if (partition_buf[0] == '\0') {
                printf("错误：分区名不能为空！\n");
                goto loop_end; /* 进入结尾判断，让用户决定是继续还是退出 */
            }
            current_partition = partition_buf;
        }

        /* 获取要修补的镜像路径 */
        if (!current_image) {
            read_line("请输入要修补的镜像文件路径: ", image_buf, sizeof(image_buf));
            if (image_buf[0] == '\0') {
                printf("错误：镜像路径不能为空！\n");
                goto loop_end;
            }
            current_image = image_buf;
        }

        if (!file_exists(current_image)) {
            fprintf(stderr, "错误：找不到镜像文件: %s\n", current_image);
            goto loop_end;
        }

        /* 分析槽位及匹配备份 */
        char base_name[MAX_INPUT_LEN];
        strip_slot_suffix(current_partition, base_name, sizeof(base_name));
        printf("目标分区: %s (基准分区名: %s)\n", current_partition, base_name);

        char vbmeta_path[MAX_PATH_LEN];
        snprintf(vbmeta_path, sizeof(vbmeta_path), "%s%cvbmetas%c%s.vbmeta",
                 exe_dir, PATH_SEP, PATH_SEP, base_name);

        const char *flash_image = current_image;
        char temp_image[MAX_PATH_LEN] = {0};

        if (file_exists(vbmeta_path)) {
            printf("检测到对应的 vbmeta 备份: %s\n", vbmeta_path);
            printf("正在开始修补(移植) VBMeta 到镜像中...\n");

            char temp_dir[MAX_PATH_LEN];
            snprintf(temp_dir, sizeof(temp_dir), "%s%ctemp", exe_dir, PATH_SEP);
            mkdir_p(temp_dir);

            snprintf(temp_image, sizeof(temp_image), "%s%c%s.img",
                     temp_dir, PATH_SEP, current_partition);

            if (transplant_vbmeta(vbmeta_path, current_image, temp_image) != 0) {
                fprintf(stderr, "错误：VBMeta 修补失败！\n");
                remove(temp_image);
                goto loop_end;
            }

            printf("\n>> [成功] 镜像修补完成！新镜像已生成 -> %s <<\n", temp_image);
            flash_image = temp_image;
        } else {
            printf("注意：未找到 '%s' 的 vbmeta 备份，将直接刷写原镜像。\n", base_name);
        }

        /* 准备进入 Fastboot 刷写 */
        printf("\n请确保手机已进入 Fastboot 模式并连接至电脑。\n");
        printf("按 [回车键] 开始刷写镜像...");
        fflush(stdout);
        clear_input_buffer();

        printf("\n正在刷写 %s -> %s ...\n", flash_image, current_partition);
        int flash_ret = flash_partition(current_partition, flash_image);

        /* 清理临时修补文件 */
        if (temp_image[0])
            remove(temp_image);

        if (flash_ret == 0) {
            printf("\n>> [成功] Fastboot 刷写完成！ <<\n");
        } else {
            printf("\n>> [失败] Fastboot 刷写遇到错误。 <<\n");
        }

        /* 重启选择与后续控制菜单 */
        while (1) {
            printf("\n=============================\n");
            printf(" 请选择接下来的操作：\n");
            printf(" 1. 重启到系统 (System)\n");
            printf(" 2. 重启到恢复模式 (Recovery)\n");
            printf(" 3. 重启到用户空间 Fastboot (Fastbootd)\n");
            printf(" 4. 重启到引导加载程序 (Bootloader)\n");
            printf(" 5. 不重启，继续修补/刷写其他镜像\n");
            printf("=============================\n");
            
            char choice[16];
            read_line("请输入选项 (1-5): ", choice, sizeof(choice));

            if (strcmp(choice, "1") == 0) {
                char cmd[MAX_CMD_LEN];
                snprintf(cmd, sizeof(cmd), "%s reboot", fastboot_path);
                system(cmd);
                break;
            } else if (strcmp(choice, "2") == 0) {
                char cmd[MAX_CMD_LEN];
                snprintf(cmd, sizeof(cmd), "%s reboot recovery", fastboot_path);
                system(cmd);
                break;
            } else if (strcmp(choice, "3") == 0) {
                char cmd[MAX_CMD_LEN];
                snprintf(cmd, sizeof(cmd), "%s reboot fastboot", fastboot_path);
                system(cmd);
                break;
            } else if (strcmp(choice, "4") == 0) {
                char cmd[MAX_CMD_LEN];
                snprintf(cmd, sizeof(cmd), "%s reboot-bootloader", fastboot_path);
                system(cmd);
                break;
            } else if (strcmp(choice, "5") == 0) {
                // 不重启直接开启下一轮
                goto next_loop;
            } else {
                printf("无效输入，请重新选择。\n");
            }
        }

        // 如果上面执行了重启命令 (1-4)，程序会运行到这里
        printf("\n重启命令已发送。");
        
    loop_end:
        // 只有选择重启后，或者在上面的步骤中报错跳出时，才会来到这里询问退出或继续
        while (1) {
            printf("\n选项：\n [C] 继续修补下一个镜像\n [E] 退出程序\n");
            char exit_choice[16];
            read_line("请选择 (C/E): ", exit_choice, sizeof(exit_choice));
            if (exit_choice[0] == 'c' || exit_choice[0] == 'C') {
                break; 
            } else if (exit_choice[0] == 'e' || exit_choice[0] == 'E') {
                printf("感谢使用，正在退出...\n");
                return 0;
            } else {
                printf("无效输入。\n");
            }
        }

    next_loop:
        /* 如果当初是从命令行传参进来的，在进入第二轮循环时需要清空，防止死循环刷同一个镜像 */
        partition_arg = NULL;
        image_arg = NULL;
    }

    return 0;
}
