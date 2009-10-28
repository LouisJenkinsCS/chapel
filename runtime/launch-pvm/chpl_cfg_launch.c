#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <chplio_md.h>
#include <signal.h>

#include "pvm3.h"

#include "chplcgfns.h"
#include "chplrt.h"
#include "chplcomm.h"
#include "chpl_mem.h"
#include "chplsys.h"
#include "chplthreads.h"
#include "error.h"

#include "chpllaunch.h"

#define NOTIFYTAG 4194295
#define PRINTF_BUFF_LEN 1024

int tids[32];

// For memory allocation
#define M_ARGV0REP            0x1
#define M_ARGV2               0x2
#define M_COMMANDTOPVM        0x4
#define M_ENVIRONMENT         0x8
#define M_HOSTFILE            0x10
#define M_MULTIREALMENVNAME   0x20
#define M_MULTIREALMPATHTOADD 0x40
#define M_PVMNODESTOADD       0x80
#define M_REALMTOADD          0x100
#define M_REALMTYPE           0x200
char** argv2;
char* argv0rep;
char* commandtopvm;
char* environment;
char* hostfile;
char* multirealmenvname;
char* multirealmpathtoadd[2048];
char* pvmnodestoadd[2048];
char* realmtoadd[2048];
char* realmtype;
int memalloced = 0;
int totalalloced = 0;


// Helper function
static char *replace_str(char *str, char *orig, char *rep)
{
  static char buffer[4096];
  static char part2[4096];
  char *p;

  if(!(p = strstr(str, orig)))  // Is 'orig' even in 'str'?
    return str;

  strncpy(buffer, str, p-str); // Copy characters from 'str' start to 'orig' st$
  buffer[p-str] = '\0';

  sprintf(part2, "%s", p+strlen(orig));
  sprintf(buffer+(p-str), "%s%s", rep, part2);

  return buffer;
}


static int hostsAdded = 0;
static int32_t numLocales = 0;

static void hosts_cleanup(void) {
  int infos[256];
  if (hostsAdded) {
    int i = pvm_setopt(PvmAutoErr, 0);
    pvm_delhosts(pvmnodestoadd, numLocales, infos);
    pvm_setopt(PvmAutoErr, i);
  }
}

static void memory_cleanup(void) {
  int i;
  if (memalloced & M_ARGV0REP) chpl_free(argv0rep, -1, "");
  if (memalloced & M_ARGV2) chpl_free(argv2, -1, "");
  if (memalloced & M_ENVIRONMENT) chpl_free(environment, -1, "");
  if (memalloced & M_HOSTFILE) chpl_free(hostfile, -1, "");
  if (memalloced & M_MULTIREALMENVNAME) chpl_free(multirealmenvname, -1, "");
  if (memalloced & M_MULTIREALMPATHTOADD) for (i = 0; i < totalalloced; i++) chpl_free(multirealmpathtoadd[i], -1, "");
  if (memalloced & M_PVMNODESTOADD) for (i = 0; i < totalalloced; i++) chpl_free(pvmnodestoadd[i], -1, "");
  if (memalloced & M_REALMTOADD) for (i = 0; i < totalalloced; i++) chpl_free(realmtoadd[i], -1, "");
  if (memalloced & M_REALMTYPE) chpl_free(realmtype, -1, "");
  return;
}


static void cleanup_for_exit(void) {
  hosts_cleanup();
  memory_cleanup();
}


static void pvm_launcher_error(const char* errorMsg) {
  cleanup_for_exit();
  chpl_error(errorMsg, 0, "<PVM launcher>");
}


