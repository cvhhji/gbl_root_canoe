// 从 Magisk v28.1 magiskboot 扩展的 vendor_boot 修补逻辑
// 集成进 magiskboot，直接调用 unpack/repack API + rust::cpio_commands，
// 不依赖外部 mboot 二进制。支持 vendor_boot header v4（vendor_ramdisk）。
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "magiskboot.hpp"
#include "boot-rs.hpp"



static bool write_all(int fd, const void *data, size_t size) {
    auto *p = static_cast<const char *>(data);
    while (size) {
        ssize_t written = write(fd, p, size);
        if (written < 0 && errno == EINTR) continue;
        if (written < 0) return false;
        if (written == 0) {
            errno = EIO;
            return false;
        }
        p += written;
        size -= static_cast<size_t>(written);
    }
    return true;
}

static void print_errno(const char *action, const char *path, int error) {
    fprintf(stdout, "[!] %s %s 失败: %s (errno=%d)\n",
            action, path, strerror(error), error);
}

static bool set_block_writable(const char *path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        print_errno("打开块设备", path, errno);
        return false;
    }

    int read_only = 0;
    if (ioctl(fd, BLKROGET, &read_only) == 0 && read_only) {
        read_only = 0;
        if (ioctl(fd, BLKROSET, &read_only) != 0) {
            int error = errno;
            close(fd);
            print_errno("解除只读", path, error);
            return false;
        }
    }

    if (close(fd) != 0) {
        print_errno("关闭块设备", path, errno);
        return false;
    }
    return true;
}

static bool verify_written_image(const char *image, const char *block) {
    int image_fd = open(image, O_RDONLY | O_CLOEXEC);
    if (image_fd < 0) {
        print_errno("打开校验镜像", image, errno);
        return false;
    }
    int block_fd = open(block, O_RDONLY | O_CLOEXEC);
    if (block_fd < 0) {
        int error = errno;
        close(image_fd);
        print_errno("打开校验分区", block, error);
        return false;
    }

    char image_buf[65536];
    char block_buf[65536];
    bool ok = true;
    for (;;) {
        ssize_t image_size;
        do {
            image_size = read(image_fd, image_buf, sizeof(image_buf));
        } while (image_size < 0 && errno == EINTR);
        if (image_size < 0) {
            print_errno("读取校验镜像", image, errno);
            ok = false;
            break;
        }
        if (image_size == 0) break;

        size_t offset = 0;
        while (offset < static_cast<size_t>(image_size)) {
            ssize_t block_size = read(block_fd, block_buf + offset,
                                      static_cast<size_t>(image_size) - offset);
            if (block_size < 0 && errno == EINTR) continue;
            if (block_size <= 0) {
                if (block_size < 0) {
                    print_errno("读取校验分区", block, errno);
                } else {
                    fprintf(stdout, "[!] 校验分区提前结束: %s\n", block);
                }
                ok = false;
                break;
            }
            offset += static_cast<size_t>(block_size);
        }
        if (!ok ||
            memcmp(image_buf, block_buf, static_cast<size_t>(image_size)) != 0) {
            if (ok) fprintf(stdout, "[!] 写入后校验不一致: %s\n", block);
            ok = false;
            break;
        }
    }

    if (close(image_fd) != 0) ok = false;
    if (close(block_fd) != 0) ok = false;
    return ok;
}

