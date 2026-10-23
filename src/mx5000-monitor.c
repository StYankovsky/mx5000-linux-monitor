#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/limits.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/*
 * MX5000 Monitor for Linux
 * Initial C implementation: collect common system metrics, listen for the
 * four LCD buttons, and ask mx5000-tool to render the selected page.
 */

#define REFRESH_SECONDS 5
#define VENDOR_PRODUCT "0005:046D:B305."
#define DEFAULT_TOOL "mx5000-tool"

static volatile sig_atomic_t keep_running = 1;
static int current_page = 1;
static int input_fd = -1;
static char tool_path[PATH_MAX] = DEFAULT_TOOL;
static char input_path[PATH_MAX] = "";
static char hidraw_path[PATH_MAX] = "";
static unsigned long long previous_total = 0, previous_idle = 0;
static double previous_rx = 0, previous_tx = 0;
static bool have_net_sample = false;

static void on_signal(int sig) { (void)sig; keep_running = 0; }

static bool path_contains_device_id(const char *path) {
    return path && strstr(path, VENDOR_PRODUCT) != NULL;
}

static int find_device(const char *class_dir, const char *prefix,
                       char *out, size_t out_size, bool require_name) {
    DIR *dir = opendir(class_dir);
    if (!dir) return -1;
    struct dirent *entry;
    int found = -1;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, prefix, strlen(prefix)) != 0) continue;
        char sys_path[PATH_MAX], resolved[PATH_MAX], dev_path[PATH_MAX];
        snprintf(sys_path, sizeof(sys_path), "%s/%s/device", class_dir, entry->d_name);
        if (!realpath(sys_path, resolved) || !path_contains_device_id(resolved)) continue;
        if (require_name) {
            char name_path[PATH_MAX], name[256] = "";
            snprintf(name_path, sizeof(name_path), "%s/%s/device/name", class_dir, entry->d_name);
            FILE *f = fopen(name_path, "r");
            if (!f) continue;
            if (fgets(name, sizeof(name), f) == NULL) {
              fclose(f);
              continue;
            }
            fclose(f);

        if (!strstr(name, "MX5000")) continue;
        }
        /* Input events live in /dev/input/, while hidraw devices
         * are located directly in /dev/. */
        if (strcmp(prefix, "event") == 0) {
            snprintf(dev_path, sizeof(dev_path), "/dev/input/%s", entry->d_name);
        } else {
            snprintf(dev_path, sizeof(dev_path), "/dev/%s", entry->d_name);
        }

        if (strlen(dev_path) + 1 > out_size) continue;
        strcpy(out, dev_path);
        found = 0;
        break;
    }
    closedir(dir);
    return found;
}

static int discover_devices(void) {
    char new_input[PATH_MAX] = "", new_hidraw[PATH_MAX] = "";
    int input_ok = find_device("/sys/class/input", "event", new_input,
                               sizeof(new_input), true);
    int hid_ok = find_device("/sys/class/hidraw", "hidraw", new_hidraw,
                             sizeof(new_hidraw), false);
    if (input_ok == 0 && strcmp(input_path, new_input) != 0) {
        if (input_fd >= 0) close(input_fd);
        input_fd = -1;
        strcpy(input_path, new_input);
    }
    if (hid_ok == 0) strcpy(hidraw_path, new_hidraw);
    else hidraw_path[0] = '\0';
    if (input_fd < 0 && input_path[0]) {
        input_fd = open(input_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (input_fd < 0) {
            fprintf(stderr, "Cannot open %s: %s\n", input_path, strerror(errno));
            input_path[0] = '\0';
        } else {
            fprintf(stderr, "Listening for MX5000 buttons on %s\n", input_path);
        }
    }
    if (input_ok != 0) {
        if (input_fd >= 0) close(input_fd);
        input_fd = -1;
        input_path[0] = '\0';
    }
    return hidraw_path[0] ? 0 : -1;
}

static double read_cpu_percent(void) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0.0;
    unsigned long long user=0,nice=0,system=0,idle=0,iowait=0,irq=0,softirq=0,steal=0;
    int n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &user,&nice,&system,&idle,&iowait,&irq,&softirq,&steal);
    fclose(f);
    if (n < 4) return 0.0;
    unsigned long long idle_all = idle + iowait;
    unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
    double result = 0.0;
    if (previous_total && total > previous_total) {
        unsigned long long dt = total - previous_total;
        unsigned long long di = idle_all - previous_idle;
        result = 100.0 * (double)(dt - (di > dt ? dt : di)) / (double)dt;
    }
    previous_total = total;
    previous_idle = idle_all;
    return result;
}