static void error_exit(int sig) {
  int i;
  char buffer[PRINTF_BUFF_LEN];

  fflush(stdout);
  fflush(stderr);

  if (verbosity > 1) {
    fprintf(stderr, "Received a signal\n");
  }
  for (i=0; i<numLocales; i++) {
    if (verbosity > 1) {
      fprintf(stderr, "Calling pvm_kill(%d)\n", tids[i]);
    }
    pvm_kill(tids[i]);
  }
  if (verbosity > 1) {
    fprintf(stderr, "Calling pvm_halt()\n");
  }
  pvm_halt();
  sprintf(buffer, "echo reset | %s/lib/pvm > /dev/null 2>&1", PVM_ROOT);
  if (verbosity > 1) {
    fprintf(stderr, "Calling '%s'\n", buffer);
  }
  system(buffer);
  sprintf(buffer, "echo halt | %s/lib/pvm > /dev/null 2>&1", PVM_ROOT);
  if (verbosity > 1) {
    fprintf(stderr, "Calling '%s'\n", buffer);
  }
  system(buffer);
  for (i=0; i<numLocales; i++) {
    sprintf(buffer, "ssh -q %s \"touch /tmp/pvm-chpl-deleteme && rm -rf /tmp/pvm*\" > /dev/null 2>&1", pvmnodestoadd[i]);
    if (verbosity > 1) {
      fprintf(stderr, "Calling '%s'\n", buffer);
    }
    system(buffer);
  }
  
  memory_cleanup();
  exit(1);
}


static void missing_file_error(const char* filename) {
  char errorMsg[FILENAME_MAX + 256];
  sprintf(errorMsg, "unable to locate file: %s", filename);
  pvm_launcher_error(errorMsg);
}


static int pvm_spawn_wrapper(char* command, char** args, char* node, int* tid) {
  int numt;
  if (verbosity > 1) {
    int j = 0;
    fprintf(stderr, "trying to spawn %s", command);
    do {
      fprintf(stderr, " %s", argv2[j]);
      j++;
    } while (argv2[j]);
    fprintf(stderr, " on %s\n", node);
  }
  numt = pvm_spawn(command, args, 1, node, 1, tid);
  if (numt == 0) {
    if (*tid != PvmNoFile) {
      char errorMsg[256];
      snprintf(errorMsg, 255, "pvm_spawn got error %d", *tid);
      pvm_launcher_error(errorMsg);
    }
    return 0;
  } else {
    if (verbosity > 1) {
      fprintf(stderr, "tid = %d\n", *tid);
    }
    return 1;
  }
}