static bool copy_file(const char *src, const char *dst, bool flush = false) {
    int sfd = open(src, O_RDONLY | O_CLOEXEC);
    if (sfd < 0) {
        print_errno("打开源文件", src, errno);
        return false;
    }
    if (flush && !set_block_writable(dst)) {
        close(sfd);
        return false;
    }

    int flags = O_WRONLY | O_CLOEXEC | (flush ? 0 : O_CREAT | O_TRUNC);
    int dfd = open(dst, flags, 0644);
    if (dfd < 0) {
        int error = errno;
        close(sfd);
        print_errno("打开目标", dst, error);
        return false;
    }

    if (flush) {
        struct stat source_info{};
        uint64_t block_size = 0;
        if (fstat(sfd, &source_info) != 0) {
            print_errno("读取镜像大小", src, errno);
            close(sfd);
            close(dfd);
            return false;
        }
        if (ioctl(dfd, BLKGETSIZE64, &block_size) == 0 &&
            static_cast<uint64_t>(source_info.st_size) > block_size) {
            fprintf(stdout, "[!] 镜像大于目标分区: %lld > %llu bytes\n",
                    static_cast<long long>(source_info.st_size),
                    static_cast<unsigned long long>(block_size));
            close(sfd);
            close(dfd);
            return false;
        }
    }

    char buf[65536];
    bool ok = true;
    for (;;) {
        ssize_t n = read(sfd, buf, sizeof(buf));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            if (n < 0) ok = false;
            break;
        }
        if (!write_all(dfd, buf, static_cast<size_t>(n))) {
            print_errno("写入目标", dst, errno);
            ok = false;
            break;
        }
    }
    if (flush && ok && fsync(dfd) != 0) {
        print_errno("同步目标", dst, errno);
        ok = false;
    }
    if (close(sfd) != 0) {
        print_errno("关闭源文件", src, errno);
        ok = false;
    }
    if (close(dfd) != 0) {
        print_errno("关闭目标", dst, errno);
        ok = false;
    }
    if (flush && ok) {
        int block_fd = open(dst, O_RDONLY | O_CLOEXEC);
        if (block_fd >= 0) {
            ioctl(block_fd, BLKFLSBUF, 0);
            close(block_fd);
        }
        ok = verify_written_image(src, dst);
    }
    return ok;
}

class WorkDir {
public:
    WorkDir() {
        bool created = false;
        for (unsigned int attempt = 0; attempt < 100; ++attempt) {
            int len = ssprintf(path_, sizeof(path_), ".patch_tools.%d.%u",
                               getpid(), attempt);
            if (len <= 0 || static_cast<size_t>(len) >= sizeof(path_)) return;
            if (mkdir(path_, 0700) == 0) {
                created = true;
                break;
            }
            if (errno != EEXIST) return;
        }
        if (!created) return;
        if (chdir(path_) != 0) {
            rmdir(path_);
            return;
        }
        ready_ = true;
    }

    ~WorkDir() {
        if (!ready_) return;
        if (chdir("..") == 0) rm_rf(path_);
    }

    explicit operator bool() const { return ready_; }

private:
    char path_[64]{};
    bool ready_ = false;
};