static double read_temperature(void) {
    DIR *dir = opendir("/sys/class/thermal");
    if (!dir) return -1.0;
    struct dirent *entry;
    double max_temp = -1.0;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "thermal_zone", 11) != 0) continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/sys/class/thermal/%s/temp", entry->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        long value = 0;
        int ok = fscanf(f, "%ld", &value);
        fclose(f);
        if (ok == 1) {
            double c = (value > 1000 || value < -1000) ? value / 1000.0 : (double)value;
            if (c >= 0.0 && c <= 120.0 && c > max_temp) max_temp = c;
        }
    }
    closedir(dir);
    return max_temp;
}

static void read_memory(unsigned long long *used_mb, unsigned long long *total_mb,
                        unsigned long long *swap_used_mb) {
    FILE *f = fopen("/proc/meminfo", "r");
    unsigned long long total=0, available=0, swap_total=0, swap_free=0;
    if (f) {
        char key[64]; unsigned long long value; char unit[16];
        while (fscanf(f, "%63s %llu %15s", key, &value, unit) >= 2) {
            if (!strcmp(key,"MemTotal:")) total=value;
            else if (!strcmp(key,"MemAvailable:")) available=value;
            else if (!strcmp(key,"SwapTotal:")) swap_total=value;
            else if (!strcmp(key,"SwapFree:")) swap_free=value;
            int ch; while ((ch=fgetc(f)) != '\n' && ch != EOF) {}
        }
        fclose(f);
    }
    *total_mb = total / 1024;
    *used_mb = total > available ? (total - available) / 1024 : 0;
    *swap_used_mb = swap_total > swap_free ? (swap_total - swap_free) / 1024 : 0;
}

static void read_disk(unsigned long long *used_gb, unsigned long long *free_gb,
                      unsigned long long *total_gb, double *used_percent) {
    struct statvfs s;
    if (statvfs("/", &s) != 0) { *used_gb=*free_gb=*total_gb=0; *used_percent=0; return; }
    unsigned long long unit = s.f_frsize ? s.f_frsize : s.f_bsize;
    unsigned long long total = (unsigned long long)s.f_blocks * unit;
    unsigned long long freeb = (unsigned long long)s.f_bavail * unit;
    unsigned long long used = total > freeb ? total-freeb : 0;
    *total_gb = total / (1024ULL*1024*1024);
    *free_gb = freeb / (1024ULL*1024*1024);
    *used_gb = used / (1024ULL*1024*1024);
    *used_percent = total ? (100.0 * (double)used / (double)total) : 0.0;
}



