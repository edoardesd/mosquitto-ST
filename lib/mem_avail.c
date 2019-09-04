void mem_avail(void)
{
  char *cmd = "awk '{ if (NR == 2) { print $4 }}' /proc/meminfo";
  
  FILE *cmdfile = popen(cmd, "r");
  char result[256] = { 0 };
  
  while (fgets(result, sizeof(result), cmdfile) != NULL) {
    printf("%s\n", result);
  }
  
  pclose(cmdfile);
}

// Alternately for free memory and other info
//------------------------------------------
#include <sys/sysinfo.h>


int sysinfo(struct sysinfo *info);

struct sysinfo {
               long uptime;             /* Seconds since boot */
               unsigned long loads[3];  /* 1, 5, and 15 minute load averages */
               unsigned long totalram;  /* Total usable main memory size */
               unsigned long freeram;   /* Available memory size */
               unsigned long sharedram; /* Amount of shared memory */
               unsigned long bufferram; /* Memory used by buffers */
               unsigned long totalswap; /* Total swap space size */
               unsigned long freeswap;  /* swap space still available */
               unsigned short procs;    /* Number of current processes */
               unsigned long totalhigh; /* Total high memory size */
               unsigned long freehigh;  /* Available high memory size */
               unsigned int mem_unit;   /* Memory unit size in bytes */
               char _f[20-2*sizeof(long)-sizeof(int)]; /* Padding to 64 bytes */
           };
           
unsigned long mem_avail()
{
  struct sysinfo info;
  
  if (sysinfo(&info) < 0)
    return 0;
    
  return info.freeram;
}