#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cstdio>
#include <vector>
#include <set>
#include <algorithm>
#include <memory>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include "glconf.h"

const char* GLFTPD_LOG      = "/glftpd/ftp-data/logs/glftpd.log";
const char* XFER_LOG        = "/glftpd/ftp-data/logs/xferlog";
const int   SNAPSHOTS[]     = { 1, 5, 10, 15, 20, 25, 30 }; // seconds
const long  REFRESH_RATE    = 50000; // microseconds
const char* IPC_KEY         = "0xDEADBABE";
const int   CUT_OFF         = 60; // seconds

struct Traffic
{
    double size;
    int userCount;
    int groupCount;

    Traffic() : size(0), userCount(0), groupCount(0) { }
};

struct Bandwidth
{
    double speed;
    int userCount;

    Bandwidth() : speed(0), userCount(0) { }
};

bool isDebug()
{
    const char* s = getenv("PREBW_DEBUG");
    return s && !strcasecmp(s, "TRUE");
}

const std::size_t NUM_SNAPSHOTS = sizeof(SNAPSHOTS) / sizeof(SNAPSHOTS[0]);

bool isInDirectory(const char* dir, const char* subdir)
{
    std::size_t dirLen = std::strlen(dir);
    if (std::strncmp(subdir, dir, dirLen) != 0) {
        return false;
    }

    return *(subdir + dirLen) == '/' || *(subdir + dirLen) == '\0';
}

bool collectBandwidth(
        const std::string& dirname,
        key_t ipcKey,
        Bandwidth* result
    )
{
    long long shmid = shmget(ipcKey, 0, 0);
    if (shmid < 0) {
        if (errno == ENOENT) {
            std::cout << "no users online\n";
            return result != NULL;
        }
        std::cout << "shmget: " << strerror(errno) << "\n";
        return false;
    }

    ONLINE* online = (ONLINE*) shmat(shmid, NULL, SHM_RDONLY);
    if (online == (ONLINE*) -1) {
        std::cout << "shmat: " << strerror(errno) << "\n";
        return false;
    }

    struct shmid_ds	stat;
    if (shmctl(shmid, IPC_STAT, &stat) < 0) {
        std::cout << "shmctl: " << strerror(errno) << "\n";
        shmdt(online);
        return false;
    }

    if (result) {
        result->speed = 0;
        result->userCount = 0;
    }

    struct timeval now;
    gettimeofday(&now, NULL);

    const std::size_t numOnline  = stat.shm_segsz / sizeof(ONLINE);
    int numDownloaders           = 0;

    for (std::size_t i = 0; i < numOnline; ++i) {
        if (online[i].procid == 0) {
            continue;
        }

        if (strncasecmp(online[i].status, "RETR ", 5) != 0) {
            std::cout << "skipping, not downloading: " << online[i].status << "\n";
            continue;
        }

        if (online[i].bytes_xfer <= 100 * 1024) {
            std::cout << "skipping, not enough bytes: " << online[i].bytes_xfer << "\n";
            continue;
        }

        if (!isInDirectory(dirname.c_str(), online[i].currentdir)) {
            std::cout << "skipping, not in directory: " << online[i].currentdir << "\n";
            continue;
        }

        if (result) {
            double duration = (now.tv_sec - online[i].tstart.tv_sec) +
                              ((now.tv_usec - online[i].tstart.tv_usec) / 1000000.0);
            result->speed += (duration == 0 ? online[i].bytes_xfer
                                            : online[i].bytes_xfer / duration) / 1024.0 / 1024.0;
            ++result->userCount;
        }

        ++numDownloaders;
    }

    shmdt(online);

    return result || numDownloaders > 0;
}

bool collectBandwidthSnapshot(
        const std::string& dirname,
        key_t ipcKey,
        std::time_t snapshotTime,
        Bandwidth& result
    )
{
    result.speed = 0;
    result.userCount = 0;

    while (true) {
        Bandwidth bandwidth;
        if (!collectBandwidth(dirname, ipcKey, &bandwidth)) {
            return false;
        }

        result.speed     = std::max(result.speed,     bandwidth.speed);
        result.userCount = std::max(result.userCount, bandwidth.userCount);

        if (std::time(NULL) >= snapshotTime) {
            break;
        }

        usleep(REFRESH_RATE);
    }

    return true;
}

