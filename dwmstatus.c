#define _BSD_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <X11/Xlib.h>

/* ********** *
 * STRUCTURES *
 * ********** */
struct Interface {
  char name[20];
  unsigned long long int bytes_sent;
  unsigned long long int bytes_rec;
};

/* ********** *
 * PROTOTYPES *
 * ********** */
void logger(char* chaine);

char * readfile(char *base, char *file);
char * getbattery(char *base);

// For the network
char* get_dev_speed(char* name,
    unsigned long long int oldsent,
    unsigned long long int oldrec,
    unsigned long long int newsent,
    unsigned long long int newrec);
int devinfo(struct Interface interfaces[]);
void network(char netstr[]);

char *tzparis = "Europe/Paris";

static Display *dpy;


  char *
smprintf(char *fmt, ...)
{
  va_list fmtargs;
  char *ret;
  int len;

  va_start(fmtargs, fmt);
  len = vsnprintf(NULL, 0, fmt, fmtargs);
  va_end(fmtargs);

  ret = malloc(++len);
  if (ret == NULL) {
    perror("malloc");
    exit(1);
  }

  va_start(fmtargs, fmt);
  vsnprintf(ret, len, fmt, fmtargs);
  va_end(fmtargs);

  return ret;
}

  void
settz(char *tzname)
{
  setenv("TZ", tzname, 1);
}

  char *
mktimes(char *fmt, char *tzname)
{
  char buf[129];
  time_t tim;
  struct tm *timtm;

  memset(buf, 0, sizeof(buf));
  settz(tzname);
  tim = time(NULL);
  timtm = localtime(&tim);
  if (timtm == NULL) {
    perror("localtime");
    exit(1);
  }

  if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
    fprintf(stderr, "strftime == 0\n");
    exit(1);
  }

  return smprintf("%s", buf);
}

  void
setstatus(char *str)
{
  XStoreName(dpy, DefaultRootWindow(dpy), str);
  XSync(dpy, False);
}

/*
   char *
   loadavg(void)
   {
   double avgs[3];

   if (getloadavg(avgs, 3) < 0) {
   perror("getloadavg");
   exit(1);
   }

   return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
   }
   */

void logger(char* chaine) {
  FILE* fichier = NULL;

  fichier = fopen("/var/log/dwmstatus.log", "a");
  char* tmparis = mktimes("%a %d %b %H:%M %Y", tzparis);

  if (fichier != NULL) {
    fprintf(fichier, "%s: %s", tmparis, chaine);
    fclose(fichier);
  }
}


/* ******* *
 * Network *
 * ******* */

int devinfo(struct Interface interfaces[]) {
  // Create a buffer to store the lines and open the file
  static int line_length = 255;
  char* line = (char*) calloc(line_length, 1);
  FILE* file = fopen("/proc/net/dev", "r");

  // Ignore the first two lines
  fgets(line, line_length, file);
  fgets(line, line_length, file);

  // For each line
  char *part, *iface;
  int i;
  int nb_ifaces = 0;
  while(fgets(line, line_length, file)) {
    // Get the name of the interface
    iface = strtok (line,": ");
    // If it is local loopback, ignore it
    if(strcmp(iface, "lo")) {
      part = "42";
      strcpy(interfaces[nb_ifaces].name, iface);
      // Otherwise, get the number of bytes sent and received
      for(i=0; part != NULL; i++) {
        part = strtok (NULL, ": ");
        if(i == 0) { // received bytes
          interfaces[nb_ifaces].bytes_rec = atoi(part);
        }
        else if(i == 8) { // sent bytes
          interfaces[nb_ifaces].bytes_sent = atoi(part);
        }	
      }
      nb_ifaces++;
    }
  }

  // Cleanup and return the result
  fclose(file);
  free(line);
  return nb_ifaces;
}

void network(char netstr[]) {
  struct Interface interfaces_before[5];
  struct Interface interfaces_after[5];
  int nb_ifaces = devinfo(interfaces_before);
  sleep(1);
  int nb_ifaces_2 = devinfo(interfaces_after);
  strcpy(netstr, "");

  if(nb_ifaces != nb_ifaces_2) {
    logger("[Network] - The number of interfaces has changed");
    strcpy(netstr, "[-IFACE CHANGE-]");
    return;
  }

  int i;
  for(i = 0; i < nb_ifaces; i++) {
    // Find the interface corresponding to
    // interfaces_before[i] in interfaces_after
    int j;
    for(j = 0; j < nb_ifaces; j++) {
      if(!strcmp(interfaces_before[i].name, interfaces_after[j].name)) {
        if(interfaces_before[i].bytes_sent != 0 &&
            interfaces_before[i].bytes_rec  != 0 &&
            interfaces_after[j].bytes_sent  != 0 &&
            interfaces_after[j].bytes_rec   != 0) {	

          char* ifacestr = get_dev_speed(interfaces_before[i].name,
              interfaces_before[i].bytes_sent,
              interfaces_before[i].bytes_rec,
              interfaces_after[j].bytes_sent,
              interfaces_after[j].bytes_rec);
          if(ifacestr != NULL) {
            strcat(netstr, ifacestr);
          }
        }	
      }
    }


  }

  if(strcmp(netstr, "")) {
    logger("[Network] - Everything is fine");
    return;
  }

  logger("[Network] - No interface seems to be up");
  strcpy(netstr, "[-NO IFACE UP-]");
  return;
}

