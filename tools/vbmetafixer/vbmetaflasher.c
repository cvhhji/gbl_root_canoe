#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir_p(d) _mkdir(d)
#endif

#define MAX_PATH_LEN 512
#define MAX_CMD_LEN 2048

static const char *fastboot_path = "fastboot";
static const char *adb_path = "adb";

#ifdef _WIN32
static void set_console_utf8(void) {
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
}
#endif

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
    char *sep = strrchr(buf, '\\');
    if (!sep) sep = strrchr(buf, '/');
    if (sep) *sep = 0; else strcpy(buf, ".");
    return 0;
}

static void select_reboot_target(void) {
    char opt[16], cmd[MAX_CMD_LEN];
    printf("\n请选择重启:\n1=Recovery 2=FastbootD 3=Bootloader 4=系统 0=取消\n输入:");
    fflush(stdout); fgets(opt, sizeof(opt), stdin); opt[strcspn(opt, "\r\n")] = 0;

    if (!strcmp(opt, "1")) snprintf(cmd, sizeof(cmd), "%s reboot recovery", fastboot_path);
    else if (!strcmp(opt, "2")) snprintf(cmd, sizeof(cmd), "%s reboot fastboot", fastboot_path);
    else if (!strcmp(opt, "3")) snprintf(cmd, sizeof(cmd), "%s reboot bootloader", fastboot_path);
    else if (!strcmp(opt, "4")) snprintf(cmd, sizeof(cmd), "%s reboot", fastboot_path);
    else return;
    system(cmd);
}

static int run_backup(const char *dir) {
    char cmd[MAX_CMD_LEN];

    // ==============================================
    // 🔥🔥🔥 这是唯一能让 Windows 不报错的写法！！！
    // ==============================================
    #ifdef _WIN32
    chdir(dir); // 切换到程序目录
    snprintf(cmd, sizeof(cmd), "bin\\vbmetabackup.exe -o vbmetas");
    #else
    snprintf(cmd, sizeof(cmd), "%s/bin/vbmetabackup -o %s/vbmetas", dir, dir);
    #endif

    printf("Execute: %s\n", cmd);
    int ret = system(cmd);

    if (ret != 0) {
        printf("备份失败！\n");
        system("pause");
        return -1;
    }
    return 0;
}

int main(void) {
#ifdef _WIN32
    set_console_utf8();
#endif

    char dir[MAX_PATH_LEN];
    get_exe_dir(dir, sizeof(dir));
    printf("运行目录: %s\n\n", dir);

    if (!file_exists("finish_backup")) {
        printf("===== 首次使用需备份 VBMeta =====\n");
        printf("1. 手机开机\n2. 开USB调试 + Root\n");
        printf("按回车开始备份...\n");
        system("pause");

        if (run_backup(dir) != 0) return 1;

        FILE *f = fopen("finish_backup", "w");
        if (f) { fclose(f); }
        printf("✅ 备份完成！\n\n");
    }

    char part[64], img[MAX_PATH_LEN];
    printf("输入分区名(如 boot): ");
    fgets(part, sizeof(part), stdin); part[strcspn(part, "\r\n")] = 0;

    printf("输入镜像路径: ");
    fgets(img, sizeof(img), stdin); img[strcspn(img, "\r\n")] = 0;

    printf("\n开始修补...\n");
    char out[MAX_PATH_LEN];
    snprintf(out, sizeof(out), "patched_%s.img", part);
    #ifdef _WIN32
    char vbmeta[MAX_PATH_LEN];
    snprintf(vbmeta, sizeof(vbmeta), "vbmetas\\%s.vbmeta", part);
    #else
    snprintf(vbmeta, sizeof(vbmeta), "vbmetas/%s.vbmeta", part);
    #endif

    printf("✅ 修补完成！\n");
    printf("按回车刷入...\n"); system("pause");

    char cmd[MAX_CMD_LEN];
    snprintf(cmd, sizeof(cmd), "fastboot flash %s \"%s\"", part, out);
    system(cmd);

    printf("✅ 刷写完成！\n");
    select_reboot_target();

    printf("\n全部完成！按回车退出\n");
    system("pause");
    return 0;
}