static void read_network(char *iface, size_t iface_size,
                         double *rx_kbs, double *tx_kbs,
                         char *ip, size_t ip_size,
                         char *gateway, size_t gw_size) {
    snprintf(iface, iface_size, "n/a");
    snprintf(ip, ip_size, "IP:n/a");
    snprintf(gateway, gw_size, "GW:n/a");
    *rx_kbs = *tx_kbs = 0.0;

    unsigned long long rx_bytes = 0, tx_bytes = 0;
    bool counters_found = false;

    FILE *f = fopen("/proc/net/route", "r");
    if (f) {
        char line[512];

        if (fgets(line, sizeof(line), f) != NULL) {
            while (fgets(line, sizeof(line), f) != NULL) {
                char name[IFNAMSIZ];
                unsigned long dest = 1, gw = 0;
                unsigned int flags = 0;

                if (sscanf(line, "%15s %lx %lx %X",
                           name, &dest, &gw, &flags) == 4 &&
                    dest == 0 && (flags & 1)) {
                    snprintf(iface, iface_size, "%s", name);

                    unsigned char *b = (unsigned char *)&gw;
                    char addr[16];

                    snprintf(addr, sizeof(addr), "%u.%u.%u.%u",
                             b[0], b[1], b[2], b[3]);

                    snprintf(gateway, gw_size, "%s%s",
                             strlen(addr) >= 13 ? "G" : "GW:",
                             addr);
                    break;
                }
            }
        }
        fclose(f);
    }

    if (strcmp(iface, "n/a") != 0) {
        FILE *p = fopen("/proc/net/dev", "r");
        if (p) {
            char line[512];

            if (fgets(line, sizeof(line), p) != NULL &&
                fgets(line, sizeof(line), p) != NULL) {
                while (fgets(line, sizeof(line), p) != NULL) {
                    char name[IFNAMSIZ];
                    char *colon = strchr(line, ':');
                    if (!colon) continue;
                    *colon = '\0';

                    char *start_name = line;
                    while (*start_name == ' ') start_name++;

                    if (sscanf(start_name, "%15s", name) != 1 ||
                        strcmp(name, iface) != 0) {
                        continue;
                    }

                    unsigned long long rx_packets, rx_errs, rx_drop;
                    unsigned long long rx_fifo, rx_frame, rx_compressed;
                    unsigned long long rx_multicast;
                    unsigned long long tx_packets, tx_errs, tx_drop;
                    unsigned long long tx_fifo, tx_colls, tx_carrier;
                    unsigned long long tx_compressed;

                    if (sscanf(colon + 1,
                        "%llu %llu %llu %llu %llu %llu %llu %llu "
                        "%llu %llu %llu %llu %llu %llu %llu %llu",
                        &rx_bytes, &rx_packets, &rx_errs, &rx_drop,
                        &rx_fifo, &rx_frame, &rx_compressed,
                        &rx_multicast, &tx_bytes, &tx_packets, &tx_errs,
                        &tx_drop, &tx_fifo, &tx_colls, &tx_carrier,
                        &tx_compressed) == 16) {
                        counters_found = true;
                    }
                    break;
                }
            }
            fclose(p);
        }
    }

    /*
     * Convert cumulative byte counters to KiB/s using monotonic time.
     * The first successful sample only establishes the baseline.
     */
    static struct timespec previous_time;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0 && counters_found) {
        if (have_net_sample) {
            double elapsed =
                (double)(now.tv_sec - previous_time.tv_sec) +
                (double)(now.tv_nsec - previous_time.tv_nsec) / 1e9;

            if (elapsed > 0.0 &&
                (double)rx_bytes >= previous_rx &&
                (double)tx_bytes >= previous_tx) {
                *rx_kbs = ((double)rx_bytes - previous_rx) / elapsed / 1024.0;
                *tx_kbs = ((double)tx_bytes - previous_tx) / elapsed / 1024.0;
            }
        }

        previous_rx = (double)rx_bytes;
        previous_tx = (double)tx_bytes;
        previous_time = now;
        have_net_sample = true;
    } else {
        have_net_sample = false;
    }

    if (strcmp(iface, "n/a") != 0) {
        char command[128];
        snprintf(command, sizeof(command),
                 "ip -4 -o addr show dev %s 2>/dev/null", iface);

        FILE *pipe = popen(command, "r");
        if (pipe) {
            char line[512];

            if (fgets(line, sizeof(line), pipe) != NULL) {
                char *marker = strstr(line, " inet ");
                if (marker) {
                    marker += 6;
                    char *end = strchr(marker, '/');
                    if (end) {
                        size_t len = (size_t)(end - marker);
                        if (len > 0 && len < sizeof(line)) {
                            char addr[16];

                            if (len < sizeof(addr)) {
                                memcpy(addr, marker, len);
                                addr[len] = '\0';

                                snprintf(ip, ip_size, "%s%s",
                                         len >= 13 ? "I" : "IP:",
                                         addr);
                            }
                        }
                    }
                }
            }
            (void)pclose(pipe);
        }
    }
}

static void make_lines(char lines[5][128]) {
    time_t now=time(NULL); struct tm tm_now; localtime_r(&now,&tm_now);
    char date[32], clock_text[32];
    strftime(clock_text,sizeof(clock_text),"%H:%M:%S",&tm_now);
    strftime(date,sizeof(date),"%Y.%m.%d",&tm_now);
    double cpu=read_cpu_percent(), temp=read_temperature();
    unsigned long long mem_used,mem_total,swap_used,disk_used,disk_free,disk_total; double disk_pct;
    read_memory(&mem_used,&mem_total,&swap_used); read_disk(&disk_used,&disk_free,&disk_total,&disk_pct);
    char iface[IFNAMSIZ],ip[64],gw[64]; double rx,tx;
    read_network(iface,sizeof(iface),&rx,&tx,ip,sizeof(ip),gw,sizeof(gw));
    struct sysinfo si; memset(&si,0,sizeof(si)); (void)sysinfo(&si);
    double loads[3]={0}; (void)getloadavg(loads,3);
    double load1=loads[0], load5=loads[1], load15=loads[2];
    char temp_text[24]; if(temp<0) strcpy(temp_text,"n/a"); else snprintf(temp_text,sizeof(temp_text),"%.0fC",temp);
    switch(current_page) {
      case 1:
        snprintf(lines[0], 128, "%.5s %.10s", clock_text, date);
        snprintf(lines[1], 128, "CPU;%.0f%% T%.4s", cpu, temp_text);
        snprintf(lines[2], 128, "RAM:%.1f/%.1fG",
             mem_used / 1024.0, mem_total / 1024.0);
         snprintf(lines[3], 128, "D. free:%lluG", disk_free);
        snprintf(lines[4], 128, "RX:%.0f TX:%.0fK", rx, tx);
        break;
      case 2:
        snprintf(lines[0], 128, "NET:%.12s", iface);
        snprintf(lines[1], 128, "%s", ip);
        snprintf(lines[2], 128, "%s", gw);
        snprintf(lines[3], 128, "RX:%.0fK/s", rx);
        snprintf(lines[4], 128, "TX:%.0fK/s", tx);
        break;
      case 3:
        snprintf(lines[0], 128, "RAM:%llu/%lluM",
             mem_used, mem_total);
        snprintf(lines[1], 128, "SWAP:%lluM", swap_used);
        snprintf(lines[2], 128, "DISK:%llu/%lluG",
             disk_used, disk_total);
        snprintf(lines[3], 128, "Disk:%.0f%% used", disk_pct);
        snprintf(lines[4], 128, "Free:%lluG", disk_free);
        break;
      default:
        snprintf(lines[0], 128, "UP:%.1fd",
             (double)si.uptime / 86400.0);
        snprintf(lines[1], 128, "L:%.1f/%.1f/%.1f",
             load1, load5, load15);
        snprintf(lines[2], 128, "CPU:%.0f%% T%.4s", cpu, temp_text);
        snprintf(lines[3], 128, "RAM:%.1f/%.1fG",
             mem_used / 1024.0, mem_total / 1024.0);
        snprintf(lines[4], 128, "SWAP:%lluM", swap_used);
        break;
    }
    char page_text[3];
    snprintf(page_text, sizeof(page_text), "P%d", current_page);

    char last_line[15];
    snprintf(last_line, sizeof(last_line), "%.14s", lines[4]);
    snprintf(lines[4], sizeof(lines[4]), "%-14s%s", last_line, page_text);
}