char* get_dev_speed(char* name,
    unsigned long long int oldsent,
    unsigned long long int oldrec,
    unsigned long long int newsent,
    unsigned long long int newrec) {
  double downspeed, upspeed;
  char *downspeedstr, *upspeedstr;
  char *retstr;

  downspeedstr = (char *) malloc(15);
  upspeedstr = (char *) malloc(15);
  retstr = (char *) malloc(42);

  downspeed = (newrec - oldrec) / 1024.0;
  if (downspeed > 1024.0) {
    downspeed /= 1024.0;
    sprintf(downspeedstr, "%.3f MB/s", downspeed);
  } else {
    sprintf(downspeedstr, "%.2f KB/s", downspeed);
  }

  upspeed = (newsent - oldsent) / 1024.0;
  if (upspeed > 1024.0) {
    upspeed /= 1024.0;
    sprintf(upspeedstr, "%.3f MB/s", upspeed);
  } else {
    sprintf(upspeedstr, "%.2f KB/s", upspeed);
  }

  if(downspeed == 0 && upspeed == 0) {
    sprintf(retstr, "[%s]", name);
    //return NULL;
  }
  else {
    sprintf(retstr, "[%s D: %s ~ U: %s]", name, downspeedstr, upspeedstr);
  }

  free(downspeedstr);
  free(upspeedstr);
  return retstr;
}

/* ******* *
 * Battery *
 * ******* */

  char *
readfile(char *base, char *file)
{
  char *path, line[513];
  FILE *fd;

  memset(line, 0, sizeof(line));

  path = smprintf("%s/%s", base, file);
  fd = fopen(path, "r");
  if (fd == NULL)
    return NULL;
  free(path);

  if (fgets(line, sizeof(line)-1, fd) == NULL)
    return NULL;
  fclose(fd);

  return smprintf("%s", line);
}

/*
 * Linux seems to change the filenames after suspend/hibernate
 * according to a random scheme. So just check for both possibilities.
 */
  char *
getbattery(char *base)
{
  char *co;
  char* remaining_time;
  int descap, remcap;
  double remaining, using, voltage, current;
  char status = '?';

  descap = -1;
  remcap = -1;
  using = -1;
  remaining = -1;

  co = readfile(base, "present");
  if (co == NULL || co[0] != '1') {
    if (co != NULL) free(co);
    return smprintf("not present");
  }
  free(co);

  co = readfile(base, "charge_full_design");
  if (co == NULL) {
    //co = readfile(base, "energy_full_design");
    co = readfile(base, "energy_full");
    if (co == NULL)
      return smprintf("");
  }
  sscanf(co, "%d", &descap);
  free(co);

  co = readfile(base, "charge_now");
  if (co == NULL) {
    co = readfile(base, "energy_now");
    if (co == NULL)
      return smprintf("");
  }
  sscanf(co, "%d", &remcap);
  free(co);

  co = readfile(base, "power_now"); /* µWattage being used */
  if (co == NULL) {
    co = readfile(base, "voltage_now");
    sscanf(co, "%lf", &voltage);
    free(co);
    co = readfile(base, "current_now");
    sscanf(co, "%lf", &current);
    free(co);
    remcap  = (voltage / 1000.0) * ((double)remcap / 1000.0);
    descap  = (voltage / 1000.0) * ((double)descap / 1000.0);
    using = (voltage / 1000.0) * ((double)current / 1000.0);
  } else {
    sscanf(co, "%lf", &using);
    free(co);
  }

  if (remcap < 0 || descap < 0)
    return smprintf("invalid");

  // Set status (charging, discharging or full)
  co = readfile(base, "status");
  if(!strncmp(co, "Charging", 8)) {
    status = '^';
    remaining = ((double)descap - (double)remcap) / using;
  }
  else if(!strncmp(co, "Discharging", 11)) {
    status = 'v';
    remaining = (double)remcap / using;
  }
  else {
    status = '=';
    remaining = 0;
    remaining_time="";
  }

  if(remaining != 0) {
    /* convert to hour:min:sec */
    int hours, seconds, minutes, secs_rem;
    secs_rem = (int)(remaining * 3600.0);
    hours = secs_rem / 3600;
    seconds = secs_rem - (hours * 3600);
    minutes = seconds / 60;
    seconds -= (minutes *60);

    if(seconds < 0 || minutes < 0 || hours < 0) {
      remaining_time = smprintf(" ...");
    }
    else if(hours) {
      remaining_time = smprintf(" %02d:%02d:%02d", hours, minutes, seconds);
    }
    else {
      remaining_time = smprintf(" %02d:%02d", minutes, seconds);
    }
  }

  return smprintf("[%c%.0f%%%s]", status, ((float)remcap / (float)descap) * 100, remaining_time);
}

  int
main(void)
{
  char *status;
  char *tmparis;
  char *battery;
  char netstats[150];

  if (!(dpy = XOpenDisplay(NULL))) {
    fprintf(stderr, "dwmstatus: cannot open display.\n");
    return 1;
  }

  //for (;;sleep(60)) {
  for (;;sleep(10)) {
    tmparis = mktimes("%a %d %b %H:%M %Y", tzparis);
    battery = getbattery("/sys/class/power_supply/BAT0/");
    network(netstats);

    status = smprintf("%s %s %s", netstats, tmparis, battery);

    setstatus(status);

    //free(netstats);
    free(battery);
    free(tmparis);
    free(status);
  }

  XCloseDisplay(dpy);

  return 0;
}