static const char *fstab_content =
"# Copyright (c) 2019-2020 The Linux Foundation. All rights reserved.\n"
"#\n"
"# Redistribution and use in source and binary forms, with or without\n"
"# modification, are permitted (subject to the limitations in the\n"
"# disclaimer below) provided that the following conditions are met:\n"
"#\n"
"#    * Redistributions of source code must retain the above copyright\n"
"#      notice, this list of conditions and the following disclaimer.\n"
"#\n"
"#    * Redistributions in binary form must reproduce the above\n"
"#      copyright notice, this list of conditions and the following\n"
"#      disclaimer in the documentation and/or other materials provided\n"
"#      with the distribution.\n"
"#\n"
"#    * Neither the name of The Linux Foundation nor the names of its\n"
"#      contributors may be used to endorse or promote products derived\n"
"#      from this software without specific prior written permission.\n"
"#\n"
"# NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE\n"
"# GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT\n"
"# HOLDERS AND CONTRIBUTORS \"AS IS\" AND ANY EXPRESS OR IMPLIED\n"
"# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF\n"
"# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.\n"
"# IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR\n"
"# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL\n"
"# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE\n"
"# GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS\n"
"# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER\n"
"# IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR\n"
"# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN\n"
"# IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n"
"\n"
"# Android fstab file.\n"
"# The filesystem that contains the filesystem checker binary (typically /system) cannot\n"
"# specify MF_CHECK, and must come before any filesystems that do specify MF_CHECK\n"
"\n"
"#<src>                                                 <mnt_point>            <type>  <mnt_flags and options>                            <fs_mgr_flags>\n"
"/dev/block/by-name/oplusreserve2          /mnt/vendor/oplusreserve             ext4   nosuid,nodev,noatime,barrier=1                           wait,check,nofail,first_stage_mount\n"
"system                                                  /system                ext4    ro,barrier=1,discard                                 wait,slotselect,avb=vbmeta_system,logical,first_stage_mount\n"
"system                                                  /system                erofs   ro                               wait,slotselect,avb=vbmeta_system,logical,first_stage_mount\n"
"system_ext                                              /system_ext            ext4    ro,barrier=1,discard                                 wait,slotselect,logical,first_stage_mount\n"
"system_ext                                              /system_ext            erofs   ro                               wait,slotselect,logical,first_stage_mount\n"
"product                                                 /product               ext4    ro,barrier=1,discard                                 wait,slotselect,logical,first_stage_mount\n"
"product                                                 /product               erofs   ro                               wait,slotselect,logical,first_stage_mount\n"
"vendor                                                  /vendor                ext4    ro,barrier=1,discard                                 wait,slotselect,avb=vbmeta_vendor,logical,first_stage_mount\n"
"vendor                                                  /vendor                erofs   ro                               wait,slotselect,avb=vbmeta_vendor,logical,first_stage_mount\n"
"vendor_dlkm                                             /vendor_dlkm           ext4    ro,barrier=1,discard                                 wait,slotselect,logical,first_stage_mount\n"
"vendor_dlkm                                             /vendor_dlkm           erofs   ro                               wait,slotselect,logical,first_stage_mount\n"
"system_dlkm                                             /system_dlkm           ext4    ro,barrier=1,discard                                 wait,slotselect,avb=vbmeta,logical,first_stage_mount\n"
"system_dlkm                                             /system_dlkm           erofs   ro                               wait,slotselect,avb=vbmeta,logical,first_stage_mount\n"
"odm                                                     /odm                   ext4    ro,barrier=1,discard                                 wait,slotselect,logical,first_stage_mount\n"
"odm                                                     /odm                   erofs   ro                               wait,slotselect,logical,first_stage_mount\n"
"/dev/block/by-name/boot                                 /boot                  emmc    defaults                                             slotselect,first_stage_mount\n"
"/dev/block/by-name/init_boot                            /init_boot             emmc    defaults                                             slotselect,first_stage_mount\n"
"/dev/block/by-name/vendor_boot                          /vendor_boot           emmc    defaults                                             slotselect,first_stage_mount\n"
"/dev/block/by-name/dtbo                                 /dtbo                  emmc    defaults                                             slotselect,first_stage_mount\n"
"/dev/block/by-name/recovery                             /recovery              emmc    defaults                                             slotselect,first_stage_mount\n"
"# Mount my_xx to /my_xxx in first_stage_mount\n"
"my_product                 /my_product                             ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_product                 /my_product                             erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_engineering             /my_engineering                         ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_engineering             /my_engineering                         erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_company                 /my_company                             ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_company                 /my_company                             erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_carrier                 /my_carrier                             ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_carrier                 /my_carrier                             erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_region                  /my_region                              ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_region                  /my_region                              erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_heytap                  /my_heytap                              ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_heytap                  /my_heytap                              erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_stock                   /my_stock                               ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_stock                   /my_stock                               erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_preload                 /my_preload                             ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_preload                 /my_preload                             erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_bigball                 /my_bigball                             ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_bigball                 /my_bigball                             erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_manifest                /my_manifest                            ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_manifest                /my_manifest                            erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"# Mount app,priv-app,lib64,lib of my_region/my_preload/my_heytap/my_engineering over /product/\n"
"overlay-overlay            /product/app                            overlay ro,seclabel,noatime,redirect_dir=nofollow,userxattr,lowerdir=/my_region/app:/my_preload/app:/my_product/app:/my_heytap/app:/my_stock/app:/my_engineering/app:/product/app                                    overlayfs_remove_missing_lowerdir\n"
"overlay-overlay            /product/priv-app                       overlay ro,seclabel,noatime,redirect_dir=nofollow,userxattr,lowerdir=/my_region/priv-app:/my_preload/priv-app:/my_product/priv-app:/my_heytap/priv-app:/my_stock/priv-app:/my_engineering/priv-app:/product/priv-app overlayfs_remove_missing_lowerdir\n"
"overlay-overlay            /product/lib64                          overlay ro,seclabel,noatime,redirect_dir=nofollow,userxattr,lowerdir=/my_region/lib64:/my_preload/lib64:/my_product/lib64:/my_heytap/lib64:/my_stock/lib64:/my_engineering/lib64:/product/lib64                      overlayfs_remove_missing_lowerdir\n"
"overlay-overlay            /product/lib                            overlay ro,seclabel,noatime,redirect_dir=nofollow,userxattr,lowerdir=/my_region/lib:/my_preload/lib:/my_product/lib:/my_heytap/lib:/my_stock/lib:/my_engineering/lib:/product/lib                                    overlayfs_remove_missing_lowerdir\n"
"# Mount etc/permissions,framework,media/audio/ui of my_product over /product/\n"
"overlay-overlay            /product/etc/permissions                overlay ro,seclabel,redirect_dir=nofollow,userxattr,lowerdir=/my_product/product_overlay/etc/permissions:/product/etc/permissions                                                                                    overlayfs_remove_missing_lowerdir\n"
"overlay-overlay            /product/framework                      overlay ro,seclabel,redirect_dir=nofollow,userxattr,lowerdir=/my_product/product_overlay/framework:/product/framework                                                                                                overlayfs_remove_missing_lowerdir\n"
"overlay-overlay            /product/media/audio/ui                 overlay ro,seclabel,redirect_dir=nofollow,userxattr,lowerdir=/my_product/product_overlay/media/audio/ui:/product/media/audio/ui                                                                                      overlayfs_remove_missing_lowerdir\n"
"# Mount media/audio/ringtones/,media/audio/notifications/ of my_product over /system_ext\n"
"overlay-overlay            /system_ext/media/audio/ringtones       overlay ro,seclabel,redirect_dir=nofollow,userxattr,lowerdir=/my_product/product_overlay/media/audio/ringtones:/system_ext/media/audio/ringtones                                                                     overlayfs_remove_missing_lowerdir\n"
"overlay-overlay            /system_ext/media/audio/notifications   overlay ro,seclabel,redirect_dir=nofollow,userxattr,lowerdir=/my_product/product_overlay/media/audio/notifications:/system_ext/media/audio/notifications                                                             overlayfs_remove_missing_lowerdir\n"
"/dev/block/by-name/metadata                             /metadata              f2fs    noatime,nosuid,nodev,discard                         wait,check,formattable,first_stage_mount\n"
"/dev/block/bootdevice/by-name/persist                   /mnt/vendor/persist    ext4    noatime,nosuid,nodev,barrier=1                       wait\n"
"/dev/block/bootdevice/by-name/userdata                  /data                  f2fs    noatime,nosuid,nodev,discard,reserve_root=32768,resgid=1065,fsync_mode=nobarrier,inlinecrypt   latemount,wait,check,formattable,fileencryption=aes-256-xts:aes-256-cts:v2+inlinecrypt_optimized+wrappedkey_v0,keydirectory=/metadata/vold/metadata_encryption,metadata_encryption=aes-256-xts:wrappedkey_v0,quota,reservedsize=128M,sysfs_path=/sys/devices/platform/soc/1d84000.ufshc,checkpoint=fs\n"
"/dev/block/by-name/misc                                 /misc                  emmc    defaults                                             defaults\n"
"/devices/platform/soc/8804000.sdhci/mmc_host*           /storage/sdcard1       vfat    nosuid,nodev                                         wait,voldmanaged=sdcard1:auto,encryptable=footer\n"
"/devices/platform/soc/*.ssusb/*.dwc3/xhci-hcd.*.auto*   /storage/usbotg        vfat    nosuid,nodev,readwrite                              wait,voldmanaged=usbotg:auto\n"
"/dev/block/bootdevice/by-name/modem                     /vendor/firmware_mnt   vfat    ro,uid=1000,gid=1000,dmask=227,fmask=337,context=u:object_r:firmware_file:s0 wait,slotselect\n"
"/dev/block/bootdevice/by-name/dsp                       /vendor/dsp            ext4    ro,nosuid,nodev,barrier=1                            wait,slotselect\n"
"/dev/block/bootdevice/by-name/vm-bootsys                /vendor/vm-system     ext4    ro,nosuid,nodev,barrier=1                            wait,slotselect\n"
"/dev/block/bootdevice/by-name/vm-persist                /mnt/vendor/vm-persist     ext4    noatime,nosuid,nodev,barrier=1                  wait\n"
"/dev/block/bootdevice/by-name/bluetooth                 /vendor/bt_firmware    vfat    ro,shortname=lower,uid=1002,gid=3002,dmask=227,fmask=337,context=u:object_r:bt_firmware_file:s0 wait,slotselect\n"
"/dev/block/bootdevice/by-name/qmcs                      /mnt/vendor/qmcs       vfat    noatime,nosuid,nodev,context=u:object_r:vendor_qmcs_file:s0   wait,check,formattable\n"
"/dev/block/bootdevice/by-name/spunvm                    /mnt/vendor/spunvm     vfat    noatime,nosuid,nodev,context=u:object_r:vendor_spunvm_file:s0   wait,check,formattable\n"
"/dev/block/bootdevice/by-name/soccp                     /vendor_soccp_firmware vfat    ro,shortname=lower,uid=0,gid=1000,dmask=227,fmask=337,context=u:object_r:vendor_soccp_file:s0 wait,slotselect\n";