static void render_page(void) {
    if (!hidraw_path[0]) return;
    char lines[5][128]; make_lines(lines);
    pid_t pid=fork();
    if (pid < 0) { perror("fork mx5000-tool"); return; }
    if (pid == 0) {
        int nullfd=open("/dev/null",O_WRONLY);
        if(nullfd>=0){dup2(nullfd,STDOUT_FILENO);dup2(nullfd,STDERR_FILENO);close(nullfd);}
        execlp(tool_path,tool_path,"-d",hidraw_path,"--static",
               "--add-text",lines[0],"reg","0","7",
               "--add-text",lines[1],"reg","0","15",
               "--add-text",lines[2],"reg","0","23",
               "--add-text",lines[3],"reg","0","31",
               "--add-text",lines[4],"reg","0","40",(char *)NULL);
        _exit(127);
    }
    int status; while(waitpid(pid,&status,0)<0 && errno==EINTR) {}
}

static void handle_buttons(void) {
    if(input_fd<0) return;
    struct input_event ev[16];
    ssize_t n=read(input_fd,ev,sizeof(ev));
    if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) {
        close(input_fd); input_fd=-1; input_path[0]='\0'; return;
    }
    if(n<=0) return;
    size_t count=(size_t)n/sizeof(struct input_event);
    for(size_t i=0;i<count;i++) if(ev[i].type==EV_KEY && ev[i].value==1) {
        int page=0;
        switch(ev[i].code) { case 696: page=1; break; case 697: page=2; break; case 698: page=3; break; case 699: page=4; break; default: break; }
        if(page && page!=current_page) { current_page=page; fprintf(stderr,"Selected page %d\n",page); render_page(); }
    }
}

static void usage(const char *argv0) {
    fprintf(stderr,"Usage: %s [-t /path/to/mx5000-tool] [-p 1-4]\n",argv0);
}

int main(int argc,char **argv) {
    int opt;
    while((opt=getopt(argc,argv,"t:p:h"))!=-1) {
        switch(opt) {
          case 't': snprintf(tool_path,sizeof(tool_path),"%s",optarg); break;
          case 'p': current_page=atoi(optarg); if(current_page<1||current_page>4){usage(argv[0]);return 2;} break;
          default: usage(argv[0]); return opt=='h'?0:2;
        }
    }
    signal(SIGINT,on_signal); signal(SIGTERM,on_signal);
    fprintf(stderr,"MX5000 Monitor starting (refresh every %d seconds)\n",REFRESH_SECONDS);
    while(keep_running) {
        (void)discover_devices();
        render_page();
        for(int i=0;i<REFRESH_SECONDS*10 && keep_running;i++) {
            handle_buttons();
            usleep(100000);
            /* Re-scan periodically so unplug/replug does not require restart. */
            if(i>0 && i%10==0) (void)discover_devices();
        }
    }
    if(input_fd>=0) close(input_fd);
    fprintf(stderr,"MX5000 Monitor stopped\n");
    return 0;
}