void chpl_launch(int argc, char* argv[], int32_t init_numLocales) {
  int i, j, info;
  int infos[256];
  int infos2[256];
  struct utsname myhostname;
  char pvmnodetoadd[256];
  int commsig = 0;
  static char *hosts2redo[2048];
  int bufid;
  int usingbaserealm;

  // These are for receiving singals from slaves
  int hostsexit;
  int fdnum;
  char buffer[PRINTF_BUFF_LEN];
  char description[PRINTF_BUFF_LEN];  // gdb specific
  int ignorestatus;                   // gdb specific
  int who;                            // gdb specific

  char nameofbin[1024];
  char numlocstr[128];

  // Add nodes to PVM configuration.
  FILE* nodelistfile;

  int k;                              // k iterates over chpl_numRealms
  int lpr;                            // locales per realm
  const char* multirealmenv;
  int baserealm = 0;
  const char* t;                      // temp string for checking nil

  numLocales = init_numLocales;

  // Ensure PVM_ROOT is set if the user hasn't
  if (getenv("PVM_ROOT") == NULL) {
    putenv((char*)"PVM_ROOT="PVM_ROOT);
  }

  // Signal handlers
  signal(SIGINT, error_exit);
  signal(SIGQUIT, error_exit);
  signal(SIGKILL, error_exit);
  signal(SIGTERM, error_exit);

  // Get a new argument list for PVM spawn.
  // The last argument needs to be the number of locations for the PVM
  // comm layer to use it. The comm layer strips this off.

  argv2 = chpl_malloc(((argc+1) * sizeof(char *)), sizeof(char*), CHPL_RT_MD_PVM_SPAWN_THING, -1, "");
  memalloced |= M_ARGV2;
  for (i=0; i < (argc-1); i++) {
    argv2[i] = argv[i+1];
  }
  sprintf(numlocstr, "%d", numLocales);
  argv2[argc-1] = numlocstr;
  argv2[argc] = NULL;

  // Add nodes to PVM configuration.
  i = 0;
  for (k = 0; k < chpl_numRealms; k++) {
    if (chpl_numRealms != 1) {
      lpr = chpl_localesPerRealm(k);
      if (lpr == 0) {
        continue;
      }
    } else {
      lpr = numLocales;
    }
    hostfile = chpl_malloc(1024, sizeof(char*), CHPL_RT_MD_PVM_LIST_OF_NODES, -1, "");
    memalloced |= M_HOSTFILE;
    realmtype = chpl_malloc(1024, sizeof(char*), CHPL_RT_MD_PVM_LIST_OF_NODES, -1, "");
    memalloced |= M_REALMTYPE;
    multirealmenvname = chpl_malloc(1024, sizeof(char*), CHPL_RT_MD_PVM_LIST_OF_NODES, -1, "");
    memalloced |= M_MULTIREALMENVNAME;
    if (chpl_numRealms != 1) {
      sprintf(realmtype, "%s", chpl_realmType(k));
    } else {
      sprintf(realmtype, "%s", getenv((char *)"CHPL_HOST_PLATFORM"));
    }
    sprintf(multirealmenvname, "CHPL_MULTIREALM_LAUNCH_DIR_%s", realmtype);
    multirealmenv = getenv(multirealmenvname);
    if (multirealmenv == NULL) {
      multirealmenv = CHPL_HOME;
    }
    chpl_free(multirealmenvname, -1, "");
    memalloced &= ~M_MULTIREALMENVNAME;
    sprintf(hostfile, "%s%s%s", CHPL_HOME, "/hostfile.", realmtype);
    
    if ((nodelistfile = fopen(hostfile, "r")) == NULL) {
      missing_file_error(hostfile);
    }
    chpl_free(hostfile, -1, "");
    memalloced &= ~M_HOSTFILE;
    j = 0;
    while (((fscanf(nodelistfile, "%s", pvmnodetoadd)) == 1) && (j < lpr)) {
      pvmnodestoadd[i] = chpl_malloc((strlen(pvmnodetoadd)+1), sizeof(char *), CHPL_RT_MD_PVM_LIST_OF_NODES, -1, "");
      memalloced |= M_PVMNODESTOADD;
      realmtoadd[i] = chpl_malloc((strlen(realmtype)+1), sizeof(char *), CHPL_RT_MD_PVM_LIST_OF_NODES, -1, "");
      memalloced |= M_REALMTOADD;
      multirealmpathtoadd[i] = chpl_malloc((strlen(multirealmenv)+1), sizeof(char *), CHPL_RT_MD_PVM_LIST_OF_NODES, -1, "");
      memalloced |= M_MULTIREALMPATHTOADD;
      strcpy(pvmnodestoadd[i], pvmnodetoadd);
      strcpy(realmtoadd[i], realmtype);
      strcpy(multirealmpathtoadd[i], multirealmenv);
      //      fprintf(stderr, "Adding pvmnodestoadd[%d], realm %s of j iter %d on directory %s: %s\n", i, realmtoadd[i], j, multirealmpathtoadd[i], pvmnodestoadd[i]);
      i++;
      j++;
    }
    chpl_free(realmtype, -1, "");
    memalloced &= ~M_REALMTYPE;
    // Check to make sure user hasn't specified more nodes (-nl <n>) than
    // what's included in the hostfile.
    fclose(nodelistfile);
    if (j < lpr) {
      pvm_launcher_error("Number of locales specified is greater than what's known in PVM hostfile");
    }
  }
  totalalloced = i;

  // Check to see if daemon is started or not by this user. If not, start one.
  i = pvm_setopt(PvmAutoErr, 0);
  if (verbosity > 1) {
    fprintf(stderr, "calling pvm_start_pvmd(0, NULL, 1);\n");
  }
  info = pvm_start_pvmd(0, NULL, 1);
  pvm_setopt(PvmAutoErr, i);

  if ((info != 0) && (info != -28)) {
    char errorMsg[256];
    snprintf(errorMsg, 255, "Problem starting PVM daemon (%d)", info);
    pvm_launcher_error(errorMsg);
  }

  // Find the node we're on. We use this in spawning (to know what realm
  // type we are to replace that string with an architecture appropriate one
  uname(&myhostname);
  usingbaserealm = 0;
  for (i = 0; i < numLocales; i++) {
    if (!(strcmp((char *)pvmnodestoadd[i], myhostname.nodename))) {
      baserealm = i;
      usingbaserealm = 1;
      break;
    }
  }
  if (!usingbaserealm) {
    realmtype = chpl_malloc(1024, sizeof(char*), CHPL_RT_MD_PVM_LIST_OF_NODES, -1, "");
    memalloced |= M_REALMTYPE;
    sprintf(realmtype, "%s", getenv((char *)"CHPL_HOST_PLATFORM"));
  }

  // Add everything (turn off errors -- we don't care if we add something
  // that's already there).
  i = pvm_setopt(PvmAutoErr, 0);
  if (verbosity > 1) {
    int loc;
    fprintf(stderr, "calling pvm_addhosts({");
    for (loc=0; loc<numLocales; loc++) {
      fprintf(stderr, "%s%s", (loc ? ", " : ""), pvmnodestoadd[loc]);
    }
    fprintf(stderr, "}, %d, infos);\n", numLocales);
  }
  info = pvm_addhosts( (char **)pvmnodestoadd, numLocales, infos );
  pvm_setopt(PvmAutoErr, i);
  // Something happened on addhosts -- likely old pvmd running
  for (i = 0; i < numLocales; i++) {
    if ((infos[i] < 0) && (infos[i] != PvmDupHost)) {
      hosts2redo[0] = pvmnodestoadd[i];
      if (verbosity > 1) {
        fprintf(stderr, "calling pvm_addhosts({%s}, 1, infos2);\n", *hosts2redo);
      }
      info = pvm_addhosts( (char **)hosts2redo, 1, infos2);
      if (infos2[0] < 0) {
        char errorMsg[256];
        snprintf(errorMsg, 255, "Remote error on %s (%d) -- shutting down host",
                 hosts2redo[0], infos2[0]);
        pvm_launcher_error(errorMsg);
      }
    }
  }
  hostsAdded = 1;

  argv0rep = chpl_malloc(1024, sizeof(char*), CHPL_RT_MD_PVM_SPAWN_THING, -1, "");
  memalloced |= M_ARGV0REP;
  strcpy(argv0rep, argv[0]);
  // Take extra if-else step in case there's no / in executed command
  t = strrchr(argv[0], '/');
  if (t)
    strcpy(nameofbin, t);
  else
    strcpy(nameofbin, argv[0]);
  // Build the command to send to pvm_spawn.
  // First, try the command built from CHPL_MULTIREALM_LAUNCH_DIR_<realm>
  //      and the executable_real. Replace architecture strings with target
  //      architecture names.
  // If this doesn't work, store the name of the file tried with the node
  //      into a debug message. Then try just what was passed on the command
  //      line.
  // Failing that, try the current working directory with executable_real.
  // If this doesn't work, error out with the debug message.
  commandtopvm = chpl_malloc(1024, sizeof(char*), CHPL_RT_MD_PVM_SPAWN_THING, -1, "");
  memalloced |= M_COMMANDTOPVM;
  for (i = 0; i < numLocales; i++) {
    //    fprintf(stderr, "Loop i=%d (iteration %d of %d)\n", i, i+1, numLocales);
    *commandtopvm = '\0';
    environment = chpl_malloc(1024, sizeof(char*), CHPL_RT_MD_PVM_SPAWN_THING, -1, "");
    memalloced |= M_ENVIRONMENT;
    *environment = '\0';
    sprintf(environment, "%s/", multirealmpathtoadd[i]);
    strcat(commandtopvm, environment);
    chpl_free(environment, -1, "");
    memalloced &= ~M_ENVIRONMENT;
    strcat(commandtopvm, nameofbin);
    strcat(commandtopvm, "_real");

    if (usingbaserealm) {
      while (strstr(commandtopvm, realmtoadd[baserealm]) && 
             (chpl_numRealms != 1) &&
             (strcmp(realmtoadd[baserealm], realmtoadd[i]))) {
        commandtopvm = replace_str(commandtopvm, realmtoadd[baserealm], realmtoadd[i]);
      }
    } else {
      while (strstr(commandtopvm, realmtype)) {
        commandtopvm = replace_str(commandtopvm, realmtype, realmtoadd[i]);
      }
    }

    if (!pvm_spawn_wrapper(commandtopvm, argv2, pvmnodestoadd[i], &tids[i])) {
      *commandtopvm = '\0';
      strcat(commandtopvm, argv0rep);
      strcat(commandtopvm, "_real");

      if (usingbaserealm) {
        while (strstr(commandtopvm, realmtoadd[baserealm]) && 
               (chpl_numRealms != 1) &&
               (strcmp(realmtoadd[baserealm], realmtoadd[i]))) {
          commandtopvm = replace_str(commandtopvm, realmtoadd[baserealm], realmtoadd[i]);
        }
      } else {
        while (strstr(commandtopvm, realmtype)) {
          commandtopvm = replace_str(commandtopvm, realmtype, realmtoadd[i]);
        }
      }

      if (!pvm_spawn_wrapper(commandtopvm, argv2, pvmnodestoadd[i], &tids[i] )) {
        sprintf(commandtopvm, "%s/%s%s", getenv((char *)"PWD"), nameofbin, "_real");
        if (!pvm_spawn_wrapper(commandtopvm, argv2, pvmnodestoadd[i], &tids[i] )) {
          pvm_launcher_error("Unable to spawn child process(es)");
        }
      }
    }
  }
  chpl_free(argv0rep, -1, "");
  memalloced &= ~M_ARGV0REP;
  chpl_free(argv2, -1, "");
  memalloced &= ~M_ARGV2;
  if (!usingbaserealm) {
    chpl_free(realmtype, -1, "");
    memalloced &= ~M_REALMTYPE;
  }
  for (i = 0; i < totalalloced; i++) chpl_free(multirealmpathtoadd[i], -1, "");
  memalloced &= ~M_MULTIREALMPATHTOADD;
  for (i = 0; i < totalalloced; i++) chpl_free(realmtoadd[i], -1, "");
  memalloced &= ~M_REALMTOADD;

  // We have a working configuration. What follows is the communication
  // between the slaves and the parent (this process).
  info = pvm_mytid();

  hostsexit = 0;
  while (commsig == 0) {
    bufid = pvm_recv(-1, NOTIFYTAG);
    pvm_upkint(&commsig, 1, 1);
    // exit case
    if (commsig == 1) {
      hostsexit++;
      if (hostsexit != numLocales) {
        commsig = 0;
      }
    }
    // fprintf case
    if (commsig == 2) {
      pvm_upkint(&fdnum, 1, 1);
      pvm_upkstr(buffer);
      if (fdnum == 0) {
        fprintf(stdin, "%s", buffer);
      } else if (fdnum == 1) {
        fprintf(stdout, "%s", buffer);
      } else {
        fprintf(stderr, "%s", buffer);
      }
      fflush(stdout);
      fflush(stderr);
      commsig = 0;
    }
    // printf case
    if (commsig == 3) {
      pvm_upkstr(buffer);
      printf("%s", buffer);
      fflush(stdout);
      fflush(stderr);
      commsig = 0;
    }
    // Run in gdb mode
    if (commsig == 4) {
      pvm_upkint(&who, 1, 1);
      pvm_upkstr(buffer);
      pvm_upkstr(description);
      pvm_upkint(&ignorestatus, 1, 1);
      info = system(buffer);
      pvm_initsend(PvmDataDefault);
      pvm_pkint(&info, 1, 1);
      pvm_send(who, NOTIFYTAG);
      if (info == -1) {
        pvm_launcher_error("system() fork failed");
      } else if (info != 0 && !ignorestatus) {
        pvm_launcher_error(description);
      }
    }
  }

  cleanup_for_exit();
  exit(0);
}


int chpl_launch_handle_arg(int argc, char* argv[], int argNum,
                           int32_t lineno, chpl_string filename) {
  return 0;
}