static bool file_exists(const char *filename) {
    struct stat st;
    return stat(filename, &st) == 0;
}

static bool write_file(const char *path, const void *data, size_t size) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    bool ok = write_all(fd, data, size);
    if (close(fd) != 0) ok = false;
    return ok;
}

static bool filter_file(const char *path, std::string_view key) {
    std::string content = full_read(path);
    if (content.empty()) return true;

    std::string out;
    out.reserve(content.size());
    for (size_t pos = 0; pos < content.size();) {
        size_t nl = content.find('\n', pos);
        if (nl == std::string::npos) nl = content.size();
        std::string_view line(content.data() + pos, nl - pos);
        if (!str_contains(line, key)) {
            out.append(line);
            out += '\n';
        }
        pos = nl + 1;
    }
    return out == content || write_file(path, out.data(), out.size());
}

static bool update_header_cmdline(const char *path, std::string_view param) {
    std::string content = full_read(path);
    if (content.empty()) return true;
    if (str_contains(content, param)) return true;

    std::string out;
    out.reserve(content.size() + param.size() + 1);
    bool found = false;
    for (size_t pos = 0; pos < content.size();) {
        size_t nl = content.find('\n', pos);
        if (nl == std::string::npos) nl = content.size();
        size_t len = nl - pos;
        if (len && content[pos + len - 1] == '\r') --len;
        std::string_view line(content.data() + pos, len);
        out.append(line);
        if (str_starts(line, "cmdline=")) {
            out += ' ';
            out += param;
            found = true;
        }
        out += '\n';
        pos = nl + 1;
    }
    return !found || write_file(path, out.data(), out.size());
}