std::vector<Bandwidth> collectBandwidthSnapshots(
        const std::string& dirname,
        key_t ipcKey,
        std::time_t startTime
    )
{
    std::vector<Bandwidth> bandwidths;
    for (std::size_t i = 0; i < NUM_SNAPSHOTS; ++i) {
        Bandwidth bandwidth;
        if (!collectBandwidthSnapshot(dirname, ipcKey, startTime + SNAPSHOTS[i], bandwidth)) {
            return std::vector<Bandwidth>();
        }
        bandwidths.push_back(bandwidth);
    }
    return bandwidths;
}

void waitNoTransfersOrCutOff(
        const std::string& dirname,
        key_t ipcKey,
        std::time_t cutOffTime
    )
{
    int consecutive = 0;
    while (std::time(NULL) < cutOffTime) {
        if (!collectBandwidth(dirname, ipcKey, NULL)) {
            if (++consecutive == 3) {
                std::cout << "no active transfers\n";
                return;
            }
        }
        else {
            consecutive = 0;
        }
        sleep(1);
    }

    std::cout << "transfers still active, cut off reached\n";
}

bool collectTrafficStats(const std::string& dirname, Traffic& traffic)
{
    std::ifstream f(XFER_LOG);
    if (!f) {
        return false;
    }

    traffic.size       = 0;
    traffic.userCount  = 0;
    traffic.groupCount = 0;

    std::set<std::string> users;
    std::set<std::string> groups;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream is(line);
        long bytes;
        std::string user;
        std::string group;
        std::string path;
        std::string type;
        std::string skip;
        is >> skip >> skip >> skip >> skip >> skip >> skip >> skip;
        is >> bytes >> path >> skip >> skip >> type >> skip >> user >> group;

        if (is.good() && type == "o" && isInDirectory(dirname.c_str(), path.c_str())) {

            traffic.size += bytes / 1024.0 / 1024.0;
            if (users.insert(user).second) {
                ++traffic.userCount;
            }

            if (groups.insert(group).second) {
                ++traffic.groupCount;
            }
        }
    }

    return true;
}

std::string formatTimestamp()
{
    const std::time_t now = std::time(NULL);
    char timestamp[26];
    std::strftime(timestamp, sizeof(timestamp),
                  "%a %b %e %T %Y",
                  std::localtime(&now));
    return timestamp;
}

bool log(
        const std::string& dirname,
        const std::vector<Bandwidth>& bandwidths,
        const Traffic& traffic
    )
{
    std::ofstream f(GLFTPD_LOG, std::ios_base::app);
    if (!f) {
        return false;
    }

    f << formatTimestamp() << " PREBW: \"" << dirname << "\" ";

    for (std::size_t i = 0; i < NUM_SNAPSHOTS; ++i) {
        f << "\"" << bandwidths[i].userCount << "\" "
          << "\"" << std::fixed << std::setprecision(1) << bandwidths[i].speed << "\" ";
    }

    f << "\"" << traffic.userCount  << "\" "
      << "\"" << traffic.groupCount << "\" "
      << "\"" << std::fixed << std::setprecision(2) << traffic.size << "\"" << std::endl;

    return f.good();
}

int main(int argc, char** argv)
{
    if (!isDebug()) {
        std::cout.setstate(std::ios::failbit);
        std::cout.setstate(std::ios::failbit);
    }

    if (argc != 2) {
        std::cout << "usage: " << argv[0] << " <dirname>\n";
        return 1;
    }

    const std::string dirname   = argv[1];
    const key_t ipcKey          = std::strtoll(IPC_KEY, NULL, 16);
    const std::time_t startTime = std::time(NULL);

    std::vector<Bandwidth> bandwidths = collectBandwidthSnapshots(dirname, ipcKey, startTime);
    if (bandwidths.size() != NUM_SNAPSHOTS) {
        std::cout << "bandwidth snapshot collection failed\n";
        return 1;
    }

    waitNoTransfersOrCutOff(dirname, ipcKey, startTime + CUT_OFF);

    Traffic traffic;
    if (!collectTrafficStats(dirname, traffic)) {
        std::cout << "traffic collection failed\n";
        return 1;
    }

    if (!log(dirname, bandwidths, traffic)) {
        std::cout << "error while writing to glftpd.log\n";
        return 1;
    }
}