static bool cpio_command(const char *archive, const char *command) {
    const char *args[] = {archive, command};
    return rust::cpio_commands(2, args);
}

static const char *parse_slot(const char *arg) {
    if (!arg) return nullptr;
    if (!strcasecmp(arg, "a") || !strcasecmp(arg, "_a")) return "_a";
    if (!strcasecmp(arg, "b") || !strcasecmp(arg, "_b")) return "_b";
    return nullptr;
}

static bool extract_entry(const char *archive, const char *entry) {
    unlink("tmp_modules.load.recovery");
    char command[256];
    ssprintf(command, sizeof(command), "extract %s tmp_modules.load.recovery", entry);
    return cpio_command(archive, command) && file_exists("tmp_modules.load.recovery");
}

int patch_vendor_boot(int argc, char *argv[]) {
    const char *slot = argc > 0 ? parse_slot(argv[0]) : nullptr;
    if (!slot) {
        if (argc > 0) fprintf(stdout, "[!] 无效的参数: %s\n", argv[0]);
        fprintf(stdout, "[!] 未指定有效槽位 (a/b)\n");
        return 1;
    }
    bool super_mode = argc > 1 && strcmp(argv[1], "super") == 0;
    fprintf(stdout, "[+] 开始修补 %s%s\n", super_mode ? "super" : "vendor_boot", slot);

    char block_path[128];
    ssprintf(block_path, sizeof(block_path), "/dev/block/by-name/vendor_boot%s", slot);
    WorkDir workdir;
    if (!workdir) {
        fprintf(stdout, "[!] 创建临时目录失败！\n");
        return 1;
    }
    if (!copy_file(block_path, "vendor_boot.img")) {
        fprintf(stdout, "[!] 提取 vendor_boot%s 失败！\n", slot);
        return 1;
    }
    if (unpack("vendor_boot.img", false, true) != 0) {
        fprintf(stdout, "[!] 解包 vendor_boot%s 失败！\n", slot);
        return 1;
    }

    const char *target_cpio = file_exists("vendor_ramdisk/ramdisk.cpio")
            ? "vendor_ramdisk/ramdisk.cpio" : "ramdisk.cpio";
    if (!file_exists(target_cpio)) {
        fprintf(stdout, "[!] 解包失败或未找到 ramdisk\n");
        return 1;
    }

    const char *entry = "lib/modules/modules.load.recovery";
    if (!extract_entry(target_cpio, entry)) {
        entry = "modules.load.recovery";
        if (!extract_entry(target_cpio, entry)) {
            fprintf(stdout, "[!] 未找到 modules.load.recovery 文件\n");
            return 1;
        }
    }
    if (!filter_file("tmp_modules.load.recovery", "oplus_secure_guard_new")) {
        fprintf(stdout, "[!] 修改 modules.load.recovery 失败\n");
        return 1;
    }

    char command[256];
    ssprintf(command, sizeof(command), "add 0644 %s tmp_modules.load.recovery", entry);
    if (!cpio_command(target_cpio, command)) {
        fprintf(stdout, "[!] 写回 modules.load.recovery 失败\n");
        return 1;
    }

    if (super_mode) {
        if (!write_file("fstab.qcom", fstab_content, strlen(fstab_content)) ||
            !cpio_command(target_cpio, "add 0644 first_stage_ramdisk/fstab.qcom fstab.qcom")) {
            fprintf(stdout, "[!] 写入 fstab.qcom 失败\n");
            return 1;
        }
    }

    if (file_exists("header") &&
        !update_header_cmdline("header", "module_blacklist=oplus_secure_guard_new")) {
        fprintf(stdout, "[!] 修改 vendor_boot cmdline 失败\n");
        return 1;
    }

    repack("vendor_boot.img", "new_vendor_boot.img");
    if (!file_exists("new_vendor_boot.img")) {
        fprintf(stdout, "[!] 打包 new_vendor_boot.img 失败\n");
        return 1;
    }
    if (!copy_file("new_vendor_boot.img", block_path, true)) {
        fprintf(stdout, "[!] 写入 vendor_boot%s 失败！\n", slot);
        return 1;
    }

    fprintf(stdout, "[+] 修补完成: %s%s\n", super_mode ? "super" : "vendor_boot", slot);
    return 0;
}
